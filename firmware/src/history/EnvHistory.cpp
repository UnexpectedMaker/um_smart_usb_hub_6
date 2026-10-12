// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// EnvHistory.cpp — rolling temperature/humidity history for the sidebar graph.
//
// Samples are kept in RAM only. The UI can request the five-minute window or
// points newer than a previous sample timestamp.
#include "history/EnvHistory.h"
#include <esp_heap_caps.h>
#include <math.h>

EnvHistory g_envHistory;

namespace
{
// Pack a floating-point value into a signed integer with saturation.
int16_t packSigned10(float v)
{
    if (!isfinite(v))
        return 0;
    double scaled = (double)v * 10.0;
    if (scaled > 32767.0)
        return 32767;
    if (scaled < -32768.0)
        return -32768;
    return (int16_t)(scaled + (scaled >= 0.0 ? 0.5 : -0.5));
}

// Pack a floating-point value into an unsigned integer with saturation.
uint16_t packUnsigned10(float v)
{
    if (!isfinite(v) || v <= 0.0f)
        return 0;
    double scaled = (double)v * 10.0;
    if (scaled > 65535.0)
        return 65535;
    return (uint16_t)(scaled + 0.5);
}

// Expand a compact sample temperature to degrees C.
float sampleTemp(const EnvHistory::Sample &s)
{
    return (float)s.temp10 / 10.0f;
}

// Expand a compact sample humidity to relative humidity percent.
float sampleHumidity(const EnvHistory::Sample &s)
{
    return (float)s.humid10 / 10.0f;
}
}

// Allocate the zeroed environment ring in PSRAM.
bool EnvHistory::begin()
{
    if (available())
        return true;
    if (!_mutex)
    {
        _mutex = xSemaphoreCreateMutex();
        if (!_mutex)
        {
            Serial.println("[hist] environment history mutex allocation failed");
            return false;
        }
    }
    _samples = (Sample *)heap_caps_calloc(SAMPLES, sizeof(Sample),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_samples)
    {
        Serial.printf("[hist] environment history allocation failed (%u bytes)\r\n",
                      (unsigned)(SAMPLES * sizeof(Sample)));
        return false;
    }
    Serial.printf("[hist] environment history ready: %u samples (%u bytes)\r\n",
                  (unsigned)SAMPLES, (unsigned)(SAMPLES * sizeof(Sample)));
    return true;
}

// Append one valid temperature/humidity reading, overwriting the oldest sample
// when the ring is full.
void EnvHistory::record(uint32_t ms, float tempC, float humidity)
{
    if (!available())
        return;
    if (!_mutex || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        return;
    _samples[_head] = {ms, packSigned10(tempC), packUnsigned10(humidity)};
    _head = (_head + 1) % SAMPLES;
    if (_count < SAMPLES)
        _count++;
    xSemaphoreGive(_mutex);
}

// Copy a sample by age-ordered index, oldest first.
bool EnvHistory::sampleAt(uint16_t index, Sample &sample) const
{
    if (!available() || index >= _count)
        return false;
    if (!_mutex || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        return false;
    if (index >= _count)
    {
        xSemaphoreGive(_mutex);
        return false;
    }
    uint16_t start = (_head + SAMPLES - _count) % SAMPLES;
    sample = _samples[(start + index) % SAMPLES];
    xSemaphoreGive(_mutex);
    return true;
}

// Append compact point arrays, filtering to the retained five-minute window.
void EnvHistory::appendPoints(Print &out, uint32_t now, uint32_t sinceMs, bool absoluteTime) const
{
    if (!available())
        return;

    uint32_t startMs = now - (uint32_t)SECONDS * 1000UL;
    bool comma = false;
    for (uint16_t i = 0; i < _count; i++)
    {
        Sample s;
        if (!sampleAt(i, s))
            continue;
        if ((int32_t)(s.ms - startMs) < 0 || (int32_t)(now - s.ms) < 0)
            continue;
        if (sinceMs && (int32_t)(s.ms - sinceMs) <= 0)
            continue;
        if (comma)
            out.print(',');
        comma = true;
        out.print('[');
        if (absoluteTime)
            out.print(s.ms);
        else
            out.print((int32_t)(s.ms - now));
        out.print(',');
        out.print(sampleTemp(s), 1);
        out.print(',');
        out.print(sampleHumidity(s), 1);
        out.print(']');
    }
}

// Write the environment history JSON body for the web UI.
void EnvHistory::writeJson(Print &out, uint32_t now, uint32_t sinceMs, bool absoluteTime) const
{
    out.print(F("{\"ok\":"));
    out.print(available() ? "true" : "false");
    out.print(F(",\"window_s\":"));
    out.print(SECONDS);
    out.print(F(",\"now_ms\":"));
    out.print(now);
    out.print(F(",\"time\":\""));
    out.print(absoluteTime ? "absolute" : "relative");
    out.print(F("\",\"points\":["));
    appendPoints(out, now, sinceMs, absoluteTime);
    out.print(F("]}"));
}

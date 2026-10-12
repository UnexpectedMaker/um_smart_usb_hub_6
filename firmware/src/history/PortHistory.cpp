// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// PortHistory.cpp — PSRAM-backed rolling measurements for the graph UI.
//
// Samples are kept in RAM only. The UI can request a full window, pan back
// through the retained PSRAM history, or request points newer than a previous
// sample timestamp while watching the live head.
#include "history/PortHistory.h"
#include <esp_heap_caps.h>
#include <math.h>

PortHistory g_history;

namespace
{
constexpr uint16_t INVALID_MV = 0xffff;
constexpr uint8_t TIER_COUNT = 3;
constexpr uint16_t TIER_HZ[TIER_COUNT] = {
    PortHistory::RAW_HZ, PortHistory::MID_HZ, PortHistory::LONG_HZ};
constexpr uint32_t TIER_SECONDS[TIER_COUNT] = {
    PortHistory::RAW_SECONDS, PortHistory::MID_SECONDS, PortHistory::LONG_SECONDS};
constexpr uint32_t TIER_SAMPLES[TIER_COUNT] = {
    PortHistory::RAW_SAMPLES, PortHistory::MID_SAMPLES, PortHistory::LONG_SAMPLES};
constexpr uint16_t MAX_HISTORY_POINTS = 3200;

// Return the history tier that best matches the requested display rate.
uint8_t tierForHz(uint16_t hz)
{
    if (hz >= 100)
        return 0;
    if (hz >= 10)
        return 1;
    return 2;
}

// Clamp display rates to the retained history tiers.
uint16_t normalizedHz(uint16_t hz)
{
    if (hz != 1 && hz != 10 && hz != 100)
        hz = 10;
    return hz;
}

// Clamp a requested history window to the chosen tier's retained span.
uint32_t normalizedSeconds(uint32_t seconds, uint8_t tier)
{
    if (seconds < 10)
        seconds = 10;
    if (seconds > TIER_SECONDS[tier])
        seconds = TIER_SECONDS[tier];
    return seconds;
}

// True when `a` is the same as or later than `b`, within the retained window.
bool timeAtOrAfter(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

// Return the later of two millis() timestamps within the retained window.
uint32_t laterMs(uint32_t a, uint32_t b)
{
    return timeAtOrAfter(a, b) ? a : b;
}

// Pack a floating-point value into an unsigned integer with saturation.
uint32_t packScaled(float v, float scale, uint32_t maxValue)
{
    if (!isfinite(v) || v <= 0.0f)
        return 0;
    double scaled = (double)v * (double)scale;
    if (scaled >= (double)maxValue)
        return maxValue;
    return (uint32_t)(scaled + 0.5);
}

// Convert one profiler reading into the compact PSRAM sample format.
PortHistory::Sample makeSample(uint32_t ms, bool ok, float volts, float ma)
{
    if (!ok)
        return {ms, INVALID_MV, 0};
    return {
        ms,
        (uint16_t)packScaled(volts, 1000.0f, INVALID_MV - 1),
        (uint16_t)packScaled(ma, 10.0f, 0xffff)};
}

// True when a compact sample represents a valid monitor reading.
bool sampleOk(const PortHistory::Sample &s)
{
    return s.mv != INVALID_MV;
}

// Expand a compact sample voltage to volts.
float sampleVolts(const PortHistory::Sample &s)
{
    return (float)s.mv / 1000.0f;
}

// Expand a compact sample current to milliamps.
float sampleMa(const PortHistory::Sample &s)
{
    return (float)s.ma10 / 10.0f;
}

// Expand a compact sample power to milliwatts.
float sampleMw(const PortHistory::Sample &s)
{
    return sampleVolts(s) * sampleMa(s);
}

}

// Allocate zeroed ring buffers per tier and per port in PSRAM.
bool PortHistory::begin()
{
    if (available())
        return true;

    if (!_mutex)
    {
        _mutex = xSemaphoreCreateMutex();
        if (!_mutex)
        {
            Serial.println("[hist] PSRAM history mutex allocation failed");
            return false;
        }
    }

    size_t total = 0;
    for (uint8_t tier = 0; tier < TIER_COUNT; tier++)
    {
        size_t samples = (size_t)HUB_NUM_PORTS * TIER_SAMPLES[tier];
        size_t bytes = samples * sizeof(Sample);
        _samples[tier] = (Sample *)heap_caps_calloc(samples, sizeof(Sample),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_samples[tier])
        {
            Serial.printf("[hist] PSRAM allocation failed for %u Hz tier (%u bytes)\r\n",
                          (unsigned)TIER_HZ[tier], (unsigned)bytes);
            for (uint8_t i = 0; i < tier; i++)
            {
                heap_caps_free(_samples[i]);
                _samples[i] = nullptr;
            }
            return false;
        }
        total += bytes;
    }

    Serial.printf("[hist] PSRAM history ready: %u ports, %u bytes/port, %u bytes total\r\n",
                  (unsigned)HUB_NUM_PORTS,
                  (unsigned)(SAMPLES_PER_PORT * sizeof(Sample)),
                  (unsigned)total);
    return true;
}

// Return true once all tiers have backing PSRAM.
bool PortHistory::available() const
{
    for (uint8_t tier = 0; tier < TIER_COUNT; tier++)
        if (!_samples[tier])
            return false;
    return true;
}

// Append one reading to the raw ring and its lower-rate history tiers.
void PortHistory::record(uint8_t port, uint32_t ms, bool ok, float volts, float ma)
{
    if (!available() || port >= HUB_NUM_PORTS)
        return;

    Sample sample = makeSample(ms, ok, volts, ma);
    append(TIER_RAW, port, sample);

    for (uint8_t tier = TIER_MID; tier < TIER_COUNT; tier++)
    {
        uint32_t interval = 1000UL / TIER_HZ[tier];
        if (!_lastTierMs[tier][port] || (uint32_t)(ms - _lastTierMs[tier][port]) >= interval)
        {
            _lastTierMs[tier][port] = ms;
            append(tier, port, sample);
        }
    }
}

// Append one sample to a tier ring, overwriting the oldest sample when full.
void PortHistory::append(uint8_t tier, uint8_t port, const Sample &sample)
{
    if (tier >= TIER_COUNT || port >= HUB_NUM_PORTS)
        return;
    if (!_mutex || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        return;
    uint32_t capacity = TIER_SAMPLES[tier];
    uint32_t pos = port * capacity + _head[tier][port];
    _samples[tier][pos] = sample;
    _head[tier][port] = (_head[tier][port] + 1) % capacity;
    if (_count[tier][port] < capacity)
        _count[tier][port]++;
    xSemaphoreGive(_mutex);
}

// Copy a sample by age-ordered index, oldest first.
bool PortHistory::sampleAt(uint8_t tier, uint8_t port, uint32_t index, Sample &sample) const
{
    if (tier >= TIER_COUNT || !_samples[tier] || port >= HUB_NUM_PORTS || index >= _count[tier][port])
        return false;
    if (!_mutex || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
        return false;
    if (index >= _count[tier][port])
    {
        xSemaphoreGive(_mutex);
        return false;
    }
    uint32_t capacity = TIER_SAMPLES[tier];
    uint32_t start = (_head[tier][port] + capacity - _count[tier][port]) % capacity;
    sample = _samples[tier][port * capacity + ((start + index) % capacity)];
    xSemaphoreGive(_mutex);
    return true;
}

// Return how many seconds of history are currently populated for one tier.
uint32_t PortHistory::availableSeconds(uint8_t tier, uint8_t port) const
{
    if (tier >= TIER_COUNT || port >= HUB_NUM_PORTS || _count[tier][port] < 2)
        return 0;
    Sample oldest;
    Sample newest;
    if (!sampleAt(tier, port, 0, oldest) ||
        !sampleAt(tier, port, _count[tier][port] - 1, newest))
        return 0;
    uint32_t seconds = (uint32_t)(newest.ms - oldest.ms) / 1000UL;
    return min<uint32_t>(seconds, TIER_SECONDS[tier]);
}

// Append compact point arrays for one port, decimating large visible windows so
// JSON responses stay small enough for the ESP32.
void PortHistory::appendPoints(Print &out, uint8_t tier, uint8_t port, uint32_t seconds,
                               uint32_t now, uint32_t sinceMs, bool absoluteTime,
                               uint32_t offsetSeconds) const
{
    if (!available() || tier >= TIER_COUNT || port >= HUB_NUM_PORTS)
        return;

    uint32_t offsetMs = offsetSeconds * 1000UL;
    uint32_t endMs = (offsetMs < now) ? (now - offsetMs) : 0;
    uint32_t windowMs = seconds * 1000UL;
    uint32_t startMs = (windowMs < endMs) ? (endMs - windowMs) : 0;
    uint32_t count = _count[tier][port];
    uint32_t searchSeconds = min<uint32_t>(TIER_SECONDS[tier], seconds + offsetSeconds);
    uint32_t searchSamples = min<uint32_t>(count, searchSeconds * TIER_HZ[tier] + 2);
    uint32_t first = count - searchSamples;
    uint32_t targetMs = sinceMs ? laterMs(startMs, sinceMs + 1) : startMs;
    uint32_t lo = first;
    uint32_t hi = count;
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        Sample s;
        if (sampleAt(tier, port, mid, s) && !timeAtOrAfter(s.ms, targetMs))
            lo = mid + 1;
        else
            hi = mid;
    }
    first = lo;

    uint32_t visibleSamples = min<uint32_t>(count, seconds * TIER_HZ[tier] + 2);
    uint32_t stride = max<uint32_t>(1, (visibleSamples + MAX_HISTORY_POINTS - 1) / MAX_HISTORY_POINTS);
    bool comma = false;
    uint16_t emitted = 0;
    for (uint32_t i = first; i < count; i += stride)
    {
        Sample s;
        if (!sampleAt(tier, port, i, s) || !sampleOk(s))
            continue;
        if ((int32_t)(s.ms - startMs) < 0 || (int32_t)(endMs - s.ms) < 0)
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
            out.print((int32_t)(s.ms - endMs));
        out.print(',');
        out.print(sampleVolts(s), 3);
        out.print(',');
        out.print(sampleMa(s), 1);
        out.print(',');
        out.print(sampleMw(s), 1);
        out.print(']');
        if ((++emitted & 0x7f) == 0)
            delay(0);
    }
}

// Write the history JSON body for one port.
void PortHistory::writeJson(Print &out, uint8_t port, uint16_t hz,
                            uint32_t seconds, uint32_t now,
                            uint32_t sinceMs, bool absoluteTime,
                            uint32_t offsetSeconds) const
{
    hz = normalizedHz(hz);
    uint8_t tier = tierForHz(hz);
    seconds = normalizedSeconds(seconds, tier);
    uint32_t availableSpan = availableSeconds(tier, port);
    uint32_t maxOffset = (availableSpan > seconds) ? (availableSpan - seconds) : 0;
    if (offsetSeconds > maxOffset)
        offsetSeconds = maxOffset;

    out.print(F("{\"ok\":"));
    out.print(available() ? "true" : "false");
    out.print(F(",\"port\":"));
    out.print((unsigned)(port + 1));
    out.print(F(",\"hz\":"));
    out.print(hz);
    out.print(F(",\"capture_hz\":"));
    out.print(SAMPLE_HZ);
    out.print(F(",\"stored_hz\":"));
    out.print(TIER_HZ[tier]);
    out.print(F(",\"window_s\":"));
    out.print(seconds);
    out.print(F(",\"max_window_s\":"));
    out.print(TIER_SECONDS[tier]);
    out.print(F(",\"available_s\":"));
    out.print(availableSpan);
    out.print(F(",\"tiers\":["));
    for (uint8_t i = 0; i < TIER_COUNT; i++)
    {
        if (i)
            out.print(',');
        out.print(F("{\"hz\":"));
        out.print(TIER_HZ[i]);
        out.print(F(",\"max_window_s\":"));
        out.print(TIER_SECONDS[i]);
        out.print(F(",\"available_s\":"));
        out.print(availableSeconds(i, port));
        out.print('}');
    }
    out.print(F("],\"offset_s\":"));
    out.print(offsetSeconds);
    out.print(F(",\"now_ms\":"));
    out.print(now);
    out.print(F(",\"time\":\""));
    out.print(absoluteTime ? "absolute" : "relative");
    out.print(F("\",\"points\":["));
    appendPoints(out, tier, port, seconds, now, sinceMs, absoluteTime, offsetSeconds);
    out.print(F("]}"));
}

// Write the history JSON body for every port.
void PortHistory::writeJsonAll(Print &out, uint16_t hz, uint32_t seconds,
                               uint32_t now, uint32_t sinceMs,
                               bool absoluteTime, uint32_t offsetSeconds) const
{
    hz = normalizedHz(hz);
    uint8_t tier = tierForHz(hz);
    seconds = normalizedSeconds(seconds, tier);

    out.print(F("{\"ok\":"));
    out.print(available() ? "true" : "false");
    out.print(F(",\"port\":\"all\""));
    out.print(F(",\"hz\":"));
    out.print(hz);
    out.print(F(",\"capture_hz\":"));
    out.print(SAMPLE_HZ);
    out.print(F(",\"stored_hz\":"));
    out.print(TIER_HZ[tier]);
    out.print(F(",\"window_s\":"));
    out.print(seconds);
    out.print(F(",\"max_window_s\":"));
    out.print(TIER_SECONDS[tier]);
    out.print(F(",\"offset_s\":"));
    out.print(offsetSeconds);
    out.print(F(",\"now_ms\":"));
    out.print(now);
    out.print(F(",\"time\":\""));
    out.print(absoluteTime ? "absolute" : "relative");
    out.print(F("\",\"ports\":["));
    for (uint8_t port = 0; port < HUB_NUM_PORTS; port++)
    {
        if (port)
            out.print(',');
        out.print(F("{\"port\":"));
        out.print((unsigned)(port + 1));
        out.print(F(",\"available_s\":"));
        out.print(availableSeconds(tier, port));
        out.print(F(",\"points\":["));
        appendPoints(out, tier, port, seconds, now, sinceMs, absoluteTime, offsetSeconds);
        out.print(F("]}"));
    }
    out.print(F("]}"));
}

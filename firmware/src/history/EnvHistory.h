// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// EnvHistory.h — rolling temperature/humidity history for the sidebar graph.
//
// The web page keeps a five-minute environment trend. This class keeps the same
// short window on the hub, so a browser reload can refill the graph from cached
// samples instead of starting empty.
#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>

class EnvHistory
{
public:
    static constexpr uint16_t SECONDS = 5 * 60;
    static constexpr uint16_t SAMPLE_MS = 1000;
    static constexpr uint16_t SAMPLES = SECONDS * 1000UL / SAMPLE_MS;

    // One compact captured environment reading at one `millis()` timestamp.
    struct __attribute__((packed)) Sample
    {
        uint32_t ms = 0;
        int16_t temp10 = 0;    // degrees C * 10
        uint16_t humid10 = 0;  // relative humidity percent * 10
    };

    // Allocate the backing ring. Returns false if allocation fails.
    bool begin();
    // True once the backing ring has been allocated.
    bool available() const { return _samples != nullptr; }
    // Append one valid temperature/humidity sample.
    void record(uint32_t ms, float tempC, float humidity);
    // Write JSON history for the web UI environment graph.
    void writeJson(Print &out, uint32_t now, uint32_t sinceMs = 0,
                   bool absoluteTime = false) const;

private:
    // Copy a sample by age-ordered index, oldest first.
    bool sampleAt(uint16_t index, Sample &sample) const;
    // Append compact [time,temp_c,humidity] point arrays to a JSON stream.
    void appendPoints(Print &out, uint32_t now, uint32_t sinceMs,
                      bool absoluteTime) const;

    Sample *_samples = nullptr;
    uint16_t _head = 0;
    uint16_t _count = 0;
    mutable SemaphoreHandle_t _mutex = nullptr;
};

extern EnvHistory g_envHistory;

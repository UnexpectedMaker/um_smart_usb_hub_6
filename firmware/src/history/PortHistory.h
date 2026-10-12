// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// PortHistory.h — PSRAM-backed rolling measurements for the graph UI.
//
// The graph page reads historical voltage/current from this class. It keeps
// three fixed-rate rings per port so live capture can stay at 100 Hz while
// longer views use compact 10 Hz and 1 Hz history tiers. Power is calculated
// from voltage and current when history JSON is built.
#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include "settings/HubSettings.h"

class PortHistory
{
public:
    static constexpr uint16_t SAMPLE_HZ = 100;
    static constexpr uint16_t SAMPLE_MS = 1000 / SAMPLE_HZ;
    static constexpr uint32_t BYTES_PER_PORT = 1024UL * 1024UL;

    static constexpr uint16_t RAW_HZ = 100;
    static constexpr uint16_t MID_HZ = 10;
    static constexpr uint16_t LONG_HZ = 1;

    static constexpr uint16_t RAW_SECONDS = 10 * 60;
    static constexpr uint16_t MID_SECONDS = 20 * 60;
    static constexpr uint32_t LONG_SECONDS = 16UL * 60UL * 60UL;

    static constexpr uint32_t RAW_SAMPLES = (uint32_t)RAW_HZ * RAW_SECONDS;
    static constexpr uint32_t MID_SAMPLES = (uint32_t)MID_HZ * MID_SECONDS;
    static constexpr uint32_t LONG_SAMPLES = (uint32_t)LONG_HZ * LONG_SECONDS;
    static constexpr uint32_t SAMPLES_PER_PORT = RAW_SAMPLES + MID_SAMPLES + LONG_SAMPLES;

    // One compact captured measurement at one `millis()` timestamp.
    struct __attribute__((packed)) Sample
    {
        uint32_t ms = 0;
        uint16_t mv = 0;   // volts * 1000; 0xffff means no valid reading
        uint16_t ma10 = 0; // milliamps * 10
    };
    static_assert(SAMPLES_PER_PORT * sizeof(Sample) <= BYTES_PER_PORT,
                  "PortHistory must stay within 1 MB per port");

    // Allocate the PSRAM rings. Returns false if PSRAM is unavailable.
    bool begin();
    // True once every backing ring has been allocated.
    bool available() const;
    // Append one 100 Hz sample for an internal 0..5 port index.
    void record(uint8_t port, uint32_t ms, bool ok, float volts, float ma);
    // Write JSON history for one internal 0..5 port index.
    void writeJson(Print &out, uint8_t port, uint16_t hz, uint32_t seconds,
                   uint32_t now, uint32_t sinceMs = 0,
                   bool absoluteTime = false, uint32_t offsetSeconds = 0) const;
    // Write JSON history for every port, used by the mini graphs.
    void writeJsonAll(Print &out, uint16_t hz, uint32_t seconds, uint32_t now,
                      uint32_t sinceMs = 0, bool absoluteTime = false,
                      uint32_t offsetSeconds = 0) const;

private:
    enum Tier : uint8_t
    {
        TIER_RAW = 0,
        TIER_MID = 1,
        TIER_LONG = 2,
        TIER_COUNT = 3
    };

    // Copy a sample by age-ordered index, oldest first, from one tier.
    bool sampleAt(uint8_t tier, uint8_t port, uint32_t index, Sample &sample) const;
    // Append one sample into one tier ring.
    void append(uint8_t tier, uint8_t port, const Sample &sample);
    // Seconds of actual data currently retained for one port and tier.
    uint32_t availableSeconds(uint8_t tier, uint8_t port) const;
    // Append compact [time,V,mA,mW] point arrays to an existing JSON stream.
    void appendPoints(Print &out, uint8_t tier, uint8_t port, uint32_t seconds,
                      uint32_t now, uint32_t sinceMs, bool absoluteTime,
                      uint32_t offsetSeconds) const;

    Sample *_samples[TIER_COUNT] = {};
    uint32_t _head[TIER_COUNT][HUB_NUM_PORTS] = {};
    uint32_t _count[TIER_COUNT][HUB_NUM_PORTS] = {};
    uint32_t _lastTierMs[TIER_COUNT][HUB_NUM_PORTS] = {};
    mutable SemaphoreHandle_t _mutex = nullptr;
};

extern PortHistory g_history;

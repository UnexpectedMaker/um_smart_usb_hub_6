// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// PortHistory.h — 100 Hz port history in PSRAM, 60 minutes per port.
//
// The profiler task is the only writer, and the only source of port readings.
// Every 10 ms it writes one slot for every port: the raw INA3221 bus and shunt
// values packed into 3 bytes, or an invalid slot when the port is off, its
// monitor is missing or the read failed.
//
// A slot's capture count is its 10 ms period since boot, so its time is
// count x 10 ms and no timestamp is stored. Periods the profiler missed are
// written as invalid slots, so position always matches time.
//
// Each port also keeps a list of events (power off/on, device removed/
// inserted), each marked with the capture count it happened at.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include "settings/HubSettings.h"

class PortHistory
{
public:
    static constexpr uint16_t SAMPLE_HZ = 100;
    static constexpr uint16_t SAMPLE_MS = 1000 / SAMPLE_HZ;
    static constexpr uint32_t SECONDS = 60UL * 60UL;
    static constexpr uint32_t SAMPLES = (uint32_t)SAMPLE_HZ * SECONDS; // per port
    static constexpr uint8_t SAMPLE_BYTES = 3;
    static constexpr uint16_t MAX_EVENTS = 256; // per port; the oldest is overwritten

    enum Event : uint8_t
    {
        EVENT_POWER_OFF = 1,
        EVENT_POWER_ON = 2,
        EVENT_DEVICE_REMOVED = 3,
        EVENT_DEVICE_INSERTED = 4,
    };

    // One reading, unpacked: the INA3221 bus and shunt voltage registers as the
    // chip returns them. valid = false for an invalid slot (bus and shunt 0).
    struct Reading
    {
        bool valid;
        int16_t bus;
        int16_t shunt;
    };

    // One page of history for the history request: which readings it covers,
    // plus the header and the events in its range, already in the binary
    // layout the page is sent in (all values little-endian):
    //   header   first capture count   4 bytes
    //            number of events      2 bytes
    //            number of readings    4 bytes
    //   events   capture count + code  5 bytes each, oldest first
    //   readings                       3 bytes each, as stored, oldest first
    // The readings are copied out of PSRAM while the page is sent (readRaw()).
    static constexpr uint32_t PAGE_SAMPLES = 10UL * 60UL * SAMPLE_HZ; // 10 minutes
    static constexpr size_t PAGE_HEADER_BYTES = 10;
    static constexpr size_t EVENT_BYTES = 5;
    struct Page
    {
        uint32_t first;    // capture count of the first reading
        uint32_t readings; // how many readings
        size_t headBytes;  // header + events
        uint8_t head[PAGE_HEADER_BYTES + MAX_EVENTS * EVENT_BYTES];
    };
    // Which readings a page covers, up to `most` of them:
    enum PageFrom : uint8_t
    {
        PAGE_NEWEST, // the newest readings
        PAGE_BEFORE, // the readings just before capture count `count`
        PAGE_AFTER,  // the readings after capture count `count`. If `count` is
                     // newer than anything held (the hub restarted), the newest.
    };

    // Allocate the PSRAM history. Returns false if PSRAM is unavailable.
    bool begin();
    // True once the PSRAM history has been allocated.
    bool available() const { return _samples != nullptr; }

    // ---- Writer: the profiler task only ----
    // Write capture `count` for every port. Periods between the newest written
    // count and this one are written as invalid. A count that isn't newer than
    // the newest written is ignored.
    void write(uint32_t count, const Reading readings[HUB_NUM_PORTS]);
    // Add an event to a port's list at capture `count`.
    void addEvent(uint8_t port, uint32_t count, Event code);

    // ---- Readers: any task ----
    // Up to `max` of a port's newest slots, newest first, valid or not. Returns
    // how many were copied (0 before anything is written).
    uint8_t latest(uint8_t port, uint8_t max, Reading *out) const;
    // Plan one page of a port's history: its range, header and events.
    // False if there's no history or the port is out of range. A page with
    // nothing to send has 0 readings.
    bool page(uint8_t port, PageFrom from, uint32_t count, uint32_t most, Page &out) const;
    // Copy `n` readings of a port, from capture count `first`, as stored (3
    // bytes each). A reading that's no longer held, or was overwritten while
    // being copied, is copied as an invalid reading.
    void readRaw(uint8_t port, uint32_t first, uint32_t n, uint8_t *out) const;

    // NOT ALLOWED: writeJsonAll() (every port's history in one response) was
    // removed from /api/history and must not be put back. History is only ever
    // served for one port at a time.

private:
    // The published state, copied together under _mux.
    struct State
    {
        bool hasData;      // anything written yet
        uint32_t newest;   // newest written capture count
        uint32_t newestAt; // its slot index
        uint32_t filled;   // slots holding data, up to SAMPLES
        uint32_t writeTo;  // highest count being written; slots at or below
                           // writeTo - SAMPLES are being overwritten
    };
    struct __attribute__((packed)) EventRec
    {
        uint32_t count;
        uint8_t code;
    };

    State state() const;
    // Copy capture `count` of one port. False when it's outside the history or
    // was overwritten while being copied.
    bool readAt(uint8_t port, uint32_t count, Reading &out) const;
    void fillInvalid(uint32_t from, uint32_t n);

    uint8_t *_samples = nullptr; // [port][SAMPLES][3]
    EventRec *_events = nullptr; // [port][MAX_EVENTS]
    uint16_t _eventHead[HUB_NUM_PORTS] = {};
    uint16_t _eventCount[HUB_NUM_PORTS] = {};
    State _state = {};
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

extern PortHistory g_history;

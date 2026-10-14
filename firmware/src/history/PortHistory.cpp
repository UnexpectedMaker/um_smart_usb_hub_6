// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// PortHistory.cpp — 100 Hz port history in PSRAM, 60 minutes per port.
//
// Samples are kept in RAM only. One writer (the profiler task) and any number
// of readers. The writer announces the highest count it is about to write
// (writeTo) before touching the slots, and publishes the newest count after.
// A reader only uses a slot that was published and isn't being overwritten,
// checked again after copying it, so it never returns a half-written or
// overwritten reading.
#include "history/PortHistory.h"
#include <esp_heap_caps.h>

PortHistory g_history;

namespace
{
void put16(uint8_t *p, uint16_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}

void put32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

// 24 bits: bit 23 valid, bits 22-13 bus (10 bits, 8 mV steps, 0-8.184 V),
// bits 12-0 shunt (13 bits signed, 40 uV steps). The registers hold their
// value in bits 15-3.
void pack(uint8_t *p, const PortHistory::Reading &r)
{
    if (!r.valid)
    {
        p[0] = p[1] = p[2] = 0;
        return;
    }
    int32_t bus = r.bus >> 3;
    if (bus < 0)
        bus = 0;
    if (bus > 1023)
        bus = 1023;
    uint32_t v = (1UL << 23) | ((uint32_t)bus << 13) | ((uint32_t)(r.shunt >> 3) & 0x1fff);
    p[0] = v >> 16;
    p[1] = v >> 8;
    p[2] = v;
}

PortHistory::Reading unpack(const uint8_t *p)
{
    uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    if (!(v & (1UL << 23)))
        return {false, 0, 0};
    int32_t shunt = v & 0x1fff;
    if (shunt & 0x1000)
        shunt -= 0x2000;
    return {true, (int16_t)(((v >> 13) & 0x3ff) << 3), (int16_t)(shunt * 8)};
}
}

// Allocate the zeroed (all invalid) history and event lists in PSRAM.
bool PortHistory::begin()
{
    if (available())
        return true;

    size_t bytes = (size_t)HUB_NUM_PORTS * SAMPLES * SAMPLE_BYTES;
    _samples = (uint8_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    _events = (EventRec *)heap_caps_calloc((size_t)HUB_NUM_PORTS * MAX_EVENTS, sizeof(EventRec),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_samples || !_events)
    {
        Serial.printf("[hist] PSRAM history allocation failed (%u bytes)\r\n", (unsigned)bytes);
        heap_caps_free(_samples);
        heap_caps_free(_events);
        _samples = nullptr;
        _events = nullptr;
        return false;
    }
    Serial.printf("[hist] PSRAM history ready: %u ports, %u s at %u Hz, %u bytes\r\n",
                  (unsigned)HUB_NUM_PORTS, (unsigned)SECONDS, (unsigned)SAMPLE_HZ, (unsigned)bytes);
    return true;
}

PortHistory::State PortHistory::state() const
{
    portENTER_CRITICAL(&_mux);
    State s = _state;
    portEXIT_CRITICAL(&_mux);
    return s;
}

// Write n invalid slots for every port, starting at slot index `from`.
void PortHistory::fillInvalid(uint32_t from, uint32_t n)
{
    uint32_t first = min(n, SAMPLES - from);
    for (uint8_t port = 0; port < HUB_NUM_PORTS; port++)
    {
        uint8_t *base = _samples + (size_t)port * SAMPLES * SAMPLE_BYTES;
        memset(base + (size_t)from * SAMPLE_BYTES, 0, (size_t)first * SAMPLE_BYTES);
        if (n > first)
            memset(base, 0, (size_t)(n - first) * SAMPLE_BYTES);
    }
}

void PortHistory::write(uint32_t count, const Reading readings[HUB_NUM_PORTS])
{
    if (!available())
        return;
    State s = state();
    uint32_t blank = 0;
    uint32_t at = SAMPLES - 1; // the first slot ever written goes at index 0
    if (s.hasData)
    {
        uint32_t gap = count - s.newest;
        if ((int32_t)gap <= 0)
            return;
        blank = min(gap - 1, SAMPLES);
        at = s.newestAt;
    }

    portENTER_CRITICAL(&_mux);
    _state.writeTo = count;
    portEXIT_CRITICAL(&_mux);

    if (blank)
        fillInvalid((at + 1) % SAMPLES, blank);
    at = (at + blank + 1) % SAMPLES;
    for (uint8_t port = 0; port < HUB_NUM_PORTS; port++)
        pack(_samples + ((size_t)port * SAMPLES + at) * SAMPLE_BYTES, readings[port]);

    portENTER_CRITICAL(&_mux);
    _state.filled = s.hasData ? min(s.filled + blank + 1, SAMPLES) : 1;
    _state.newest = count;
    _state.newestAt = at;
    _state.hasData = true;
    portEXIT_CRITICAL(&_mux);
}

void PortHistory::addEvent(uint8_t port, uint32_t count, Event code)
{
    if (!available() || port >= HUB_NUM_PORTS)
        return;
    portENTER_CRITICAL(&_mux);
    _events[port * MAX_EVENTS + _eventHead[port]] = {count, (uint8_t)code};
    _eventHead[port] = (_eventHead[port] + 1) % MAX_EVENTS;
    if (_eventCount[port] < MAX_EVENTS)
        _eventCount[port]++;
    portEXIT_CRITICAL(&_mux);
}

bool PortHistory::readAt(uint8_t port, uint32_t count, Reading &out) const
{
    State s = state();
    if (!s.hasData)
        return false;
    uint32_t age = s.newest - count;
    if ((int32_t)age < 0 || age >= s.filled || s.writeTo - count >= SAMPLES)
        return false;
    uint32_t at = (s.newestAt + SAMPLES - age) % SAMPLES;
    out = unpack(_samples + ((size_t)port * SAMPLES + at) * SAMPLE_BYTES);
    // Overwritten while it was being copied?
    portENTER_CRITICAL(&_mux);
    uint32_t writeTo = _state.writeTo;
    portEXIT_CRITICAL(&_mux);
    return writeTo - count < SAMPLES;
}

uint8_t PortHistory::latest(uint8_t port, uint8_t max, Reading *out) const
{
    if (!available() || port >= HUB_NUM_PORTS)
        return 0;
    State s = state();
    uint8_t n = 0;
    while (n < max && readAt(port, s.newest - n, out[n]))
        n++;
    return n;
}

// Plan one page: pick its readings, then build the header and copy in the
// port's events that fall within them.
bool PortHistory::page(uint8_t port, PageFrom from, uint32_t count, uint32_t most, Page &out) const
{
    if (!available() || port >= HUB_NUM_PORTS)
        return false;
    if (most < 1 || most > PAGE_SAMPLES)
        most = PAGE_SAMPLES;
    State s = state();
    uint32_t first = 0, n = 0;
    if (s.hasData)
    {
        // The oldest reading held that isn't about to be overwritten.
        uint32_t oldest = s.newest - s.filled + 1;
        uint32_t safe = s.writeTo - SAMPLES + 1;
        if ((int32_t)(safe - oldest) > 0)
            oldest = safe;

        if (from == PAGE_AFTER && (int32_t)(count - s.newest) < 0)
        {
            // Oldest first, from just after `count`.
            first = count + 1;
            if ((int32_t)(oldest - first) > 0)
                first = oldest;
            n = min<uint32_t>(s.newest - first + 1, most);
        }
        else if (!(from == PAGE_AFTER && count == s.newest))
        {
            // The `most` readings ending at the newest, or just before `count`.
            uint32_t last = s.newest;
            if (from == PAGE_BEFORE && (int32_t)(count - 1 - s.newest) < 0)
                last = count - 1;
            if ((int32_t)(last - oldest) >= 0)
            {
                n = min<uint32_t>(last - oldest + 1, most);
                first = last - n + 1;
            }
        }
    }

    uint16_t events = 0;
    if (n)
    {
        uint32_t last = first + n - 1;
        uint8_t *p = out.head + PAGE_HEADER_BYTES;
        portENTER_CRITICAL(&_mux);
        uint16_t total = _eventCount[port];
        uint16_t start = (_eventHead[port] + MAX_EVENTS - total) % MAX_EVENTS;
        for (uint16_t k = 0; k < total; k++)
        {
            const EventRec &e = _events[port * MAX_EVENTS + (start + k) % MAX_EVENTS];
            if ((int32_t)(e.count - first) < 0 || (int32_t)(last - e.count) < 0)
                continue;
            put32(p, e.count);
            p[4] = e.code;
            p += EVENT_BYTES;
            events++;
        }
        portEXIT_CRITICAL(&_mux);
    }

    out.first = first;
    out.readings = n;
    out.headBytes = PAGE_HEADER_BYTES + (size_t)events * EVENT_BYTES;
    put32(out.head, first);
    put16(out.head + 4, events);
    put32(out.head + 6, n);
    return true;
}

void PortHistory::readRaw(uint8_t port, uint32_t first, uint32_t n, uint8_t *out) const
{
    memset(out, 0, (size_t)n * SAMPLE_BYTES);
    if (!available() || port >= HUB_NUM_PORTS || !n)
        return;
    State s = state();
    if (!s.hasData)
        return;

    // The part of first..first + n - 1 that's held and not being overwritten.
    uint32_t lo = first;
    uint32_t oldest = s.newest - s.filled + 1;
    if ((int32_t)(oldest - lo) > 0)
        lo = oldest;
    uint32_t safe = s.writeTo - SAMPLES + 1;
    if ((int32_t)(safe - lo) > 0)
        lo = safe;
    uint32_t hi = first + n - 1;
    if ((int32_t)(hi - s.newest) > 0)
        hi = s.newest;
    if ((int32_t)(hi - lo) < 0)
        return;

    // Copy it, in two parts if it wraps round the end of the ring.
    uint32_t m = hi - lo + 1;
    uint32_t at = (s.newestAt + SAMPLES - (s.newest - lo)) % SAMPLES;
    const uint8_t *base = _samples + (size_t)port * SAMPLES * SAMPLE_BYTES;
    uint8_t *dst = out + (size_t)(lo - first) * SAMPLE_BYTES;
    uint32_t part = min(m, SAMPLES - at);
    memcpy(dst, base + (size_t)at * SAMPLE_BYTES, (size_t)part * SAMPLE_BYTES);
    if (m > part)
        memcpy(dst + (size_t)part * SAMPLE_BYTES, base, (size_t)(m - part) * SAMPLE_BYTES);

    // Readings overwritten while they were being copied go out invalid. They're
    // always the oldest ones.
    portENTER_CRITICAL(&_mux);
    uint32_t writeTo = _state.writeTo;
    portEXIT_CRITICAL(&_mux);
    uint32_t limit = writeTo - SAMPLES + 1;
    if ((int32_t)(limit - lo) > 0)
        memset(dst, 0, (size_t)min(limit - lo, m) * SAMPLE_BYTES);
}

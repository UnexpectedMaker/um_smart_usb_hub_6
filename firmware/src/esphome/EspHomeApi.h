// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// EspHomeApi.h — Home Assistant support through the ESPHome native API
// (plaintext, port 6053). Each port is a power switch, a device-connected
// sensor and a current sensor.
//
// Like HubWebServer, the hardware is reached only through callbacks from
// main.cpp. Everything runs in loop(), so no extra locking is needed.
#pragma once
#include <Arduino.h>
#include <functional>

class EspHomeApi
{
public:
    // Ports are indexed 0-5.
    struct Hooks
    {
        std::function<bool()> enabled; // the API runs only while this is true
        std::function<bool(int)> portOn;
        std::function<bool(int)> portConnected;
        std::function<String(int)> portName; // "" if unnamed
        std::function<void(int, bool)> setPort;
        std::function<bool(int, float &)> portAmps; // false if the port has no monitor
    };

    // Call once mDNS is up; later calls do nothing. The API starts and stops
    // in loop() to follow the enabled hook.
    void begin(const Hooks &hooks, const String &name, const String &friendlyName, const char *version);

    // Start or stop to match the enabled hook, serve clients and push state
    // changes.
    void loop();

private:
    bool _started = false;
};

extern EspHomeApi espHomeApi;

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// HubWebServer.h — the hub's HTTP server: the embedded web UI plus the JSON
// API, on port 80.
//
// This layer only routes requests. Everything that reads or changes the
// hardware is supplied by main.ino as callbacks, so the server never touches
// the port or sensor globals directly.
#pragma once
#include <Arduino.h>
#include <functional>

class HubWebServer
{
public:
    using StatusFn = std::function<String()>;              // GET /api/status body
    using ThemeFn = std::function<void(const String &)>;   // store "dark" | "light"
    using PortFn = std::function<void(int, bool)>;         // switch port n (1..6)
    using SchedGetFn = std::function<String()>;            // GET /api/schedules body
    using SchedSetFn = std::function<void(const String &)>; // store schedules from a JSON body
    using SaveFn = std::function<void()>;                  // save all settings to flash

    // Suppliers for the REST API. Each returns a complete JSON body.
    struct Rest
    {
        std::function<String()> hub;                // GET /api/hub
        std::function<String()> ports;              // GET /api/ports
        std::function<String(int)> port;            // GET /api/ports/{n}
        std::function<String(int)> portSchedule;    // GET /api/ports/{n}/schedule
        std::function<void(bool)> setAllPorts;      // POST /api/hub/ports?on=
    };

    // Provide the REST suppliers. Must be called before begin().
    void setRest(const Rest &rest) { _rest = rest; }

    // Register every route and start listening. Call once the network is up;
    // later calls do nothing.
    void begin(StatusFn statusFn, ThemeFn themeFn, PortFn portFn,
               SchedGetFn schedGetFn, SchedSetFn schedSetFn, SaveFn saveFn);

private:
    void registerRest();
    Rest _rest;
    bool _started = false;
};

extern HubWebServer webServer;

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
// Like HubWebServer, the hardware is reached only through hooks from
// main.cpp. Everything runs in loop() and never waits on the network: sockets
// are non-blocking, and a client that can't take a whole frame is dropped.
// The frame and receive buffers are allocated once, in PSRAM; nothing is
// allocated per message.
#pragma once
#include <Arduino.h>
#include "settings/HubSettings.h"

class EspHomeApi
{
public:
    // Ports are indexed 0-5.
    struct Hooks
    {
        bool (*enabled)();                     // the API runs only while this is true
        const char *(*deviceName)();           // shown in Home Assistant
        bool (*portOn)(uint8_t port);
        bool (*portConnected)(uint8_t port);
        const char *(*portName)(uint8_t port); // "" if unnamed
        void (*setPort)(uint8_t port, bool on);
        bool (*portAmps)(uint8_t port, float &amps); // false if the port has no monitor
    };

    // Allocate the buffers. Call once mDNS is up; later calls do nothing.
    // hostname is copied, as the hub's own name only changes on a restart;
    // version must stay valid. The API starts and stops in loop() to follow
    // the enabled hook.
    void begin(const Hooks &hooks, const char *hostname, const char *version);

    // Start or stop to match the enabled hook, serve clients and push changes.
    void loop();

private:
    static constexpr uint16_t PORT = 6053;
    static constexpr uint8_t MAX_CLIENTS = 2;
    // The largest message is the device info with a 120-byte friendly name:
    // about 285 bytes, plus the header.
    static constexpr size_t FRAME_BYTES = 320;
    static constexpr size_t HEADER_BYTES = 11; // 0x00 + two 5-byte varints
    static constexpr size_t MESSAGE_BYTES = FRAME_BYTES - HEADER_BYTES;
    static constexpr size_t RX_BYTES = 640;    // per client
    static constexpr uint32_t AMPS_MS = 5000;  // how often current is checked
    static constexpr uint32_t NAMES_MS = 1000; // how often the names are checked
    // A client that has sent nothing for PING_MS is pinged, and dropped after
    // IDLE_MS. Home Assistant only pings when it hasn't heard from the hub, so
    // the hub has to ask.
    static constexpr uint32_t PING_MS = 20000;
    static constexpr uint32_t IDLE_MS = 60000;
    static constexpr uint32_t RETRY_MS = 5000; // between attempts to listen

    struct Client
    {
        int fd = -1;
        uint8_t *rx = nullptr; // RX_BYTES, in PSRAM
        size_t rxLen = 0;
        bool subscribed = false;
        bool pinged = false; // a ping is out since the last message received
        uint32_t lastRxMs = 0;
    };

    // Listen (once) and advertise over mDNS. If the socket can't listen, stay
    // stopped and try again RETRY_MS later.
    void start();
    // Drop every client and withdraw the mDNS service.
    void stop();
    // Close a client's socket and free its slot.
    void drop(Client &c);
    // Take a waiting connection, if any. Refused while stopped or full.
    void acceptClient();
    // Read what a client sent and handle each complete message. Ping a quiet
    // client; drop a silent one.
    void receive(Client &c);
    // Answer one message.
    void handle(Client &c, uint32_t type, const uint8_t *p, const uint8_t *end);
    // Send switch, connected and current changes to subscribed clients. After
    // a rename, update mDNS and drop the clients so they reconnect and read
    // the new names.
    void pushChanges();

    // Where a message is built: after room for its frame header.
    uint8_t *message() { return _frame + HEADER_BYTES; }
    // Write the header in front of the len-byte message at message() and send
    // the frame. Drops the client if it didn't fit or the socket can't take it.
    void transmit(Client &c, uint32_t type, size_t len);
    // Name, MAC, versions and the friendly name.
    void sendDeviceInfo(Client &c);
    // Each port's switch, connected sensor and current sensor, then the end
    // of the list.
    void sendEntities(Client &c);
    // Port power state.
    void sendSwitch(Client &c, uint8_t port, bool on);
    // Whether a device is plugged in.
    void sendConnected(Client &c, uint8_t port, bool connected);
    // Port current in amps; marked missing when the port has no monitor.
    void sendAmps(Client &c, uint8_t port, bool ok, float amps);

    Hooks _hooks = {};
    char _hostname[33] = "";
    const char *_version = "";
    bool _started = false;
    bool _running = false;
    int _listenFd = -1;
    uint8_t *_frame = nullptr; // FRAME_BYTES, in PSRAM
    Client _clients[MAX_CLIENTS];

    bool _lastOn[HUB_NUM_PORTS] = {};
    bool _lastConnected[HUB_NUM_PORTS] = {};
    float _lastAmps[HUB_NUM_PORTS] = {};
    uint32_t _portNameHash[HUB_NUM_PORTS] = {};
    uint32_t _deviceNameHash = 0;
    uint32_t _ampsAt = 0;
    uint32_t _namesAt = 0;
    uint32_t _retryAt = 0; // millis() of a failed listen, 0 if none
};

extern EspHomeApi g_espHomeApi;

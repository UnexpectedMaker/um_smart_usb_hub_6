// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// EspHomeApi.cpp — the minimal part of the ESPHome native API that Home
// Assistant needs: handshake, device info, entity list, states, ping and
// switch commands. Message ids and fields follow esphome/components/api/api.proto.
#include "EspHomeApi.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <mdns.h>
#include <vector>

#define API_PORT 6053
#define API_CLIENTS 2
#define API_NUM_PORTS 6
#define API_RX_MAX 512
#define API_AMPS_MS 5000
#define API_NAMES_MS 1000
#define API_ESPHOME_VERSION "2025.10.0" // the ESPHome release this API matches

EspHomeApi espHomeApi;

enum MsgType : uint32_t
{
    HELLO_REQUEST = 1,
    HELLO_RESPONSE = 2,
    CONNECT_REQUEST = 3,
    CONNECT_RESPONSE = 4,
    DISCONNECT_REQUEST = 5,
    DISCONNECT_RESPONSE = 6,
    PING_REQUEST = 7,
    PING_RESPONSE = 8,
    DEVICE_INFO_REQUEST = 9,
    DEVICE_INFO_RESPONSE = 10,
    LIST_ENTITIES_REQUEST = 11,
    LIST_BINARY_SENSOR_RESPONSE = 12,
    LIST_SENSOR_RESPONSE = 16,
    LIST_SWITCH_RESPONSE = 17,
    LIST_DONE_RESPONSE = 19,
    SUBSCRIBE_STATES_REQUEST = 20,
    BINARY_SENSOR_STATE_RESPONSE = 21,
    SENSOR_STATE_RESPONSE = 25,
    SWITCH_STATE_RESPONSE = 26,
    SWITCH_COMMAND_REQUEST = 33,
};

// Entity keys for port i (0-based).
static uint32_t switchKey(int i) { return i + 1; }
static uint32_t ampsKey(int i) { return 101 + i; }
static uint32_t connectedKey(int i) { return 201 + i; }

// Protobuf message builder.
struct Pb
{
    std::vector<uint8_t> buf;

    void varint(uint32_t v)
    {
        while (v >= 0x80)
        {
            buf.push_back(uint8_t(v) | 0x80);
            v >>= 7;
        }
        buf.push_back(uint8_t(v));
    }
    void u32(uint8_t field, uint32_t v)
    {
        if (!v)
            return;
        varint(field << 3);
        varint(v);
    }
    void str(uint8_t field, const String &s)
    {
        varint(field << 3 | 2);
        varint(s.length());
        buf.insert(buf.end(), s.c_str(), s.c_str() + s.length());
    }
    void fixed32(uint8_t field, uint32_t v)
    {
        varint(field << 3 | 5);
        for (int b = 0; b < 4; b++)
            buf.push_back(uint8_t(v >> (8 * b)));
    }
    void f32(uint8_t field, float f)
    {
        uint32_t v;
        memcpy(&v, &f, 4);
        fixed32(field, v);
    }
};

struct Conn
{
    WiFiClient client;
    std::vector<uint8_t> rx;
    bool subscribed = false;
};

static EspHomeApi::Hooks hooks;
static String devName, devFriendlyName, fwVersion;
static WiFiServer server(API_PORT);
static Conn conns[API_CLIENTS];
static bool lastOn[API_NUM_PORTS];
static bool lastConnected[API_NUM_PORTS];
static float lastAmps[API_NUM_PORTS];
static String lastNames[API_NUM_PORTS];
static uint32_t lastAmpsMs = 0;
static uint32_t lastNamesMs = 0;
static bool running = false;

// Decode a varint; false if it runs past end.
static bool readVarint(const uint8_t *&p, const uint8_t *end, uint32_t &v)
{
    v = 0;
    for (int shift = 0; p < end && shift < 35; shift += 7)
    {
        uint8_t b = *p++;
        v |= uint32_t(b & 0x7F) << shift;
        if (!(b & 0x80))
            return true;
    }
    return false;
}

// Frame and send one message: 0x00, payload length, type, payload.
static void send(Conn &c, uint32_t type, const Pb &msg = Pb())
{
    Pb frame;
    frame.buf.push_back(0);
    frame.varint(msg.buf.size());
    frame.varint(type);
    frame.buf.insert(frame.buf.end(), msg.buf.begin(), msg.buf.end());
    c.client.write(frame.buf.data(), frame.buf.size());
}

// Send port i's power state.
static void sendSwitch(Conn &c, int i, bool on)
{
    Pb m;
    m.fixed32(1, switchKey(i));
    m.u32(2, on);
    send(c, SWITCH_STATE_RESPONSE, m);
}

// Send whether a device is plugged into port i.
static void sendConnected(Conn &c, int i, bool connected)
{
    Pb m;
    m.fixed32(1, connectedKey(i));
    m.u32(2, connected);
    send(c, BINARY_SENSOR_STATE_RESPONSE, m);
}

// Send port i's current; missing when the port has no monitor.
static void sendAmps(Conn &c, int i, bool ok, float amps)
{
    Pb m;
    m.fixed32(1, ampsKey(i));
    m.f32(2, amps);
    m.u32(3, !ok);
    send(c, SENSOR_STATE_RESPONSE, m);
}

// Pull key and state out of a SwitchCommandRequest.
static void parseSwitchCommand(const uint8_t *p, const uint8_t *end, uint32_t &key, bool &state)
{
    while (p < end)
    {
        uint32_t tag, v;
        if (!readVarint(p, end, tag))
            return;
        switch (tag & 7)
        {
        case 0:
            if (!readVarint(p, end, v))
                return;
            if (tag >> 3 == 2)
                state = v;
            break;
        case 5:
            if (end - p < 4)
                return;
            v = p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24;
            p += 4;
            if (tag >> 3 == 1)
                key = v;
            break;
        default:
            return;
        }
    }
}

// Port i's name, or "Port n" if it has none.
static String portLabel(int i)
{
    return lastNames[i].length() ? lastNames[i] : String("Port ") + String(i + 1);
}

// Round to the precision Home Assistant shows, so noise doesn't count as a change.
static float roundAmps(float a) { return roundf(a * 1000.0f) / 1000.0f; }

void EspHomeApi::begin(const Hooks &h, const String &name, const String &friendlyName, const char *version)
{
    if (_started)
        return;
    _started = true;
    hooks = h;
    devName = name;
    devFriendlyName = friendlyName;
    fwVersion = version;
}

// Listen and advertise over mDNS. The socket is opened once and never closed:
// the core can't re-bind the port straight after closing it.
static void start()
{
    running = true;
    for (int i = 0; i < API_NUM_PORTS; i++)
    {
        lastOn[i] = hooks.portOn(i);
        lastConnected[i] = hooks.portConnected(i);
        lastNames[i] = hooks.portName(i);
    }

    if (!server)
    {
        server.begin();
        server.setNoDelay(true);
    }

    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();
    MDNS.addService("esphomelib", "tcp", API_PORT);
    MDNS.addServiceTxt("esphomelib", "tcp", "mac", mac.c_str());
    MDNS.addServiceTxt("esphomelib", "tcp", "version", API_ESPHOME_VERSION);
    MDNS.addServiceTxt("esphomelib", "tcp", "friendly_name", devFriendlyName.c_str());
    MDNS.addServiceTxt("esphomelib", "tcp", "platform", "ESP32");
    MDNS.addServiceTxt("esphomelib", "tcp", "network", "wifi");
    Serial.printf("[esphome] API on port %u\r\n", API_PORT);
}

// Drop every client and withdraw the mDNS service.
static void stop()
{
    running = false;
    for (auto &c : conns)
        c.client.stop();
    mdns_service_remove("_esphomelib", "_tcp");
    Serial.println("[esphome] API off");
}

// Answer one message from a client.
static void handle(Conn &c, uint32_t type, const uint8_t *p, const uint8_t *end)
{
    switch (type)
    {
    case HELLO_REQUEST:
    {
        Pb m;
        m.u32(1, 1);
        m.u32(2, 10);
        m.str(3, String("Smart USB Hub ") + fwVersion);
        m.str(4, devName);
        send(c, HELLO_RESPONSE, m);
        break;
    }
    case CONNECT_REQUEST:
        send(c, CONNECT_RESPONSE);
        break;
    case DISCONNECT_REQUEST:
        send(c, DISCONNECT_RESPONSE);
        c.client.stop();
        break;
    case PING_REQUEST:
        send(c, PING_RESPONSE);
        break;
    case DEVICE_INFO_REQUEST:
    {
        Pb m;
        m.str(2, devName);
        m.str(3, WiFi.macAddress());
        m.str(4, API_ESPHOME_VERSION);
        m.str(5, __DATE__ ", " __TIME__);
        m.str(6, "Smart USB Hub");
        m.str(8, "unexpectedmaker.smart-usb-hub");
        m.str(9, fwVersion);
        m.str(12, "Unexpected Maker");
        m.str(13, devFriendlyName);
        send(c, DEVICE_INFO_RESPONSE, m);
        break;
    }
    case LIST_ENTITIES_REQUEST:
        for (int i = 0; i < API_NUM_PORTS; i++)
        {
            String n(i + 1);
            String label = portLabel(i);
            Pb sw;
            sw.str(1, String("port_") + n);
            sw.fixed32(2, switchKey(i));
            sw.str(3, label);
            sw.str(9, "outlet");
            send(c, LIST_SWITCH_RESPONSE, sw);

            Pb connected;
            connected.str(1, String("port_") + n + "_connected");
            connected.fixed32(2, connectedKey(i));
            connected.str(3, label + " connected");
            connected.str(5, "plug");
            send(c, LIST_BINARY_SENSOR_RESPONSE, connected);

            Pb amps;
            amps.str(1, String("port_") + n + "_current");
            amps.fixed32(2, ampsKey(i));
            amps.str(3, label + " current");
            amps.str(6, "A");
            amps.u32(7, 3);
            amps.str(9, "current");
            amps.u32(10, 1); // measurement
            send(c, LIST_SENSOR_RESPONSE, amps);
        }
        send(c, LIST_DONE_RESPONSE);
        break;
    case SUBSCRIBE_STATES_REQUEST:
        c.subscribed = true;
        for (int i = 0; i < API_NUM_PORTS; i++)
        {
            float amps = 0.0f;
            bool ok = hooks.portAmps(i, amps);
            sendSwitch(c, i, hooks.portOn(i));
            sendConnected(c, i, hooks.portConnected(i));
            sendAmps(c, i, ok, roundAmps(amps));
        }
        break;
    case SWITCH_COMMAND_REQUEST:
    {
        uint32_t key = 0;
        bool state = false;
        parseSwitchCommand(p, end, key, state);
        for (int i = 0; i < API_NUM_PORTS; i++)
            if (key == switchKey(i))
                hooks.setPort(i, state);
        break;
    }
    }
}

// Handle every complete message waiting in a client's buffer.
static void parse(Conn &c)
{
    while (!c.rx.empty() && c.client.connected())
    {
        const uint8_t *p = c.rx.data(), *end = p + c.rx.size();
        uint32_t len, type;
        if (*p++ != 0x00)
        {
            c.client.stop();
            return;
        }
        if (!readVarint(p, end, len) || !readVarint(p, end, type) || uint32_t(end - p) < len)
            return;
        handle(c, type, p, p + len);
        c.rx.erase(c.rx.begin(), c.rx.begin() + (p + len - c.rx.data()));
    }
}

// Read what a client sent, a chunk at a time, handling messages as they complete.
static void serve(Conn &c)
{
    uint8_t tmp[128];
    while (c.client.connected() && c.client.available())
    {
        int n = c.client.read(tmp, sizeof tmp);
        if (n <= 0)
            return;
        c.rx.insert(c.rx.end(), tmp, tmp + n);
        parse(c);
        if (c.rx.size() > API_RX_MAX)
            c.client.stop();
    }
}

void EspHomeApi::loop()
{
    if (!_started)
        return;
    if (hooks.enabled() != running)
        running ? stop() : start();

    WiFiClient incoming = server.accept();
    if (!running)
    {
        incoming.stop();
        return;
    }
    if (incoming)
    {
        Conn *slot = nullptr;
        for (auto &c : conns)
            if (!c.client.connected())
                slot = &c;
        if (slot)
        {
            *slot = Conn();
            slot->client = incoming;
            slot->client.setNoDelay(true);
        }
        else
            incoming.stop();
    }

    for (auto &c : conns)
        if (c.client.connected())
            serve(c);

    for (int i = 0; i < API_NUM_PORTS; i++)
    {
        bool on = hooks.portOn(i);
        bool connected = hooks.portConnected(i);
        for (auto &c : conns)
        {
            if (!c.subscribed || !c.client.connected())
                continue;
            if (on != lastOn[i])
                sendSwitch(c, i, on);
            if (connected != lastConnected[i])
                sendConnected(c, i, connected);
        }
        lastOn[i] = on;
        lastConnected[i] = connected;
    }

    // Clients only read names when they connect, so drop them after a rename
    // and let them reconnect.
    if (millis() - lastNamesMs >= API_NAMES_MS)
    {
        lastNamesMs = millis();
        bool renamed = false;
        for (int i = 0; i < API_NUM_PORTS; i++)
        {
            String name = hooks.portName(i);
            if (name != lastNames[i])
            {
                lastNames[i] = name;
                renamed = true;
            }
        }
        if (renamed)
            for (auto &c : conns)
                c.client.stop();
    }

    if (millis() - lastAmpsMs < API_AMPS_MS)
        return;
    lastAmpsMs = millis();
    for (int i = 0; i < API_NUM_PORTS; i++)
    {
        float amps = 0.0f;
        bool ok = hooks.portAmps(i, amps);
        amps = roundAmps(amps);
        if (amps == lastAmps[i])
            continue;
        lastAmps[i] = amps;
        for (auto &c : conns)
            if (c.subscribed && c.client.connected())
                sendAmps(c, i, ok, amps);
    }
}

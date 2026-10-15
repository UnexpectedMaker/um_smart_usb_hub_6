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
//
// Messages are protobuf, encoded by hand straight into the PSRAM frame buffer.
// Each is built after the first HEADER_BYTES, and its frame header (0x00,
// length, type) is written just in front of it once the length is known.
#include "esphome/EspHomeApi.h"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mdns.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define API_ESPHOME_VERSION "2025.10.0" // the ESPHome release this API matches

EspHomeApi g_espHomeApi;

namespace
{
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

// Entity keys for port i (0-5).
uint32_t switchKey(uint8_t i) { return 1 + i; }
uint32_t ampsKey(uint8_t i) { return 101 + i; }
uint32_t connectedKey(uint8_t i) { return 201 + i; }

// Protobuf encoder writing into a fixed buffer. len keeps counting past cap,
// so len > cap means the message didn't fit.
struct Proto
{
    Proto(uint8_t *buf, size_t cap) : buf(buf), cap(cap) {}

    uint8_t *buf;
    size_t cap;
    size_t len = 0;

    // Append one byte, if there's room.
    void byte(uint8_t b)
    {
        if (len < cap)
            buf[len] = b;
        len++;
    }
    // Append a varint.
    void varint(uint32_t v)
    {
        while (v >= 0x80)
        {
            byte((uint8_t)v | 0x80);
            v >>= 7;
        }
        byte((uint8_t)v);
    }
    // An unsigned or bool field. Zero is the default, so it's left out.
    void u32(uint8_t field, uint32_t v)
    {
        if (!v)
            return;
        varint(field << 3);
        varint(v);
    }
    // A fixed32 field.
    void fixed32(uint8_t field, uint32_t v)
    {
        varint(field << 3 | 5);
        for (int b = 0; b < 4; b++)
            byte((uint8_t)(v >> (8 * b)));
    }
    // A float field.
    void f32(uint8_t field, float f)
    {
        uint32_t v;
        memcpy(&v, &f, 4);
        fixed32(field, v);
    }
    // A string field of len bytes.
    void str(uint8_t field, const char *s, size_t len)
    {
        varint(field << 3 | 2);
        varint(len);
        for (size_t i = 0; i < len; i++)
            byte(s[i]);
    }
    // A string field.
    void str(uint8_t field, const char *s) { str(field, s, strlen(s)); }
};

// How many bytes of s fit in max without splitting a UTF-8 character.
size_t fitUtf8(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (n <= max)
        return n;
    while (max && ((uint8_t)s[max] & 0xC0) == 0x80)
        max--;
    return max;
}

// Decode a varint; false if it runs past end.
bool readVarint(const uint8_t *&p, const uint8_t *end, uint32_t &v)
{
    v = 0;
    for (int shift = 0; p < end && shift < 35; shift += 7)
    {
        uint8_t b = *p++;
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80))
            return true;
    }
    return false;
}

// Pull key and state out of a SwitchCommandRequest.
void parseSwitchCommand(const uint8_t *p, const uint8_t *end, uint32_t &key, bool &state)
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
            v = p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
            p += 4;
            if (tag >> 3 == 1)
                key = v;
            break;
        default:
            return;
        }
    }
}

// FNV-1a, to notice a rename without keeping a copy of the name.
uint32_t nameHash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

// Port i's name, or "Port n" if it has none. Returns its length.
int portLabel(const char *name, uint8_t i, char *out, size_t len)
{
    if (*name)
        return snprintf(out, len, "%s", name);
    return snprintf(out, len, "Port %u", i + 1);
}

// Round to the precision Home Assistant shows, so noise doesn't count as a change.
float roundAmps(float a) { return roundf(a * 1000.0f) / 1000.0f; }
}

void EspHomeApi::begin(const Hooks &hooks, const char *hostname, const char *version)
{
    if (_started)
        return;

    _frame = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool ok = _frame != nullptr;
    for (auto &c : _clients)
    {
        c.rx = (uint8_t *)heap_caps_malloc(RX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ok = ok && c.rx;
    }
    if (!ok)
    {
        Serial.println("[esphome] PSRAM allocation failed — API unavailable");
        heap_caps_free(_frame);
        _frame = nullptr;
        for (auto &c : _clients)
        {
            heap_caps_free(c.rx);
            c.rx = nullptr;
        }
        return;
    }

    _started = true;
    _hooks = hooks;
    strlcpy(_hostname, hostname, sizeof(_hostname));
    _version = version;
}

void EspHomeApi::start()
{
    _running = true;
    for (uint8_t i = 0; i < HUB_NUM_PORTS; i++)
    {
        _lastOn[i] = _hooks.portOn(i);
        _lastConnected[i] = _hooks.portConnected(i);
        _portNameHash[i] = nameHash(_hooks.portName(i));
    }
    _deviceNameHash = nameHash(_hooks.deviceName());

    // The socket is opened once and kept: the port can't always be bound
    // again straight after it's closed.
    if (_listenFd < 0)
    {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int one = 1;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (fd >= 0)
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (fd < 0 || bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, MAX_CLIENTS) < 0)
        {
            if (fd >= 0)
                close(fd);
            Serial.printf("[esphome] can't listen on port %u\r\n", PORT);
            return;
        }
        fcntl(fd, F_SETFL, O_NONBLOCK);
        _listenFd = fd;
    }

    uint8_t m[6];
    char mac[13];
    WiFi.macAddress(m);
    snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    mdns_txt_item_t txt[] = {
        {"mac", mac},
        {"version", API_ESPHOME_VERSION},
        {"friendly_name", _hooks.deviceName()},
        {"platform", "ESP32"},
        {"network", "wifi"},
    };
    mdns_service_add(nullptr, "_esphomelib", "_tcp", PORT, txt, sizeof(txt) / sizeof(txt[0]));
    Serial.printf("[esphome] API on port %u\r\n", PORT);
}

void EspHomeApi::stop()
{
    _running = false;
    for (auto &c : _clients)
        drop(c);
    mdns_service_remove("_esphomelib", "_tcp");
    Serial.println("[esphome] API off");
}

void EspHomeApi::drop(Client &c)
{
    if (c.fd >= 0)
        close(c.fd);
    c.fd = -1;
    c.rxLen = 0;
    c.subscribed = false;
}

void EspHomeApi::acceptClient()
{
    if (_listenFd < 0)
        return;
    int fd = accept(_listenFd, nullptr, nullptr);
    if (fd < 0)
        return;
    Client *slot = nullptr;
    for (auto &c : _clients)
        if (c.fd < 0)
            slot = &c;
    if (!_running || !slot)
    {
        close(fd);
        return;
    }
    // No TCP_NODELAY: lwIP queues at most 16 segments per socket, and the
    // entity list alone is 19 frames. Nagle lets back-to-back frames share
    // segments; a single frame still goes out straight away.
    fcntl(fd, F_SETFL, O_NONBLOCK);
    slot->fd = fd;
    slot->rxLen = 0;
    slot->subscribed = false;
    slot->pinged = false;
    slot->lastRxMs = millis();
}

void EspHomeApi::receive(Client &c)
{
    for (;;)
    {
        ssize_t n = ::recv(c.fd, c.rx + c.rxLen, RX_BYTES - c.rxLen, MSG_DONTWAIT);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (n <= 0)
        {
            drop(c);
            return;
        }
        c.rxLen += n;
        c.lastRxMs = millis();
        c.pinged = false;

        size_t used = 0;
        while (c.fd >= 0 && used < c.rxLen)
        {
            const uint8_t *p = c.rx + used, *end = c.rx + c.rxLen;
            uint32_t len, type;
            if (*p++ != 0x00)
            {
                drop(c);
                return;
            }
            if (!readVarint(p, end, len) || !readVarint(p, end, type) || (size_t)(end - p) < len)
                break;
            handle(c, type, p, p + len);
            used = p + len - c.rx;
        }
        if (c.fd < 0)
            return;
        memmove(c.rx, c.rx + used, c.rxLen - used);
        c.rxLen -= used;
        if (c.rxLen == RX_BYTES) // a message bigger than the buffer
        {
            drop(c);
            return;
        }
    }
    uint32_t quiet = millis() - c.lastRxMs;
    if (quiet > IDLE_MS)
        drop(c);
    else if (quiet > PING_MS && !c.pinged)
    {
        c.pinged = true;
        transmit(c, PING_REQUEST, 0);
    }
}

void EspHomeApi::transmit(Client &c, uint32_t type, size_t len)
{
    if (c.fd < 0)
        return;
    if (len > MESSAGE_BYTES)
    {
        Serial.printf("[esphome] message %u too big (%u bytes)\r\n", (unsigned)type, (unsigned)len);
        drop(c);
        return;
    }
    uint8_t head[HEADER_BYTES];
    Proto h(head, sizeof(head));
    h.byte(0x00);
    h.varint(len);
    h.varint(type);
    uint8_t *frame = message() - h.len;
    memcpy(frame, head, h.len);
    size_t total = h.len + len;
    if (::send(c.fd, frame, total, MSG_DONTWAIT) != (ssize_t)total)
        drop(c);
}

void EspHomeApi::sendDeviceInfo(Client &c)
{
    uint8_t m[6];
    char mac[18];
    WiFi.macAddress(m);
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);

    Proto p(message(), MESSAGE_BYTES);
    p.str(2, _hostname, fitUtf8(_hostname, 31));
    p.str(3, mac);
    p.str(4, API_ESPHOME_VERSION);
    p.str(5, __DATE__ ", " __TIME__);
    p.str(6, "Smart USB Hub");
    p.str(8, "unexpectedmaker.smart-usb-hub");
    p.str(9, _version);
    p.str(12, "Unexpected Maker");
    p.str(13, _hooks.deviceName(), fitUtf8(_hooks.deviceName(), 120));
    transmit(c, DEVICE_INFO_RESPONSE, p.len);
}

void EspHomeApi::sendEntities(Client &c)
{
    char label[100]; // a port name is at most PORT_NAME_MAX characters
    char text[128];
    for (uint8_t i = 0; i < HUB_NUM_PORTS && c.fd >= 0; i++)
    {
        portLabel(_hooks.portName(i), i, label, sizeof(label));

        Proto sw(message(), MESSAGE_BYTES);
        sw.str(1, text, snprintf(text, sizeof(text), "port_%u", i + 1));
        sw.fixed32(2, switchKey(i));
        sw.str(3, label);
        sw.str(9, "outlet");
        transmit(c, LIST_SWITCH_RESPONSE, sw.len);

        Proto con(message(), MESSAGE_BYTES);
        con.str(1, text, snprintf(text, sizeof(text), "port_%u_connected", i + 1));
        con.fixed32(2, connectedKey(i));
        con.str(3, text, snprintf(text, sizeof(text), "%s connected", label));
        con.str(5, "plug");
        transmit(c, LIST_BINARY_SENSOR_RESPONSE, con.len);

        Proto amps(message(), MESSAGE_BYTES);
        amps.str(1, text, snprintf(text, sizeof(text), "port_%u_current", i + 1));
        amps.fixed32(2, ampsKey(i));
        amps.str(3, text, snprintf(text, sizeof(text), "%s current", label));
        amps.str(6, "A");
        amps.u32(7, 3);
        amps.str(9, "current");
        amps.u32(10, 1); // measurement
        transmit(c, LIST_SENSOR_RESPONSE, amps.len);
    }
    transmit(c, LIST_DONE_RESPONSE, 0);
}

void EspHomeApi::sendSwitch(Client &c, uint8_t port, bool on)
{
    Proto p(message(), MESSAGE_BYTES);
    p.fixed32(1, switchKey(port));
    p.u32(2, on);
    transmit(c, SWITCH_STATE_RESPONSE, p.len);
}

void EspHomeApi::sendConnected(Client &c, uint8_t port, bool connected)
{
    Proto p(message(), MESSAGE_BYTES);
    p.fixed32(1, connectedKey(port));
    p.u32(2, connected);
    transmit(c, BINARY_SENSOR_STATE_RESPONSE, p.len);
}

// missing_state is set when the port has no monitor.
void EspHomeApi::sendAmps(Client &c, uint8_t port, bool ok, float amps)
{
    Proto p(message(), MESSAGE_BYTES);
    p.fixed32(1, ampsKey(port));
    p.f32(2, amps);
    p.u32(3, !ok);
    transmit(c, SENSOR_STATE_RESPONSE, p.len);
}

void EspHomeApi::handle(Client &c, uint32_t type, const uint8_t *p, const uint8_t *end)
{
    switch (type)
    {
    case HELLO_REQUEST:
    {
        char info[48];
        Proto m(message(), MESSAGE_BYTES);
        m.u32(1, 1); // API 1.12, as ESPHome 2025.10.0 reports
        m.u32(2, 12);
        m.str(3, info, snprintf(info, sizeof(info), "Smart USB Hub %s", _version));
        m.str(4, _hostname, fitUtf8(_hostname, 31));
        transmit(c, HELLO_RESPONSE, m.len);
        break;
    }
    case CONNECT_REQUEST:
        transmit(c, CONNECT_RESPONSE, 0);
        break;
    case DISCONNECT_REQUEST:
        transmit(c, DISCONNECT_RESPONSE, 0);
        drop(c);
        break;
    case PING_REQUEST:
        transmit(c, PING_RESPONSE, 0);
        break;
    case DEVICE_INFO_REQUEST:
        sendDeviceInfo(c);
        break;
    case LIST_ENTITIES_REQUEST:
        sendEntities(c);
        break;
    case SUBSCRIBE_STATES_REQUEST:
        c.subscribed = true;
        for (uint8_t i = 0; i < HUB_NUM_PORTS; i++)
        {
            float amps = 0.0f;
            bool ok = _hooks.portAmps(i, amps);
            sendSwitch(c, i, _hooks.portOn(i));
            sendConnected(c, i, _hooks.portConnected(i));
            sendAmps(c, i, ok, roundAmps(amps));
        }
        break;
    case SWITCH_COMMAND_REQUEST:
    {
        uint32_t key = 0;
        bool state = false;
        parseSwitchCommand(p, end, key, state);
        for (uint8_t i = 0; i < HUB_NUM_PORTS; i++)
            if (key == switchKey(i))
                _hooks.setPort(i, state);
        break;
    }
    }
}

void EspHomeApi::pushChanges()
{
    for (uint8_t i = 0; i < HUB_NUM_PORTS; i++)
    {
        bool on = _hooks.portOn(i);
        bool connected = _hooks.portConnected(i);
        for (auto &c : _clients)
        {
            if (c.fd < 0 || !c.subscribed)
                continue;
            if (on != _lastOn[i])
                sendSwitch(c, i, on);
            if (connected != _lastConnected[i])
                sendConnected(c, i, connected);
        }
        _lastOn[i] = on;
        _lastConnected[i] = connected;
    }

    if (millis() - _namesAt >= NAMES_MS)
    {
        _namesAt = millis();
        bool renamed = false;
        for (uint8_t i = 0; i < HUB_NUM_PORTS; i++)
        {
            uint32_t h = nameHash(_hooks.portName(i));
            renamed |= (h != _portNameHash[i]);
            _portNameHash[i] = h;
        }
        uint32_t h = nameHash(_hooks.deviceName());
        if (h != _deviceNameHash)
        {
            _deviceNameHash = h;
            mdns_service_txt_item_set("_esphomelib", "_tcp", "friendly_name", _hooks.deviceName());
            renamed = true;
        }
        if (renamed)
            for (auto &c : _clients)
                drop(c);
    }

    if (millis() - _ampsAt < AMPS_MS)
        return;
    _ampsAt = millis();
    for (uint8_t i = 0; i < HUB_NUM_PORTS; i++)
    {
        float amps = 0.0f;
        bool ok = _hooks.portAmps(i, amps);
        amps = roundAmps(amps);
        if (amps == _lastAmps[i])
            continue;
        _lastAmps[i] = amps;
        for (auto &c : _clients)
            if (c.fd >= 0 && c.subscribed)
                sendAmps(c, i, ok, amps);
    }
}

void EspHomeApi::loop()
{
    if (!_started)
        return;
    if (_hooks.enabled() != _running)
        _running ? stop() : start();
    acceptClient();
    if (!_running)
        return;
    for (auto &c : _clients)
        if (c.fd >= 0)
            receive(c);
    pushChanges();
}

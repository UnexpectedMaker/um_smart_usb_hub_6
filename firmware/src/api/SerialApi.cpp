// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
#include "SerialApi.h"
#include "ApiRequest.h"
#include "json.h"
#include <ESPAsyncWebServer.h>
#include <mbedtls/base64.h>

SerialApi serialApi;

static constexpr char FRAME_START = 0x1e;
static constexpr size_t FRAME_RAW_BYTES = 768;
static constexpr uint32_t WRITE_TIMEOUT_MS = 1000;

class FrameWriter : public Print
{
public:
    explicit FrameWriter(uint32_t id) : _id(id) {}

    size_t write(uint8_t c) override { return write(&c, 1); }

    size_t write(const uint8_t *data, size_t size) override
    {
        size_t left = size;
        while (left)
        {
            size_t n = min(left, FRAME_RAW_BYTES - _length);
            memcpy(_raw + _length, data, n);
            _length += n;
            data += n;
            left -= n;
            if (_length == FRAME_RAW_BYTES)
                flush();
        }
        return size;
    }

    void flush() override
    {
        if (!_length)
            return;
        int head = snprintf(_line, sizeof(_line), "%cD %lu %lu ", FRAME_START, (unsigned long)_id, (unsigned long)_frames);
        size_t encoded = 0;
        mbedtls_base64_encode((unsigned char *)_line + head, sizeof(_line) - head - 2, &encoded, _raw, _length);
        size_t total = head + encoded;
        _line[total++] = '\r';
        _line[total++] = '\n';
        send(_line, total);
        _frames++;
        _length = 0;
    }

    void finish(int status, const String &type)
    {
        flush();
        int total = snprintf(_line, sizeof(_line), "%cE %lu %d %lu %s\r\n", FRAME_START, (unsigned long)_id, status,
                             (unsigned long)_frames, type.c_str());
        send(_line, total);
    }

private:
    void send(const char *text, size_t length)
    {
        if (_failed)
            return;
        uint32_t start = millis();
        while ((size_t)Serial.availableForWrite() < length)
        {
            if (millis() - start > WRITE_TIMEOUT_MS)
            {
                _failed = true;
                return;
            }
            delay(1);
        }
        Serial.write((const uint8_t *)text, length);
    }

    uint32_t _id;
    uint32_t _frames = 0;
    bool _failed = false;
    size_t _length = 0;
    uint8_t _raw[FRAME_RAW_BYTES];
    char _line[48 + (FRAME_RAW_BYTES / 3) * 4 + 4];
};

class SerialApiResponse : public ApiResponse
{
public:
    void addHeader(const char *, const char *) override {}
};

class SerialApiRequest : public ApiRequest
{
public:
    SerialApiRequest(const String &url, uint8_t method, FrameWriter &out) : _url(url), _method(method), _out(out) {}

    void addParam(const String &name, const String &value) { _params.emplace_back(name, value); }

    const String &url() const override { return _url; }
    uint8_t method() const override { return _method; }
    void send(int code, const char *type, const String &body) override
    {
        _status = code;
        _type = type;
        _body = body;
    }
    Print *beginResponseStream(const char *type, size_t) override
    {
        _type = type;
        return &_out;
    }
    void send(Print *) override {}
    ApiResponse *beginResponse(const char *type, size_t length, ApiFiller filler) override
    {
        _type = type;
        _length = length;
        _filler = filler;
        return &_response;
    }
    ApiResponse *beginChunkedResponse(const char *type, ApiFiller filler) override
    {
        return beginResponse(type, SIZE_MAX, filler);
    }
    void send(ApiResponse *) override {}

    void finish()
    {
        if (_filler)
        {
            uint8_t chunk[512];
            size_t sent = 0;
            while (sent < _length)
            {
                size_t n = _filler(chunk, min(sizeof(chunk), _length - sent), sent);
                if (n == RESPONSE_TRY_AGAIN)
                {
                    delay(1);
                    continue;
                }
                if (!n)
                    break;
                _out.write(chunk, n);
                sent += n;
            }
        }
        else
        {
            _out.print(_body);
        }
        _out.finish(_status, _type);
    }

private:
    String _url;
    uint8_t _method;
    FrameWriter &_out;
    int _status = 200;
    String _type = "application/json";
    String _body;
    ApiFiller _filler;
    size_t _length = 0;
    SerialApiResponse _response;
};

static String paramText(const nlohmann::json &value)
{
    if (value.is_string())
        return String(value.get<std::string>().c_str());
    if (value.is_boolean())
        return value.get<bool>() ? "1" : "0";
    return String(value.dump().c_str());
}

bool SerialApi::queue(const String &line)
{
    if (_count == QUEUE_LENGTH)
        return false;
    _queue[(_head + _count) % QUEUE_LENGTH] = line;
    _count++;
    return true;
}

void SerialApi::service()
{
    while (_count)
    {
        String line = _queue[_head];
        _queue[_head] = String();
        _head = (_head + 1) % QUEUE_LENGTH;
        _count--;
        run(line);
    }
}

void SerialApi::run(const String &line)
{
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(line.c_str());
    }
    catch (...)
    {
    }
    uint32_t id = j.is_object() ? j.value("id", 0u) : 0;
    FrameWriter out(id);
    String method = j.is_object() ? j.value("method", std::string("GET")).c_str() : "";
    method.toUpperCase();
    uint8_t methodBit = method == "GET" ? HTTP_GET : method == "POST" ? HTTP_POST : 0;
    if (!j.is_object() || !methodBit || !j.contains("path") || !j["path"].is_string())
    {
        SerialApiRequest bad("", 0, out);
        bad.send(400, "application/json", "{\"error\":\"bad request\"}");
        bad.finish();
        return;
    }

    SerialApiRequest req(j["path"].get<std::string>().c_str(), methodBit, out);
    if (j.contains("query") && j["query"].is_object())
        for (auto it = j["query"].begin(); it != j["query"].end(); ++it)
            if (!it->is_null())
                req.addParam(it.key().c_str(), paramText(*it));
    String body;
    if (j.contains("body"))
        body = j["body"].is_string() ? j["body"].get<std::string>().c_str() : j["body"].dump().c_str();
    api.handle(&req, body);
    req.finish();
}

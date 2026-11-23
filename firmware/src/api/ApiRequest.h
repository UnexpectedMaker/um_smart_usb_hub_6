// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>

class ApiParam
{
public:
    ApiParam(const String &name, const String &value) : _name(name), _value(value) {}
    const String &name() const { return _name; }
    const String &value() const { return _value; }

private:
    String _name;
    String _value;
};

using ApiFiller = std::function<size_t(uint8_t *, size_t, size_t)>;

class ApiResponse
{
public:
    virtual ~ApiResponse() = default;
    virtual void addHeader(const char *name, const char *value) = 0;
};

class ApiRequest
{
public:
    virtual ~ApiRequest() = default;
    virtual const String &url() const = 0;
    virtual uint8_t method() const = 0;
    virtual void send(int code, const char *type, const String &body) = 0;
    virtual Print *beginResponseStream(const char *type, size_t bufferSize) = 0;
    virtual void send(Print *stream) = 0;
    virtual ApiResponse *beginResponse(const char *type, size_t length, ApiFiller filler) = 0;
    virtual ApiResponse *beginChunkedResponse(const char *type, ApiFiller filler) = 0;
    virtual void send(ApiResponse *response) = 0;

    bool hasParam(const char *name) const { return getParam(name) != nullptr; }
    const ApiParam *getParam(const char *name) const
    {
        for (auto &param : _params)
            if (param.name() == name)
                return &param;
        return nullptr;
    }

    void *_tempObject = nullptr;

protected:
    std::vector<ApiParam> _params;
};

using ApiHandler = std::function<void(ApiRequest *)>;
using ApiBodyHandler = std::function<void(ApiRequest *, uint8_t *, size_t, size_t, size_t)>;

class ApiRouter
{
public:
    void on(const char *uri, uint8_t methods, ApiHandler onRequest, std::nullptr_t = nullptr, ApiBodyHandler onBody = nullptr);
    void handle(ApiRequest *req, const String &body);

private:
    struct Route
    {
        String uri;
        uint8_t methods;
        ApiHandler onRequest;
        ApiBodyHandler onBody;
    };
    std::vector<Route> _routes;
};

extern ApiRouter api;

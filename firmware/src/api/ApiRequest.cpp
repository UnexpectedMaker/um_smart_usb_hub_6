// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
#include "ApiRequest.h"

ApiRouter api;

void ApiRouter::on(const char *uri, uint8_t methods, ApiHandler onRequest, std::nullptr_t, ApiBodyHandler onBody)
{
    _routes.push_back({uri, methods, onRequest, onBody});
}

void ApiRouter::handle(ApiRequest *req, const String &body)
{
    const String &url = req->url();
    for (auto &route : _routes)
    {
        if (!(route.methods & req->method()))
            continue;
        if (url != route.uri && !url.startsWith(route.uri + "/"))
            continue;
        if (route.onBody && body.length())
            route.onBody(req, (uint8_t *)body.c_str(), body.length(), 0, body.length());
        route.onRequest(req);
        return;
    }
    req->send(404, "application/json", "{\"error\":\"not found\"}");
}

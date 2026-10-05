// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// HubWebServer.cpp — HTTP routes: the embedded web UI and the JSON API.
// The JSON bodies themselves are built in main.ino and handed in as callbacks.
#include "HubWebServer.h"
#include "HubWebUI.h"
#include "settings/HubSettings.h"
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);
HubWebServer webServer;

static const char *fieldTypeStr(HubSettingField::T t)
{
    switch (t)
    {
    case HubSettingField::T::Bool: return "bool";
    case HubSettingField::T::Int:  return "int";
    default:                       return "string";
    }
}

// One settings group as {name, label, values, fields} — what the Settings
// drawer builds its form from.
static nlohmann::json groupSchemaJson(HubSettings *m)
{
    nlohmann::json g;
    g["name"] = m->name();
    g["label"] = m->label();
    nlohmann::json values;
    m->toJson(values);
    g["values"] = values;

    nlohmann::json fields = nlohmann::json::array();
    for (auto &f : m->uiSchema())
    {
        nlohmann::json fj;
        fj["type"] = fieldTypeStr(f.type);
        fj["key"] = f.key;
        fj["label"] = f.label;
        if (f.type == HubSettingField::T::Int)
        {
            fj["min"] = f.min;
            fj["max"] = f.max;
            fj["step"] = f.step;
        }
        if (f.help)
            fj["help"] = f.help;
        fields.push_back(fj);
    }
    g["fields"] = fields;
    return g;
}

// Collect a request body that may arrive in several chunks. Returns true, with
// the whole body in `out`, once the last chunk is in.
static bool collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total,
                        String &out)
{
    if (index == 0)
    {
        req->_tempObject = new String();
        ((String *)req->_tempObject)->reserve(total);
    }
    ((String *)req->_tempObject)->concat((const char *)data, len);
    if (index + len != total)
        return false;
    out = *(String *)req->_tempObject;
    delete (String *)req->_tempObject;
    req->_tempObject = nullptr;
    return true;
}

void HubWebServer::begin(StatusFn statusFn, ThemeFn themeFn, PortFn portFn,
                         SchedGetFn schedGetFn, SchedSetFn schedSetFn, SaveFn saveFn)
{
    if (_started)
        return;
    _started = true;

    // ---- Web UI, streamed straight out of flash ----
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", HUB_INDEX_HTML);
    });
    server.on("/hub.css", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/css", HUB_STYLE_CSS);
    });
    server.on("/hub.js", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/javascript", HUB_SCRIPT_JS);
    });

    // ---- Web UI data ----

    // Everything the page shows, polled once a second.
    server.on("/api/status", HTTP_GET, [statusFn](AsyncWebServerRequest *req) {
        req->send(200, "application/json", statusFn());
    });

    // POST /api/theme?value=dark|light — store the UI theme.
    server.on("/api/theme", HTTP_POST, [themeFn](AsyncWebServerRequest *req) {
        if (req->hasParam("value"))
            themeFn(req->getParam("value")->value());
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // POST /api/port?n=1..6&on=0|1 — switch one port. Replies with the status
    // read back afterwards, so the page shows what really happened.
    server.on("/api/port", HTTP_POST, [portFn, statusFn](AsyncWebServerRequest *req) {
        if (req->hasParam("n") && req->hasParam("on"))
            portFn(req->getParam("n")->value().toInt(), req->getParam("on")->value().toInt() != 0);
        req->send(200, "application/json", statusFn());
    });

    // All schedules. POST replaces them (JSON body) and replies with what was
    // stored, including which entries were found invalid.
    server.on("/api/schedules", HTTP_GET, [schedGetFn](AsyncWebServerRequest *req) {
        req->send(200, "application/json", schedGetFn());
    });
    server.on(
        "/api/schedules", HTTP_POST,
        [schedGetFn](AsyncWebServerRequest *req) { req->send(200, "application/json", schedGetFn()); },
        nullptr,
        [schedSetFn](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            String body;
            if (collectBody(req, data, len, index, total, body))
                schedSetFn(body);
        });

    // Settings form: GET the structure and current values of every visible
    // group; POST {"group":{...},...} to update those groups and save.
    server.on("/api/settings/schema", HTTP_GET, [](AsyncWebServerRequest *req) {
        nlohmann::json groups = nlohmann::json::array();
        for (auto *m : HubRegistry::all())
            if (m->show_on_website())
                groups.push_back(groupSchemaJson(m));
        nlohmann::json out;
        out["groups"] = groups;
        req->send(200, "application/json", String(out.dump().c_str()));
    });
    server.on(
        "/api/settings", HTTP_POST,
        [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); },
        nullptr,
        [saveFn](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            String body;
            if (!collectBody(req, data, len, index, total, body))
                return;
            try
            {
                nlohmann::json j = nlohmann::json::parse(body.c_str());
                if (!j.is_object())
                    return;
                for (auto it = j.begin(); it != j.end(); ++it)
                {
                    HubSettings *m = HubRegistry::get(it.key().c_str());
                    if (!m)
                        continue;
                    m->fromJson(it.value());
                    HubRegistry::dispatchConfigChanged(it.key().c_str());
                }
                saveFn();
            }
            catch (...)
            {
                // Malformed JSON — leave the settings exactly as they were.
            }
        });

    // Start an IP-geolocation lookup. It runs from the main loop, not here.
    server.on("/api/location/detect", HTTP_POST, [](AsyncWebServerRequest *req) {
        g_location.requestGeoLookup();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    registerRest();

    server.onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "application/json", "{\"error\":\"not found\"}");
    });
    server.begin();
}

// The web server library treats a route as a prefix: "/api/ports" also
// claims "/api/ports/3", and the first route registered wins. So the REST
// routes are registered most-specific first, and each handler answers only
// its exact path — anything longer (e.g. /api/ports/9) gets a 404.
static bool exactPath(AsyncWebServerRequest *req, const String &path)
{
    if (req->url() == path)
        return true;
    req->send(404, "application/json", "{\"error\":\"not found\"}");
    return false;
}

// The REST API for other clients (see the API docs). Per-port paths are
// registered one by one for ports 1..6: path parameters in this web server
// library need a regex build flag, and with six fixed ports the explicit form
// is simpler and can't misparse.
void HubWebServer::registerRest()
{
    for (int n = 1; n <= 6; n++)
    {
        String base = "/api/ports/" + String(n);
        String sched = base + "/schedule";
        server.on(sched.c_str(), HTTP_GET, [this, n, sched](AsyncWebServerRequest *req) {
            if (exactPath(req, sched))
                req->send(200, "application/json", _rest.portSchedule(n));
        });
        server.on(base.c_str(), HTTP_GET, [this, n, base](AsyncWebServerRequest *req) {
            if (exactPath(req, base))
                req->send(200, "application/json", _rest.port(n));
        });
    }

    server.on("/api/ports", HTTP_GET, [this](AsyncWebServerRequest *req) {
        if (exactPath(req, "/api/ports"))
            req->send(200, "application/json", _rest.ports());
    });

    // POST /api/hub/ports?on=0|1 (also true/false, on/off) — every port at
    // once. Replies with the ports read back afterwards.
    server.on("/api/hub/ports", HTTP_POST, [this](AsyncWebServerRequest *req) {
        if (!exactPath(req, "/api/hub/ports"))
            return;
        String v = req->hasParam("on") ? req->getParam("on")->value() : String();
        v.toLowerCase();
        bool on = (v == "1" || v == "true" || v == "on");
        bool off = (v == "0" || v == "false" || v == "off");
        if (!on && !off)
        {
            req->send(400, "application/json", "{\"error\":\"bad param\",\"detail\":\"on must be 0 or 1\"}");
            return;
        }
        _rest.setAllPorts(on);
        req->send(200, "application/json", _rest.ports());
    });

    server.on("/api/hub", HTTP_GET, [this](AsyncWebServerRequest *req) {
        if (exactPath(req, "/api/hub"))
            req->send(200, "application/json", _rest.hub());
    });
}

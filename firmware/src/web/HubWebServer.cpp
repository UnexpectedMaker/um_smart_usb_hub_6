// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// HubWebServer.cpp — HTTP routes: the embedded web UI and the JSON API.
// The JSON bodies themselves are built in main.ino and handed in as callbacks.
// The port history is streamed straight out of the PSRAM history (g_history).
#include "HubWebServer.h"
#include "HubWebUI.h"
#include "HubWebIcons.h"
#include "history/PortHistory.h"
#include "settings/HubSettings.h"
#include <ESPAsyncWebServer.h>
#include <memory>

static AsyncWebServer server(80);
HubWebServer webServer;

// Convert a settings field kind into the JSON schema string used by the UI.
static const char *fieldTypeStr(HubSettingField::T t)
{
    switch (t)
    {
    case HubSettingField::T::Bool: return "bool";
    case HubSettingField::T::Int:  return "int";
    case HubSettingField::T::Choice: return "choice";
    case HubSettingField::T::PortBools: return "port_bools";
    default:                       return "string";
    }
}

// Split a comma-separated settings option list into JSON string values.
static nlohmann::json optionListJson(const char *csv)
{
    nlohmann::json options = nlohmann::json::array();
    if (!csv)
        return options;
    String rest(csv);
    while (rest.length())
    {
        int comma = rest.indexOf(',');
        String value = (comma >= 0) ? rest.substring(0, comma) : rest;
        value.trim();
        if (value.length())
            options.push_back(value.c_str());
        if (comma < 0)
            break;
        rest = rest.substring(comma + 1);
    }
    return options;
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
        if (f.value)
            fj["value"] = f.value;
        if (f.type == HubSettingField::T::Choice)
            fj["options"] = optionListJson(f.options);
        fields.push_back(fj);
    }
    g["fields"] = fields;
    return g;
}

// A request parameter that must be a whole number, 0..4294967295. False if it
// isn't.
static bool paramUint(AsyncWebServerRequest *req, const char *name, uint32_t &out)
{
    const String &v = req->getParam(name)->value();
    if (!v.length() || v.length() > 10)
        return false;
    uint64_t n = 0;
    for (unsigned i = 0; i < v.length(); i++)
    {
        char c = v[i];
        if (c < '0' || c > '9')
            return false;
        n = n * 10 + (c - '0');
    }
    if (n > UINT32_MAX)
        return false;
    out = (uint32_t)n;
    return true;
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
                         EnvHistoryFn envHistoryFn,
                         SchedGetFn schedGetFn, SchedSetFn schedSetFn)
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
    // The icon as PNG and ICO, for Safari, which doesn't use the SVG icon in
    // the page and also asks for these paths on its own.
    auto icon = [](const char *path, const char *type, const uint8_t *data, size_t len) {
        server.on(path, HTTP_GET, [type, data, len](AsyncWebServerRequest *req) {
            AsyncWebServerResponse *res = req->beginResponse(200, type, data, len);
            res->addHeader("Cache-Control", "max-age=86400");
            req->send(res);
        });
    };
    icon("/favicon.ico", "image/x-icon", HUB_FAVICON_ICO, sizeof(HUB_FAVICON_ICO));
    icon("/favicon-32.png", "image/png", HUB_FAVICON_32_PNG, sizeof(HUB_FAVICON_32_PNG));
    icon("/apple-touch-icon.png", "image/png", HUB_APPLE_TOUCH_PNG, sizeof(HUB_APPLE_TOUCH_PNG));
    icon("/apple-touch-icon-precomposed.png", "image/png", HUB_APPLE_TOUCH_PNG, sizeof(HUB_APPLE_TOUCH_PNG));

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

    // GET /api/history?port=1..6 — one page of a port's PSRAM history, in
    // binary (layout: PortHistory::Page). Without before/after: the newest
    // 10 minutes. before=<count>: the 10 minutes before that capture count,
    // for filling in older history. after=<count>: everything newer than that
    // capture count, for live updates. max=<n> sends at most n readings.
    // 400 for a bad port or parameter, 503 if there's no PSRAM history.
    server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *req) {
        uint32_t port = 0;
        if (req->hasParam("port"))
        {
            // NOT ALLOWED: history is only ever served for one port at a time.
            // port=all (every port's history in one response) is removed and
            // must not be put back. Rejected with 400 below.
            if (!paramUint(req, "port", port))
                port = 0;
        }
        if (port < 1 || port > HUB_NUM_PORTS)
        {
            req->send(400, "application/json", "{\"error\":\"port must be 1-6\"}");
            return;
        }
        bool before = req->hasParam("before");
        bool after = req->hasParam("after");
        uint32_t count = 0;
        uint32_t most = PortHistory::PAGE_SAMPLES;
        if ((before && after) ||
            (before && !paramUint(req, "before", count)) ||
            (after && !paramUint(req, "after", count)) ||
            (req->hasParam("max") && (!paramUint(req, "max", most) || most < 1 || most > PortHistory::PAGE_SAMPLES)))
        {
            req->send(400, "application/json", "{\"error\":\"bad parameter\"}");
            return;
        }
        PortHistory::PageFrom from = before ? PortHistory::PAGE_BEFORE
                                   : after  ? PortHistory::PAGE_AFTER
                                            : PortHistory::PAGE_NEWEST;
        auto page = std::make_shared<PortHistory::Page>();
        uint8_t index = port - 1;
        if (!g_history.page(index, from, count, most, *page))
        {
            req->send(503, "application/json", "{\"error\":\"no history\"}");
            return;
        }
        size_t length = page->headBytes + (size_t)page->readings * PortHistory::SAMPLE_BYTES;
        // Sent a piece at a time: the header and events, then the readings,
        // copied out of PSRAM as they go, always whole readings at a time.
        AsyncWebServerResponse *res = req->beginResponse(
            "application/octet-stream", length,
            [page, index](uint8_t *buf, size_t maxLen, size_t sent) -> size_t {
                if (sent < page->headBytes)
                {
                    size_t n = min(maxLen, page->headBytes - sent);
                    memcpy(buf, page->head + sent, n);
                    return n;
                }
                uint32_t at = (sent - page->headBytes) / PortHistory::SAMPLE_BYTES;
                uint32_t n = min<uint32_t>(maxLen / PortHistory::SAMPLE_BYTES, page->readings - at);
                if (!n)
                    return RESPONSE_TRY_AGAIN;
                g_history.readRaw(index, page->first + at, n, buf);
                return (size_t)n * PortHistory::SAMPLE_BYTES;
            });
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
    });

    // GET /api/environment/history?since=<millis>&abs=1 — retained sidebar
    // temperature/humidity trend data. Relative time is the default for
    // browser reloads because client and hub clocks are different.
    server.on("/api/environment/history", HTTP_GET, [envHistoryFn](AsyncWebServerRequest *req) {
        uint32_t since = req->hasParam("since")
                             ? (uint32_t)strtoul(req->getParam("since")->value().c_str(), nullptr, 10)
                             : 0;
        bool abs = req->hasParam("abs") && req->getParam("abs")->value().toInt() != 0;
        AsyncResponseStream *res = req->beginResponseStream("application/json", 1024);
        envHistoryFn(*res, since, abs);
        req->send(res);
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
    // group; POST {"group":{...},...} to update those groups. Each group sent
    // is saved to its own file; groups not sent aren't touched.
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
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
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
                    m->requestSave();
                    HubRegistry::dispatchConfigChanged(it.key().c_str());
                }
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
        String history = base + "/history";
        // GET /api/ports/{n}/history?page=<1..> — the port's PSRAM history as
        // JSON, 10 minutes per page (layout: PortHistory::jsonPage()). Page 1
        // (the default) is the newest 10 minutes, page 2 the 10 minutes before
        // that, and so on; the response says how many pages there are. 400 for
        // a bad page number, 503 if there's no PSRAM history.
        server.on(history.c_str(), HTTP_GET, [n, history](AsyncWebServerRequest *req) {
            if (!exactPath(req, history))
                return;
            uint32_t pageNo = 1;
            if (req->hasParam("page") && (!paramUint(req, "page", pageNo) || pageNo < 1))
            {
                req->send(400, "application/json", "{\"error\":\"bad param\",\"detail\":\"page must be 1 or more\"}");
                return;
            }
            auto jp = std::make_shared<PortHistory::JsonPage>();
            if (!g_history.jsonPage(n - 1, pageNo, *jp))
            {
                req->send(503, "application/json", "{\"error\":\"no history\",\"detail\":\"PSRAM history unavailable\"}");
                return;
            }
            // Streamed a piece at a time, so the page is never built in RAM.
            AsyncWebServerResponse *res = req->beginChunkedResponse(
                "application/json", [jp](uint8_t *buf, size_t maxLen, size_t) -> size_t {
                    size_t len = g_history.jsonChunk(*jp, buf, maxLen);
                    return (len || jp->done) ? len : RESPONSE_TRY_AGAIN;
                });
            res->addHeader("Cache-Control", "no-store");
            req->send(res);
        });
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

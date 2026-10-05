// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// HubSettings.cpp — the settings registry, /settings.json storage, and every
// settings group: their web form fields and the behaviour a few of them carry
// (timezone + location lookup, WiFi change detection, schedule validation).
#include "HubSettings.h"
#include <LittleFS.h>
#include <HTTPClient.h>
#include <string.h>
#include <time.h>

static const char *SETTINGS_PATH = "/settings.json";

// ---- HubRegistry ---------------------------------------------------------

// The list lives in a function-static so it exists before any group's
// constructor runs, whatever order the globals are initialised in.
static std::vector<HubSettings *> &registry()
{
    static std::vector<HubSettings *> v;
    return v;
}

void HubRegistry::add(HubSettings *m)
{
    if (m)
        registry().push_back(m);
}

const std::vector<HubSettings *> &HubRegistry::all() { return registry(); }

HubSettings *HubRegistry::get(const char *name)
{
    if (!name)
        return nullptr;
    for (auto *m : registry())
        if (strcmp(m->name(), name) == 0)
            return m;
    return nullptr;
}

void HubRegistry::dispatchNetUp(const char *ip)
{
    for (auto *m : registry())
        m->onNetUp(ip);
}

void HubRegistry::dispatchLoop()
{
    for (auto *m : registry())
        m->loop();
}

void HubRegistry::dispatchConfigChanged(const char *name)
{
    if (HubSettings *m = get(name))
        m->onConfigChanged();
}

// ---- HubConfig -----------------------------------------------------------

bool HubConfig::begin()
{
    for (auto *m : HubRegistry::all())
        m->bind(*this);
    if (!LittleFS.begin(true)) // true = format if it won't mount
    {
        Serial.println("[config] LittleFS mount failed — settings will not persist");
        return false;
    }
    _fs_ready = true;
    return load();
}

bool HubConfig::load()
{
    if (!_fs_ready)
        return false;

    nlohmann::json settings;
    bool have = false;
    File f = LittleFS.open(SETTINGS_PATH, "r");
    if (f)
    {
        String content = f.readString();
        f.close();
        try
        {
            settings = nlohmann::json::parse(content.c_str());
            have = settings.is_object();
        }
        catch (...)
        {
            Serial.printf("[config] %s is corrupt — using defaults\n", SETTINGS_PATH);
        }
    }

    // Each group gets its own entry, or an empty object (= struct defaults).
    // A group whose stored entry won't parse falls back to defaults on its own
    // without affecting the others.
    for (auto *m : HubRegistry::all())
    {
        try
        {
            m->fromJson(have && settings.contains(m->name()) ? settings[m->name()]
                                                             : nlohmann::json::object());
        }
        catch (...)
        {
            Serial.printf("[config] '%s' settings unreadable — using defaults\n", m->name());
            m->fromJson(nlohmann::json::object());
        }
    }
    Serial.printf("[config] settings loaded%s\n", have ? "" : " (defaults)");
    return true;
}

bool HubConfig::save()
{
    if (!_fs_ready)
        return false;
    nlohmann::json j = nlohmann::json::object();
    for (auto *m : HubRegistry::all())
    {
        nlohmann::json mj;
        m->toJson(mj);
        j[m->name()] = mj;
    }
    std::string content = j.dump(2);
    File f = LittleFS.open(SETTINGS_PATH, "w");
    if (!f)
        return false;
    f.write((const uint8_t *)content.data(), content.size());
    f.close();
    return true;
}

// ###########################################################################
// The settings groups
// ###########################################################################

DeviceSettings g_device;
WiFiSettings g_wifi;
LocationSettings g_location;
LedsSettings g_leds;
PortsSettings g_ports;
WebsiteSettings g_website;
SchedulesSettings g_schedules;

// Shorthand for building form fields.
using F = HubSettingField;

// ===========================================================================
// device
// ===========================================================================

std::vector<HubSettingField> DeviceSettings::uiSchema()
{
    return {
        {.type = F::T::String, .key = "device_name", .label = "Device name"},
        {.type = F::T::String, .key = "hostname", .label = "Hostname",
         .help = "Reach the hub at <hostname>.local. Takes effect after a restart."},
    };
}

String DeviceSettings::hostname() const
{
    String out;
    for (char c : _config.hostname)
    {
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (ok && !(c == '-' && out.length() == 0) && out.length() < 32)
            out += c;
    }
    while (out.endsWith("-"))
        out.remove(out.length() - 1);
    return out.length() ? out : String("usb-hub");
}

// ===========================================================================
// wifi
// ===========================================================================

std::vector<HubSettingField> WiFiSettings::uiSchema()
{
    return {
        {.type = F::T::String, .key = "ssid", .label = "SSID"},
        {.type = F::T::String, .key = "password", .label = "Password"},
        {.type = F::T::Bool, .key = "dhcp", .label = "DHCP",
         .help = "Off = use the static address below. DNS goes to the gateway."},
        {.type = F::T::String, .key = "ip", .label = "Static IP"},
        {.type = F::T::String, .key = "gateway", .label = "Gateway"},
        {.type = F::T::String, .key = "subnet", .label = "Subnet mask"},
    };
}

void WiFiSettings::fromJson(const nlohmann::json &j)
{
    HubWiFiConfig before = _config;
    HubSettingsT<HubWiFiConfig>::fromJson(j);
    _changed = _changed || before.ssid != _config.ssid || before.password != _config.password ||
               before.dhcp != _config.dhcp || before.ip != _config.ip ||
               before.gateway != _config.gateway || before.subnet != _config.subnet;
}

void WiFiSettings::setCredentials(const String &ssid, const String &password)
{
    _config.ssid = ssid;
    _config.password = password;
    if (_cfg)
        _cfg->save();
}

// ===========================================================================
// location
// ===========================================================================

std::vector<HubSettingField> LocationSettings::uiSchema()
{
    return {
        {.type = F::T::String, .key = "city", .label = "City"},
        {.type = F::T::String, .key = "country", .label = "Country"},
        {.type = F::T::Int, .key = "utc_offset", .label = "UTC offset", .min = -12, .max = 14, .step = 1},
        {.type = F::T::String, .key = "ntp_server", .label = "NTP server"},
    };
}

void LocationSettings::onNetUp(const char * /*ip*/)
{
    _net_up = true;
    applyTimeZone();
    // No location stored yet — fill it in from the public IP, once.
    if (!_geo_attempted && _config.city.length() == 0)
        _geo_request = true;
}

void LocationSettings::onConfigChanged()
{
    if (_net_up)
        applyTimeZone();
}

void LocationSettings::applyTimeZone()
{
    // POSIX TZ strings count hours WEST of UTC, so UTC+10 is written "UTC-10".
    // configTzTime() sets TZ after starting SNTP; configTime() would overwrite
    // it from its own offset arguments.
    char tz[16];
    snprintf(tz, sizeof(tz), "UTC%+d", -_config.utc_offset);
    const char *server = _config.ntp_server.length() ? _config.ntp_server.c_str() : "pool.ntp.org";
    configTzTime(tz, server);
    Serial.printf("[ntp] sync started: server=%s TZ=%s (utc_offset=%+dh)\n", server, tz, _config.utc_offset);
}

void LocationSettings::loop()
{
    if (!_geo_request)
        return;
    _geo_request = false;
    doIPGeo();
}

// Free, keyless IP geolocation. Fills city, country and UTC offset, re-applies
// the timezone, and saves.
void LocationSettings::doIPGeo()
{
    _geo_attempted = true;
    HTTPClient http;
    if (!http.begin("http://ip-api.com/json/?fields=status,countryCode,city,offset"))
        return;
    if (http.GET() == 200)
    {
        try
        {
            nlohmann::json j = nlohmann::json::parse(http.getString().c_str());
            if (j.value("status", std::string()) == "success")
            {
                // `offset` is seconds east of UTC, including DST at lookup time.
                _config.utc_offset = j.value("offset", 0) / 3600;
                std::string city = j.value("city", std::string());
                std::string cc = j.value("countryCode", std::string());
                if (city.length())
                    _config.city = String(city.c_str());
                if (cc.length())
                    _config.country = String(cc.c_str());
                Serial.printf("[loc] detected %s, %s (UTC%+d)\n",
                              _config.city.c_str(), _config.country.c_str(), _config.utc_offset);
                applyTimeZone();
                if (_cfg)
                    _cfg->save();
            }
        }
        catch (...)
        {
            Serial.println("[loc] IP geolocation reply unreadable");
        }
    }
    http.end();
}

// ===========================================================================
// leds
// ===========================================================================

std::vector<HubSettingField> LedsSettings::uiSchema()
{
    return {
        {.type = F::T::Bool, .key = "enabled", .label = "Enabled"},
        {.type = F::T::Int, .key = "brightness", .label = "Brightness", .min = 0, .max = 255, .step = 5},
    };
}

// ===========================================================================
// website
// ===========================================================================

void WebsiteSettings::setTheme(const String &theme)
{
    if (theme == "dark" || theme == "light")
        _config.theme = theme;
}

// ===========================================================================
// schedules
// ===========================================================================

const char *hubInvalidReason(uint8_t why)
{
    switch (why)
    {
    case HUB_INVALID_CLASH:         return "clash";
    case HUB_INVALID_INSIDE_WINDOW: return "inside_window";
    case HUB_INVALID_ENDS_AT_START: return "ends_at_start";
    case HUB_INVALID_INTERVAL:      return "interval_too_short";
    case HUB_INVALID_DUR_VS_EVERY:  return "duration_exceeds_interval";
    case HUB_INVALID_WINDOW:        return "window_ends_at_start";
    default:                        return "";
    }
}

int hubParseHHMM(const String &s)
{
    int h = 0, m = 0;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2)
        return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    return h * 60 + m;
}

int hubEntryWindow(const HubScheduleEntry &e)
{
    if (!e.on)
        return 0;
    if (e.mode == 1) // ON FOR
        return e.dur;
    if (e.mode == 2) // ON UNTIL — wraps past midnight
    {
        int u = hubParseHHMM(e.until), s = hubParseHHMM(e.time);
        if (u < 0 || s < 0)
            return 0;
        return (u - s + 1440) % 1440;
    }
    return 0; // plain ON — open-ended
}

bool SchedulesSettings::setFromJson(const String &body)
{
    try
    {
        fromJson(nlohmann::json::parse(body.c_str()));
        validate();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// An entry is invalid when acting on it would be ambiguous or contradictory.
// Rather than silently picking a winner, the offending entry is flagged; the
// scheduler skips it and the web UI shows why.
void SchedulesSettings::validate()
{
    _version++; // the scheduler's cached events are now stale
    for (uint8_t p = 0; p < HUB_NUM_PORTS; p++)
    {
        auto &list = _config.ports[p];
        for (auto &e : list)
        {
            e.invalid = false;
            e.why = HUB_INVALID_NONE;
        }

        // 1) Each entry on its own.
        for (size_t i = 0; i < list.size(); i++)
        {
            auto &e = list[i];
            if (!e.enabled || !e.on)
                continue;
            // ON UNTIL ending at its own start: is that 0 h or 24 h?
            if (e.mode == 2 && hubParseHHMM(e.until) == hubParseHHMM(e.time))
            {
                e.invalid = true;
                e.why = HUB_INVALID_ENDS_AT_START;
                Serial.printf("[sched] port %u: entry %u ends at its own start time — ignored\n", p + 1, (unsigned)i);
            }
            // ON EVERY: interval floor, pulse must fit the interval, and a
            // windowed repeat can't start and stop at the same time.
            if (e.mode == 3)
            {
                if (e.every < SCHED_MIN_EVERY)
                {
                    e.invalid = true;
                    e.why = HUB_INVALID_INTERVAL;
                    Serial.printf("[sched] port %u: entry %u repeats faster than %d min — ignored\n",
                                  p + 1, (unsigned)i, SCHED_MIN_EVERY);
                }
                else if (e.dur == 0 || e.dur >= e.every)
                {
                    e.invalid = true;
                    e.why = HUB_INVALID_DUR_VS_EVERY;
                    Serial.printf("[sched] port %u: entry %u on-time doesn't fit its interval — ignored\n",
                                  p + 1, (unsigned)i);
                }
                else if (!e.allday && hubParseHHMM(e.until) == hubParseHHMM(e.time))
                {
                    e.invalid = true;
                    e.why = HUB_INVALID_WINDOW;
                    Serial.printf("[sched] port %u: entry %u window starts and ends together — ignored\n",
                                  p + 1, (unsigned)i);
                }
            }
        }

        // 2) Two entries firing at the same time on a shared day. ON vs OFF:
        //    the OFF loses (the ON is taken as the intent). Two identical
        //    actions: the later one loses.
        for (size_t i = 0; i < list.size(); i++)
        {
            if (!list[i].enabled)
                continue;
            for (size_t k = i + 1; k < list.size(); k++)
            {
                if (!list[k].enabled || list[i].time != list[k].time)
                    continue;
                if ((list[i].days & list[k].days) == 0)
                    continue; // no day in common
                if (list[i].invalid || list[k].invalid)
                    continue; // already-invalid entries don't knock out others
                size_t bad = (list[i].on != list[k].on) ? (list[i].on ? k : i) : k;
                list[bad].invalid = true;
                list[bad].why = HUB_INVALID_CLASH;
                Serial.printf("[sched] port %u: entry %u (%s %s) clashes — ignored\n", p + 1, (unsigned)bad,
                              list[bad].time.c_str(), list[bad].on ? "ON" : "OFF");
            }
        }

        // 3) An entry firing strictly inside another entry's ON FOR / ON UNTIL
        //    window contradicts it — the window already says what the port
        //    does then. ON EVERY entries are left to the event timeline.
        for (size_t a = 0; a < list.size(); a++)
        {
            const auto &A = list[a];
            if (!A.enabled || A.invalid || !A.on || A.mode == 3)
                continue;
            int win = hubEntryWindow(A);
            int ta = hubParseHHMM(A.time);
            if (win == 0 || ta < 0)
                continue;
            for (size_t b = 0; b < list.size(); b++)
            {
                auto &B = list[b];
                if (b == a || !B.enabled || B.invalid)
                    continue;
                int tb = hubParseHHMM(B.time);
                if (tb < 0)
                    continue;
                bool hit = false;
                for (int da = 0; da < 7 && !hit; da++)
                {
                    if (!((A.days >> da) & 1))
                        continue;
                    int start = da * 1440 + ta;
                    for (int db = 0; db < 7 && !hit; db++)
                    {
                        if (!((B.days >> db) & 1))
                            continue;
                        int rel = ((db * 1440 + tb) - start + 10080) % 10080;
                        if (rel > 0 && rel < win)
                            hit = true;
                    }
                }
                if (hit)
                {
                    B.invalid = true;
                    B.why = HUB_INVALID_INSIDE_WINDOW;
                    Serial.printf("[sched] port %u: entry %u (%s) falls inside an ON window — ignored\n",
                                  p + 1, (unsigned)b, B.time.c_str());
                }
            }
        }
    }
}

void SchedulesSettings::disableEntry(uint8_t port, int index)
{
    if (port >= HUB_NUM_PORTS)
        return;
    auto &list = _config.ports[port];
    if (index >= 0 && (size_t)index < list.size())
        list[index].enabled = false;
}

String SchedulesSettings::toJsonString()
{
    // Built by hand so the transient `invalid` flag is included — it is
    // deliberately not part of the stored schema.
    String s = "{\"ports\":[";
    for (uint8_t p = 0; p < HUB_NUM_PORTS; p++)
    {
        if (p)
            s += ",";
        s += "[";
        const auto &list = _config.ports[p];
        for (size_t i = 0; i < list.size(); i++)
        {
            const auto &e = list[i];
            if (i)
                s += ",";
            s += "{\"days\":" + String(e.days);
            s += ",\"time\":\"" + e.time + "\"";
            s += ",\"on\":" + String(e.on ? "true" : "false");
            s += ",\"mode\":" + String(e.mode);
            s += ",\"every\":" + String(e.every);
            s += ",\"allday\":" + String(e.allday ? "true" : "false");
            s += ",\"dur\":" + String(e.dur);
            s += ",\"until\":\"" + e.until + "\"";
            s += ",\"once\":" + String(e.once ? "true" : "false");
            s += ",\"enabled\":" + String(e.enabled ? "true" : "false");
            s += ",\"invalid\":" + String(e.invalid ? "true" : "false");
            s += "}";
        }
        s += "]";
    }
    s += "]}";
    return s;
}

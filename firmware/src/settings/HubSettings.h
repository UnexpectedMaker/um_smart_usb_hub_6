// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// HubSettings.h — everything the hub stores in flash.
//
// Every persisted setting lives in a "settings group": a small class that owns
// a plain config struct. The first half of this file is the machinery the
// groups are built on:
//
//   HubSettings / HubSettingsT<T>  base class for a group. A group declares
//                                  its config struct, a name, a label, and the
//                                  list of fields the web Settings drawer
//                                  should show. JSON conversion is automatic.
//   HubRegistry                    every group registers itself on
//                                  construction, so storage, the web UI and the
//                                  lifecycle hooks can walk them all without a
//                                  hand-maintained list.
//   HubConfig                      loads and saves every group to a single
//                                  /settings.json on LittleFS.
//
// The second half is the groups themselves, each stored under its name():
//
//   device     g_device     display name and network hostname
//   wifi       g_wifi       credentials and DHCP / static addressing
//   location   g_location   city, UTC offset and NTP server
//   leds       g_leds       port LED on/off and brightness
//   ports      g_ports      which ports power up at boot
//   website    g_website    web UI theme
//   schedules  g_schedules  per-port power schedules, plus their validation
#pragma once
#include <Arduino.h>
#include <array>
#include <vector>
#include "json.h"

// ---- Arduino String <-> JSON -------------------------------------------
// Lets config structs use Arduino String fields directly with nlohmann's
// NLOHMANN_DEFINE_TYPE_* macros.
inline void to_json(nlohmann::json &j, const String &value) { j = value.c_str(); }
inline void from_json(const nlohmann::json &j, String &s) { s = j.get_ptr<const std::string *>()->c_str(); }

// One field in a group's web form. The values themselves come from the
// group's config struct; this only describes how to present them.
struct HubSettingField
{
    enum class T
    {
        Bool,   // on/off button
        Int,    // number box, bounded by min/max, stepping by step
        String  // text box (masked automatically if the key looks secret)
    };
    T type;
    const char *key;            // JSON key inside the group's config
    const char *label;          // shown to the user
    long min = 0;               // Int only
    long max = 0;               // Int only
    long step = 1;              // Int only
    const char *help = nullptr; // optional one-line hint shown under the field
};

class HubConfig;

// Base for every settings group. Use HubSettingsT<ConfigT> rather than this
// directly — it supplies the JSON conversion and self-registration.
class HubSettings
{
public:
    virtual ~HubSettings() = default;

    // Gives the group a way to persist its own changes (e.g. the IP-geo lookup
    // filling in a location). Called for every group by HubConfig::begin().
    void bind(HubConfig &cfg) { _cfg = &cfg; }

    // Unique key in /settings.json and in the settings API.
    virtual const char *name() const = 0;
    // Heading shown in the web Settings drawer.
    virtual const char *label() const = 0;
    // False hides the group from the Settings drawer (it is still stored).
    virtual bool show_on_website() const { return true; }

    virtual void toJson(nlohmann::json &j) const = 0;
    virtual void fromJson(const nlohmann::json &j) = 0;
    // Fields to show in the Settings drawer, in display order.
    virtual std::vector<HubSettingField> uiSchema() { return {}; }

    // ---- Lifecycle hooks (called via HubRegistry) ----
    // The network came up with this IP.
    virtual void onNetUp(const char *ip) {}
    // This group's values were just changed from the web UI.
    virtual void onConfigChanged() {}
    // Called every pass of the main loop.
    virtual void loop() {}

protected:
    HubConfig *_cfg = nullptr;
};

// Global list of settings groups, filled as each group is constructed.
class HubRegistry
{
public:
    static void add(HubSettings *m);
    static const std::vector<HubSettings *> &all();
    // Look a group up by name(); nullptr if there is none.
    static HubSettings *get(const char *name);

    // Fan a lifecycle event out to every group.
    static void dispatchNetUp(const char *ip);
    static void dispatchLoop();
    static void dispatchConfigChanged(const char *name);
};

// Typed base: owns the group's config struct, converts it to and from JSON
// with the struct's NLOHMANN_DEFINE_TYPE_* macro, and registers the group.
template <typename ConfigT>
class HubSettingsT : public HubSettings
{
public:
    HubSettingsT() { HubRegistry::add(this); }

    void toJson(nlohmann::json &j) const override { j = _config; }
    void fromJson(const nlohmann::json &j) override { _config = j.get<ConfigT>(); }

    const ConfigT &config() const { return _config; }

protected:
    ConfigT _config;
};

// Loads and saves every registered group to /settings.json on LittleFS.
// Each group is stored under its name(); a missing or unreadable entry falls
// back to that group's struct defaults.
class HubConfig
{
public:
    // Mount LittleFS (formatting it if it won't mount), bind every group to
    // this store, and load. Returns false if the filesystem is unusable.
    bool begin();
    bool load();
    bool save();

private:
    bool _fs_ready = false;
};

// ###########################################################################
// The settings groups
// ###########################################################################

static constexpr uint8_t HUB_NUM_PORTS = 6;

// ===========================================================================
// device — what the hub is called.
// ===========================================================================
struct HubDeviceConfig
{
    String device_name = "Smart USB Hub"; // shown in the web UI header
    String hostname = "usb-hub";          // network name: <hostname>.local
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubDeviceConfig, device_name, hostname);

class DeviceSettings : public HubSettingsT<HubDeviceConfig>
{
public:
    const char *name() const override { return "device"; }
    const char *label() const override { return "Device"; }
    std::vector<HubSettingField> uiSchema() override;

    // The stored hostname made safe to use on the network: lower case letters,
    // digits and '-', no leading or trailing '-', at most 32 characters.
    // Falls back to "usb-hub" if nothing usable is left.
    String hostname() const;
};
extern DeviceSettings g_device;

// ===========================================================================
// wifi — station credentials and addressing.
// ===========================================================================
struct HubWiFiConfig
{
    String ssid;
    String password;
    bool dhcp = true; // false = use ip / gateway / subnet below
    String ip;
    String gateway;
    String subnet;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubWiFiConfig, ssid, password, dhcp, ip, gateway, subnet);

class WiFiSettings : public HubSettingsT<HubWiFiConfig>
{
public:
    const char *name() const override { return "wifi"; }
    const char *label() const override { return "WiFi"; }
    std::vector<HubSettingField> uiSchema() override;

    // Notes whether anything actually changed, so saving the Settings drawer
    // only reconnects WiFi when the WiFi values were edited.
    void fromJson(const nlohmann::json &j) override;

    // Store new credentials and save (used by the serial console).
    void setCredentials(const String &ssid, const String &password);

    // True once after the WiFi values change. The main loop reconnects on it.
    bool consumeChanged()
    {
        bool c = _changed;
        _changed = false;
        return c;
    }

private:
    bool _changed = false;
};
extern WiFiSettings g_wifi;

// ===========================================================================
// location — where the hub is, which sets its clock.
//
// utc_offset becomes the timezone handed to the C library, so localtime_r()
// gives local time for the scheduler. With no city stored, the first time the
// network comes up a one-shot lookup on ip-api.com fills city, country and
// offset from the public IP, and saves them.
// ===========================================================================
struct HubLocationConfig
{
    String city;
    String country;      // ISO 3166-1 alpha-2, e.g. "AU"
    int utc_offset = 0;  // whole hours east of UTC, -12..+14
    String ntp_server = "pool.ntp.org";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubLocationConfig, city, country, utc_offset, ntp_server);

class LocationSettings : public HubSettingsT<HubLocationConfig>
{
public:
    const char *name() const override { return "location"; }
    const char *label() const override { return "Location & Time"; }
    std::vector<HubSettingField> uiSchema() override;

    void onNetUp(const char *ip) override; // start NTP, maybe look up location
    void onConfigChanged() override;       // re-apply the timezone after an edit
    void loop() override;                  // runs a requested lookup

    // Set the timezone from utc_offset and (re)start NTP. Safe to repeat.
    void applyTimeZone();

    // Ask for an IP-geolocation lookup. It runs from loop(), never inside a
    // web request, because it blocks on an HTTP call.
    void requestGeoLookup() { _geo_request = true; }

private:
    void doIPGeo();
    bool _net_up = false;
    bool _geo_attempted = false;
    volatile bool _geo_request = false;
};
extern LocationSettings g_location;

// ===========================================================================
// leds — the per-port RGB indicators.
// ===========================================================================
struct HubLedsConfig
{
    bool enabled = true;
    uint8_t brightness = 50; // 0-255
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubLedsConfig, enabled, brightness);

class LedsSettings : public HubSettingsT<HubLedsConfig>
{
public:
    const char *name() const override { return "leds"; }
    const char *label() const override { return "LEDs"; }
    std::vector<HubSettingField> uiSchema() override;
};
extern LedsSettings g_leds;

// ===========================================================================
// ports — the power state each port comes up in at boot. All off by default.
// Set through the API, not the Settings drawer.
// ===========================================================================
struct HubPortsConfig
{
    std::array<bool, HUB_NUM_PORTS> power_on_boot{{false, false, false, false, false, false}};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubPortsConfig, power_on_boot);

class PortsSettings : public HubSettingsT<HubPortsConfig>
{
public:
    const char *name() const override { return "ports"; }
    const char *label() const override { return "Ports"; }
};
extern PortsSettings g_ports;

// ===========================================================================
// website — the web UI's own preferences. Set by the theme button, not the
// Settings drawer.
// ===========================================================================
struct HubWebsiteConfig
{
    String theme = "dark"; // "dark" | "light"
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubWebsiteConfig, theme);

class WebsiteSettings : public HubSettingsT<HubWebsiteConfig>
{
public:
    const char *name() const override { return "website"; }
    const char *label() const override { return "Website"; }
    bool show_on_website() const override { return false; }

    // Store the theme; anything other than "dark" or "light" is ignored.
    void setTheme(const String &theme);
};
extern WebsiteSettings g_website;

// ===========================================================================
// schedules — per-port power schedules.
//
// Each port has a list of entries. An entry fires on the selected weekdays at
// `time` and does one of five things (see `on` + `mode`). The scheduler in
// main.ino expands every entry into switch events across the week; this file
// only stores entries and decides which ones are invalid.
// ===========================================================================

// Shortest allowed "ON EVERY" interval, in minutes. Bounds how many events a
// single entry can expand into across a week.
#define SCHED_MIN_EVERY 30

// Why validate() rejected an entry (HubScheduleEntry::why).
#define HUB_INVALID_NONE 0
#define HUB_INVALID_CLASH 1          // same time and a shared day as another entry
#define HUB_INVALID_INSIDE_WINDOW 2  // fires inside another entry's ON window
#define HUB_INVALID_ENDS_AT_START 3  // "ON UNTIL" ending at its own start time
#define HUB_INVALID_INTERVAL 4       // "ON EVERY" faster than SCHED_MIN_EVERY
#define HUB_INVALID_DUR_VS_EVERY 5   // on-time doesn't fit inside the interval
#define HUB_INVALID_WINDOW 6         // repeat window starts and stops together

struct HubScheduleEntry
{
    uint8_t days = 0;     // weekday bitmask, bit0 = Sunday .. bit6 = Saturday
    String time = "00:00"; // "HH:MM" 24 h local; the window start for mode 3
    bool on = false;       // false = switch OFF at `time`; true = one of the ON modes
    // Which ON variant:
    //   0 ON        switch on, open-ended
    //   1 ON FOR    on for `dur` minutes
    //   2 ON UNTIL  on until `until`
    //   3 ON EVERY  pulse on for `dur` every `every` minutes, all day or
    //               between `time` and `until`
    // Every field is always stored, so switching modes never loses a value.
    uint8_t mode = 0;
    uint16_t every = 30;     // mode 3 interval, minutes (>= SCHED_MIN_EVERY)
    bool allday = true;      // mode 3: all day, or only time..until
    uint16_t dur = 60;       // mode 1 window / mode 3 pulse length, minutes
    String until = "17:00";  // mode 2 end time / mode 3 window end. Stored as an
                             // end time so editing `time` doesn't move it.
    bool once = false;       // fire at the next occurrence, then disable itself
    bool enabled = true;     // disabled entries are kept but ignored
    // Set by validate(); never stored. Invalid entries are ignored by the
    // scheduler and flagged in the web UI.
    bool invalid = false;
    uint8_t why = HUB_INVALID_NONE;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubScheduleEntry, days, time, on, mode, dur, until, every, allday, once, enabled);

// "HH:MM" -> minutes since midnight (0..1439), or -1 if it isn't a valid time.
int hubParseHHMM(const String &s);

// Short machine-readable name for a HUB_INVALID_* code ("clash", ...).
const char *hubInvalidReason(uint8_t why);

// Length in minutes of an ON entry's window: its "ON FOR" duration, or the
// time to its "ON UNTIL" end (wrapping midnight). 0 = open-ended.
int hubEntryWindow(const HubScheduleEntry &e);

struct HubSchedulesConfig
{
    // One list per port: index 0 = port 1 .. index 5 = port 6.
    std::array<std::vector<HubScheduleEntry>, HUB_NUM_PORTS> ports;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubSchedulesConfig, ports);

class SchedulesSettings : public HubSettingsT<HubSchedulesConfig>
{
public:
    const char *name() const override { return "schedules"; }
    const char *label() const override { return "Schedules"; }
    bool show_on_website() const override { return false; }

    // Replace every port's schedule from {"ports":[[...],...]}. Returns false,
    // leaving the schedules untouched, if the JSON is malformed.
    bool setFromJson(const String &body);

    // All schedules as JSON, including each entry's `invalid` flag.
    String toJsonString();

    // Mark entries that contradict another entry (or themselves) as invalid.
    // Must run after every load or change.
    void validate();

    // Disable one entry — used when a one-shot entry has fired. Caller saves.
    void disableEntry(uint8_t port, int index);

    // Bumped by validate(). The scheduler caches its expanded events and
    // rebuilds them only when this changes.
    uint32_t version() const { return _version; }

private:
    uint32_t _version = 0;
};
extern SchedulesSettings g_schedules;

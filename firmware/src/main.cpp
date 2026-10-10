// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// main.cpp — Smart USB Hub firmware (ESP32-S3).
//
// Six USB-C downstream ports behind a WCH CH339W hub. For each port the ESP32
// can switch its 5V (TPS2552 load switch, via an XL9555 I/O expander), see an
// over-current fault, see a device plugged in even with the power off, and
// measure voltage / current / power (two INA3221s). Each port has an RGB LED
// showing its state.
//
// This file holds the application:
//   - pin map and hardware objects
//   - port LEDs (boot sweep, then live per-port state)
//   - power scheduler (weekly schedules, catch-up after a reboot)
//   - JSON for the web UI and the REST API
//   - WiFi, OTA and mDNS
//   - serial console (type 'help')
//   - setup() and loop()
//
// The rest of the firmware:
//   ports/      USBPort — one port's power, fault and device sense
//   settings/   HubSettings — everything stored in flash
//   web/        HubWebServer (routes, API) and HubWebUI (the embedded page)
//   ../lib/     the INA3221 and XL9555 drivers, and the JSON library
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include <Adafruit_NeoPixel.h>
#include <DHT20.h>
#include "UM_LCA9555.h"
#include "UM_INA3221.h"
#include "ports/USBPort.h"
#include "web/HubWebServer.h"
#include "settings/HubSettings.h"

// Firmware version, reported by the web UI, the API and 'info'.
#define HUB_FW_VERSION "1.0.0"

#define NUM_PORTS HUB_NUM_PORTS

// ===========================================================================
// Pin map (ESP32-S3)
// ===========================================================================

// Shared I2C bus: I/O expander (0x20), INA3221 x2 (0x40, 0x41), AHT20 (0x38).
#define I2C_SDA 9
#define I2C_SCL 10
#define EXP_INT_PIN 11 // I/O expander INT, active LOW, open-drain
#define BUTTON_PIN 48  // user button, external pull-down, pressed = HIGH
#define LED_PIN 4      // WS2812B data, one LED per port

// INA3221 alert outputs: open-drain with external pull-ups, so they idle HIGH
// and pull LOW when asserted. Only read (by 'mon'); faults come from the load
// switches, not from these.
#define INA1_TC_PIN 21 // INA3221 #1, ports 1-3
#define INA1_CRIT_PIN 14
#define INA1_WARN_PIN 13
#define INA2_TC_PIN 41 // INA3221 #2, ports 4-6
#define INA2_CRIT_PIN 12
#define INA2_WARN_PIN 47

// Hub IC reset, active LOW. Wired, but not used by the firmware.
#define USB_RESET_PIN 2

// 5V present-sense lines. HIGH = present.
#define MAIN_5V_SENSE_PIN 7     // the separate 5V input that powers the ports
#define UPSTREAM_5V_SENSE_PIN 6 // the upstream (host) USB connector

// ===========================================================================
// Hardware objects and global state
// ===========================================================================

Adafruit_NeoPixel strip(NUM_PORTS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Persists every settings group to /settings.json.
HubConfig config;

// Power monitors. Measurement only — over-current faults come from the
// TPS2552 load switches. Channels run high-to-low: CH1 is the chip's highest
// port. Every channel has a 100 mOhm shunt.
INA3221 ina_a; // 0x40, ports 1-3
INA3221 ina_b; // 0x41, ports 4-6
#define INA_SHUNT_OHMS 0.1f

// Temperature / humidity. The AHT20 is register-compatible with the DHT20.
DHT20 aht;
float g_tempC = 0.0f, g_humid = 0.0f;
bool g_tempValid = false; // true after the first good read

// Which optional chips answered at boot. Missing ones are reported as
// unavailable rather than polled.
bool g_ahtOk = false;
bool g_inaAok = false;
bool g_inaBok = false;

// The six downstream ports:  index, sense GPIO, expander enable pin, expander
// fault pin.
USBPort ports[NUM_PORTS] = {
    USBPort(0, 5, 1, 0),
    USBPort(1, 15, 3, 2),
    USBPort(2, 16, 5, 4),
    USBPort(3, 17, 7, 6),
    USBPort(4, 18, 9, 8),
    USBPort(5, 8, 11, 10),
};

static const char *DOW_NAME[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// ===========================================================================
// Port hardware helpers
// ===========================================================================

// Set by the expander interrupt; handled in loop(). Starts true so the first
// pass reads the current fault state. The ISR only sets the flag — the I2C
// read that clears the interrupt happens in serviceExpander().
volatile bool g_expanderIrq = true;

void IRAM_ATTR onExpanderInt() { g_expanderIrq = true; }

// A fault line changed somewhere: read both expander input registers once and
// let every port pick out its own fault bit.
void serviceExpander()
{
    uint8_t reg0 = ioex.read_port(0);
    uint8_t reg1 = ioex.read_port(1);
    for (auto &port : ports)
        port.applyFault(reg0, reg1);
}

bool main5vPresent() { return digitalRead(MAIN_5V_SENSE_PIN) == HIGH; }
bool upstream5vPresent() { return digitalRead(UPSTREAM_5V_SENSE_PIN) == HIGH; }

// One port's measurements. ok = false when that port's INA3221 isn't fitted.
struct PortReading
{
    bool ok;
    float volts, ma, mw;
};

// Read port i (0-based). Ports 1-3 are on ina_a, 4-6 on ina_b, and each chip
// numbers its channels high-to-low (port 1 = CH3 .. port 3 = CH1).
PortReading readPort(uint8_t i)
{
    INA3221 &chip = (i < 3) ? ina_a : ina_b;
    bool present = (i < 3) ? g_inaAok : g_inaBok;
    uint8_t ch = 3 - (i % 3);
    if (!present)
        return {false, 0.0f, 0.0f, 0.0f};
    return {true, chip.bus_voltage_V(ch), chip.current_mA(ch), chip.power_mW(ch)};
}

// Total current and power across all monitored ports.
void totalDraw(float &ma, float &mw)
{
    ma = mw = 0.0f;
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        PortReading r = readPort(i);
        ma += r.ma;
        mw += r.mw;
    }
}

// The port's LED state as a short name — the same decision renderLeds() makes.
// Sent to the web UI so it can show exactly what the LED shows.
const char *portStateStr(USBPort &p)
{
    if (p.isEnabled())
    {
        if (p.hasFault())
            return "fault";
        if (!main5vPresent())
            return p.isOccupied() ? "pink" : "pinkpulse";
        return p.isOccupied() ? "green" : "pulse";
    }
    return p.isOccupied() ? "blue" : "off";
}

// ===========================================================================
// Port LEDs
// ===========================================================================

// Boot sweep: one pass across all six LEDs as soon as settings are loaded —
// green if an SSID is stored, red if not. Nothing waits for it; the port
// states take over the moment it ends.
#define BOOT_SWEEP_MS 700
static uint32_t g_bootSweepAt = 0; // millis() when it started; 0 = not running
static bool g_bootSweepRed = false;

void startBootSweep(bool red)
{
    g_bootSweepRed = red;
    g_bootSweepAt = millis() | 1; // never 0, which means "not running"
    Serial.printf("[led] %lu ms  boot sweep %s\n", (unsigned long)millis(),
                  red ? "RED (no SSID)" : "GREEN (SSID set)");
}

// Draw one frame of the boot sweep. Returns false once it's over.
bool renderBootSweep()
{
    if (!g_bootSweepAt)
        return false;
    uint32_t now = millis();
    uint32_t t = now - g_bootSweepAt;
    if (t >= BOOT_SWEEP_MS)
    {
        g_bootSweepAt = 0;
        Serial.printf("[led] %lu ms  boot sweep done\n", (unsigned long)now);
        return false;
    }
    // The head moves one LED every 100 ms, leaving a short fading trail.
    int head = (int)(t / 100);
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        int d = head - i;
        uint8_t v = (d == 0) ? 255 : (d == 1 ? 90 : (d > 1 ? 25 : 0));
        strip.setPixelColor(i, g_bootSweepRed ? strip.Color(v, 0, 0) : strip.Color(0, v, 0));
    }
    strip.show();
    return true;
}

// 2 s sine pulse from 10% to 100%, shared by every pulsing state so all ports
// stay in step (the web UI mirrors this exact shape).
static float pulseLevel(uint32_t now)
{
    float ph = (now % 2000) / 2000.0f;
    float f = (sinf(ph * 2.0f * PI - PI / 2.0f) + 1.0f) * 0.5f;
    return 0.1f + 0.9f * f;
}

// Draw every port's LED from its live state. Runs every frame because the
// pulse and flash effects are time based.
//
//   powered, over-current         flashing red (250 ms on / off)
//   powered, no main 5V, device   solid pink
//   powered, no main 5V, empty    pulsing pink
//   powered, device               solid green
//   powered, empty                pulsing blue
//   unpowered, device             solid blue
//   unpowered, empty              off
void renderLeds()
{
    // LEDs switched off in settings: blank once, then leave them alone.
    static bool wasCleared = false;
    static int lastBrightness = -1;
    if (!g_leds.config().enabled)
    {
        if (!wasCleared)
        {
            strip.clear();
            strip.show();
            wasCleared = true;
        }
        return;
    }
    wasCleared = false;
    if ((int)g_leds.config().brightness != lastBrightness)
    {
        lastBrightness = g_leds.config().brightness;
        strip.setBrightness((uint8_t)lastBrightness);
    }

    uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        USBPort &port = ports[i];
        uint8_t r = 0, g = 0, b = 0;
        if (port.isEnabled())
        {
            if (port.hasFault())
            {
                if ((now / 250) % 2 == 0)
                    r = 255;
            }
            else if (!main5vPresent())
            {
                float amp = port.isOccupied() ? 1.0f : pulseLevel(now);
                r = (uint8_t)(255 * amp);
                g = (uint8_t)(90 * amp);
                b = (uint8_t)(200 * amp);
            }
            else if (port.isOccupied())
                g = 255;
            else
                b = (uint8_t)(255 * pulseLevel(now));
        }
        else if (port.isOccupied())
            b = 255;
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
}

// ===========================================================================
// Clock
// ===========================================================================

// True once NTP has set the clock (anything after 2023-01-01).
bool timeSynced() { return time(nullptr) > 1672531200; }

// Current local time (the timezone comes from the location settings). False
// until the clock is synced.
bool localNow(struct tm &out)
{
    if (!timeSynced())
        return false;
    time_t t = time(nullptr);
    localtime_r(&t, &out);
    return true;
}

// Minutes since Sunday 00:00 — the scheduler's time axis (0..10079).
int weekMinute(const struct tm &t) { return t.tm_wday * 1440 + t.tm_hour * 60 + t.tm_min; }

// Why the previous boot ended, as a short word.
const char *resetReasonStr(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_USB:       return "USB";        // reset over USB, e.g. flashing
    case ESP_RST_JTAG:      return "JTAG";
    case ESP_RST_EFUSE:     return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default:                return "UNKNOWN";
    }
}

// ===========================================================================
// Power scheduler
//
// Every schedule entry is expanded into switch events on a one-week axis
// (minutes from Sunday 00:00). A port's state at any moment is whatever its
// most recent event said. That one rule covers every mode, makes overlapping
// entries resolve predictably, and lets a reboot work out the correct state.
// ===========================================================================

struct SchedEvent
{
    int at;    // minutes from Sunday 00:00
    bool on;   // switch on or off
    int idx;   // the schedule entry that produced it
    bool tail; // true for the automatic OFF that ends an entry's window
};

// Expand one port's valid, enabled entries into a sorted list of events.
void buildEvents(uint8_t port, std::vector<SchedEvent> &out)
{
    out.clear();
    const auto &list = g_schedules.config().ports[port];
    for (size_t k = 0; k < list.size(); k++)
    {
        const auto &e = list[k];
        if (e.invalid || !e.enabled)
            continue;
        int t = hubParseHHMM(e.time);
        if (t < 0 || e.days == 0)
            continue;

        // ON EVERY: a pulse every `every` minutes, all day or across a window.
        if (e.on && e.mode == 3)
        {
            int iv = max((int)e.every, SCHED_MIN_EVERY);
            int pulse = e.dur > 0 ? e.dur : 1;
            if (pulse >= iv)
                continue; // flagged by validate()
            int from = 0, span = 1440;
            if (!e.allday)
            {
                int u = hubParseHHMM(e.until);
                if (u < 0)
                    continue;
                from = t;
                span = (u - t + 1440) % 1440; // may wrap past midnight
                if (span == 0)
                    continue; // flagged by validate()
            }
            for (int d = 0; d < 7; d++)
            {
                if (!((e.days >> d) & 1))
                    continue;
                for (int off = 0; off < span; off += iv)
                {
                    int at = (d * 1440 + from + off) % 10080;
                    out.push_back({at, true, (int)k, false});
                    out.push_back({(at + pulse) % 10080, false, (int)k, true});
                }
            }
            continue;
        }

        // OFF / ON / ON FOR / ON UNTIL: one event per day, plus the closing
        // OFF for entries with a window.
        int win = hubEntryWindow(e);
        for (int d = 0; d < 7; d++)
        {
            if (!((e.days >> d) & 1))
                continue;
            out.push_back({d * 1440 + t, e.on, (int)k, false});
            if (e.on && win > 0)
                out.push_back({(d * 1440 + t + win) % 10080, false, (int)k, true});
        }
    }
    // Time order; at the same instant ON comes before OFF.
    std::sort(out.begin(), out.end(), [](const SchedEvent &a, const SchedEvent &b) {
        return (a.at != b.at) ? a.at < b.at : a.on > b.on;
    });
}

// Expanded events per port. ON EVERY entries expand into hundreds of events,
// so these are rebuilt only when the schedules change. Only the main loop
// touches them; web requests read the precomputed g_next instead.
static std::vector<SchedEvent> g_evCache[NUM_PORTS];
static uint32_t g_evCacheVer = 0xFFFFFFFFu;

void refreshEventCache()
{
    uint32_t v = g_schedules.version();
    if (v == g_evCacheVer)
        return;
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        buildEvents(i, g_evCache[i]);
        g_evCache[i].shrink_to_fit();
    }
    g_evCacheVer = v;
}

// Each port's next event, worked out on the main loop so answering a web
// request needs no allocation.
struct NextInfo
{
    int at = -1; // minutes from Sunday 00:00; -1 = nothing scheduled
    bool on = false;
    int idx = -1; // the schedule entry that produced it
};
static NextInfo g_next[NUM_PORTS];

void refreshNextInfo(int nowAt)
{
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        const auto &ev = g_evCache[i];
        NextInfo n;
        if (!ev.empty())
        {
            const SchedEvent *nxt = &ev.front(); // nothing later this week = wrap
            for (const auto &e : ev)
                if (e.at > nowAt)
                {
                    nxt = &e;
                    break;
                }
            n.at = nxt->at;
            n.on = nxt->on;
            n.idx = nxt->idx;
        }
        g_next[i] = n;
    }
}

// Fire the events due this minute. Checked once per minute change, so each
// event fires exactly once.
void serviceSchedules()
{
    struct tm tmNow;
    if (!localNow(tmNow))
        return;
    static int lastAt = -1;
    int nowAt = weekMinute(tmNow);
    if (nowAt == lastAt)
        return;
    lastAt = nowAt;
    refreshNextInfo(nowAt);

    bool changed = false;
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        for (const auto &e : g_evCache[i])
        {
            if (e.at != nowAt)
                continue;
            ports[i].setEnabled(e.on);
            Serial.printf("[sched] port %u -> %s\n", i + 1, e.on ? "on" : "off");

            // A one-shot entry disables itself after its LAST event — for an
            // entry with a window that's the closing OFF, so the window runs.
            const auto &entry = g_schedules.config().ports[i][e.idx];
            bool hasWindow = entry.on && hubEntryWindow(entry) > 0;
            if (entry.once && (!hasWindow || e.tail))
            {
                Serial.printf("[sched] port %u: one-shot fired, entry disabled\n", i + 1);
                g_schedules.disableEntry(i, e.idx);
                changed = true;
                break; // this port's events are stale now; rebuilt next pass
            }
        }
    }
    if (changed)
    {
        g_schedules.validate();
        config.save();
    }
}

// Put every scheduled port into the state its schedule says it should be in
// right now, from its most recent past event. Without this a hub that
// rebooted after a scheduled ON would stay off until the next event. Runs once,
// as soon as the clock is synced. Ports with no schedule keep their
// power-on-boot state.
void reconcileSchedules()
{
    struct tm tmNow;
    if (!localNow(tmNow))
        return;
    int nowAt = weekMinute(tmNow);
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        const auto &ev = g_evCache[i];
        if (ev.empty())
            continue;
        // The last event at or before now; if there's none this week, the
        // week wraps and its final event is the one in force.
        const SchedEvent *best = &ev.back();
        for (const auto &e : ev)
        {
            if (e.at > nowAt)
                break;
            best = &e;
        }
        ports[i].setEnabled(best->on);
        Serial.printf("[sched] catch-up: port %u -> %s (last due %d min ago)\n", i + 1,
                      best->on ? "on" : "off", (nowAt - best->at + 10080) % 10080);
    }
}

// A port's next scheduled change, ready to report.
struct NextChange
{
    bool valid = false; // false = nothing scheduled, or the clock isn't synced
    int mins = 0;       // minutes from now
    int secs = 0;       // seconds from now, for countdowns
    bool on = false;    // what it will do
    char at[16] = "";   // when, as "Ddd HH:MM"
    int entry = -1;     // the schedule entry responsible
};

NextChange nextChange(uint8_t i)
{
    NextChange c;
    struct tm tmNow;
    const NextInfo &n = g_next[i];
    if (n.at < 0 || !localNow(tmNow))
        return c;
    c.valid = true;
    c.mins = n.at - weekMinute(tmNow);
    if (c.mins <= 0)
        c.mins += 10080; // next week
    c.secs = max(0, c.mins * 60 - tmNow.tm_sec);
    c.on = n.on;
    c.entry = n.idx;
    snprintf(c.at, sizeof(c.at), "%s %02d:%02d", DOW_NAME[n.at / 1440], (n.at % 1440) / 60, n.at % 60);
    return c;
}

// Number of a port's entries that will actually run (enabled and valid).
int liveScheduleCount(uint8_t i)
{
    int c = 0;
    for (const auto &e : g_schedules.config().ports[i])
        if (e.enabled && !e.invalid)
            c++;
    return c;
}

// ===========================================================================
// JSON for the web UI (/api/status) and the REST API (/api/hub, /api/ports...)
// ===========================================================================

static const char *jb(bool b) { return b ? "true" : "false"; }

// Escape user-entered text (device name, SSID) for a JSON string. Control
// characters are dropped.
String jsonEsc(const String &s)
{
    String out;
    out.reserve(s.length() + 4);
    for (char c : s)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if ((uint8_t)c >= 0x20)
            out += c;
    }
    return out;
}

// "clock":{...}. The REST version adds an ISO timestamp and the UTC offset.
String clockJson(bool rest)
{
    struct tm tmNow;
    bool ok = localNow(tmNow);
    char tbuf[8] = "", dbuf[20] = "", iso[32] = "";
    int off = g_location.config().utc_offset;
    if (ok)
    {
        strftime(tbuf, sizeof(tbuf), "%H:%M", &tmNow);
        strftime(dbuf, sizeof(dbuf), "%a %d %b %Y", &tmNow);
        char stamp[20];
        strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &tmNow);
        snprintf(iso, sizeof(iso), "%s%+03d:00", stamp, off);
    }
    String j = "\"clock\":{\"synced\":" + String(jb(ok));
    if (rest)
        j += ",\"iso\":\"" + String(iso) + "\"";
    j += ",\"time\":\"" + String(tbuf) + "\",\"date\":\"" + String(dbuf) + "\"";
    // Weekday and seconds since midnight drive the web timeline's "now" line.
    j += ",\"wday\":" + String(ok ? tmNow.tm_wday : 0);
    j += ",\"secs\":" + String(ok ? tmNow.tm_hour * 3600 + tmNow.tm_min * 60 + tmNow.tm_sec : 0);
    if (rest)
        j += ",\"utc_offset\":" + String(off);
    return j + "}";
}

// GET /api/status — everything the web page shows, in one object.
String buildStatusJson()
{
    float totalMa, totalMw;
    totalDraw(totalMa, totalMw);
    bool up = WiFi.status() == WL_CONNECTED;

    String j = "{\"name\":\"" + jsonEsc(g_device.config().device_name) + "\"";
    j += ",\"fw\":\"" HUB_FW_VERSION "\"";
    j += ",\"rst\":\"" + String(resetReasonStr(esp_reset_reason())) + "\"";
    j += ",\"theme\":\"" + g_website.config().theme + "\"";
    j += ",\"ip\":\"" + (up ? WiFi.localIP().toString() : String()) + "\"";
    j += ",\"uptime_s\":" + String(millis() / 1000);
    j += ",\"heap\":" + String((uint32_t)ESP.getFreeHeap());
    j += ",\"maxblk\":" + String((uint32_t)ESP.getMaxAllocHeap());
    j += ",\"main5v\":" + String(jb(main5vPresent()));
    j += ",\"upstream5v\":" + String(jb(upstream5vPresent()));
    j += ",\"temp\":{\"valid\":" + String(jb(g_tempValid));
    j += ",\"c\":" + String(g_tempC, 1) + ",\"h\":" + String(g_humid, 1) + "}";
    j += "," + clockJson(false);

    j += ",\"ports\":[";
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        USBPort &port = ports[i];
        PortReading r = readPort(i);
        if (i)
            j += ",";
        j += "{\"n\":" + String(i + 1);
        j += ",\"enabled\":" + String(jb(port.isEnabled()));
        j += ",\"occupied\":" + String(jb(port.isOccupied()));
        j += ",\"fault\":" + String(jb(port.hasFault()));
        j += ",\"state\":\"" + String(portStateStr(port)) + "\"";
        j += ",\"name\":\"" + jsonEsc(g_ports.config().names[i]) + "\"";
        j += ",\"mon\":" + String(jb(r.ok));
        j += ",\"v\":" + String(r.volts, 2);
        j += ",\"ma\":" + String(r.ma, 1);
        j += ",\"mw\":" + String(r.mw, 1);
        j += ",\"sched\":" + String(liveScheduleCount(i));
        NextChange nc = nextChange(i);
        if (nc.valid)
        {
            j += ",\"next\":{\"in\":" + String(nc.secs);
            j += ",\"on\":" + String(jb(nc.on));
            j += ",\"at\":\"" + String(nc.at) + "\"";
            j += ",\"i\":" + String(nc.entry) + "}";
        }
        j += "}";
    }
    j += "]";
    j += ",\"total\":{\"ma\":" + String(totalMa, 1) + ",\"mw\":" + String(totalMw, 1) + "}";
    return j + "}";
}

// A schedule entry's mode as an API word.
const char *schedModeStr(const HubScheduleEntry &e)
{
    if (!e.on)
        return "off";
    switch (e.mode)
    {
    case 1:  return "on_for";
    case 2:  return "on_until";
    case 3:  return "on_every";
    default: return "on";
    }
}

// {"in_s":..,"on":..,"at":"Wed 16:35","entry":0}, or null if nothing's due.
String nextJson(uint8_t i)
{
    NextChange nc = nextChange(i);
    if (!nc.valid)
        return "null";
    String j = "{\"in_s\":" + String(nc.secs);
    j += ",\"on\":" + String(jb(nc.on));
    j += ",\"at\":\"" + String(nc.at) + "\"";
    j += ",\"entry\":" + String(nc.entry) + "}";
    return j;
}

// A port's schedule entries in the REST format (mode as a word).
String scheduleEntriesJson(uint8_t i)
{
    String j = "[";
    const auto &list = g_schedules.config().ports[i];
    for (size_t k = 0; k < list.size(); k++)
    {
        const auto &e = list[k];
        if (k)
            j += ",";
        j += "{\"days\":" + String(e.days);
        j += ",\"time\":\"" + e.time + "\"";
        j += ",\"mode\":\"" + String(schedModeStr(e)) + "\"";
        j += ",\"dur\":" + String(e.dur);
        j += ",\"until\":\"" + e.until + "\"";
        j += ",\"every\":" + String(e.every);
        j += ",\"allday\":" + String(jb(e.allday));
        j += ",\"once\":" + String(jb(e.once));
        j += ",\"enabled\":" + String(jb(e.enabled));
        j += ",\"invalid\":" + String(jb(e.invalid));
        if (e.invalid)
            j += ",\"invalid_reason\":\"" + String(hubInvalidReason(e.why)) + "\"";
        j += "}";
    }
    return j + "]";
}

// One port for the REST API. `detail` adds power_on_boot and the full list of
// schedule entries.
String portJson(uint8_t i, bool detail)
{
    USBPort &port = ports[i];
    PortReading r = readPort(i);
    String j = "{\"n\":" + String(i + 1);
    j += ",\"enabled\":" + String(jb(port.isEnabled()));
    j += ",\"occupied\":" + String(jb(port.isOccupied()));
    j += ",\"fault\":" + String(jb(port.hasFault()));
    j += ",\"state\":\"" + String(portStateStr(port)) + "\"";
    j += ",\"monitor\":" + String(jb(r.ok));
    j += ",\"volts\":" + String(r.volts, 2);
    j += ",\"ma\":" + String(r.ma, 1);
    j += ",\"mw\":" + String(r.mw, 1);
    if (detail)
        j += ",\"power_on_boot\":" + String(jb(g_ports.config().power_on_boot[i]));
    j += ",\"schedule\":{\"count\":" + String(liveScheduleCount(i));
    j += ",\"next\":" + nextJson(i);
    if (detail)
        j += ",\"entries\":" + scheduleEntriesJson(i);
    return j + "}}";
}

// GET /api/ports
String apiPortsJson()
{
    String j = "{\"ports\":[";
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        if (i)
            j += ",";
        j += portJson(i, false);
    }
    return j + "]}";
}

// GET /api/ports/{n}, n = 1..6
String apiPortJson(int n) { return portJson(n - 1, true); }

// GET /api/ports/{n}/schedule, n = 1..6
String apiPortScheduleJson(int n)
{
    uint8_t i = n - 1;
    String j = "{\"n\":" + String(n);
    j += ",\"count\":" + String(liveScheduleCount(i));
    j += ",\"next\":" + nextJson(i);
    j += ",\"entries\":" + scheduleEntriesJson(i);
    return j + "}";
}

// GET /api/hub — everything that isn't per port.
String apiHubJson()
{
    float totalMa, totalMw;
    totalDraw(totalMa, totalMw);
    int powered = 0, occupied = 0, faulted = 0;
    for (auto &p : ports)
    {
        powered += p.isEnabled();
        occupied += p.isOccupied();
        faulted += p.hasFault();
    }
    bool up = WiFi.status() == WL_CONNECTED;

    String j = "{\"name\":\"" + jsonEsc(g_device.config().device_name) + "\"";
    j += ",\"fw\":\"" HUB_FW_VERSION "\"";
    j += ",\"uptime_s\":" + String(millis() / 1000);
    j += ",\"reset_reason\":\"" + String(resetReasonStr(esp_reset_reason())) + "\"";
    j += ",\"power\":{\"main_5v\":" + String(jb(main5vPresent()));
    j += ",\"upstream_5v\":" + String(jb(upstream5vPresent()));
    j += ",\"total_ma\":" + String(totalMa, 1);
    j += ",\"total_mw\":" + String(totalMw, 1) + "}";
    j += ",\"sensors\":{\"temp_c\":" + String(g_tempC, 1);
    j += ",\"humidity\":" + String(g_humid, 1);
    j += ",\"valid\":" + String(jb(g_tempValid)) + "}";
    j += "," + clockJson(true);
    j += ",\"network\":{\"connected\":" + String(jb(up));
    j += ",\"ip\":\"" + (up ? WiFi.localIP().toString() : String()) + "\"";
    j += ",\"ssid\":\"" + jsonEsc(g_wifi.config().ssid) + "\"";
    j += ",\"rssi\":" + String(up ? WiFi.RSSI() : 0);
    j += ",\"hostname\":\"" + g_device.hostname() + "\"}";
    j += ",\"devices\":{\"expander\":" + String(jb(ioex.connected()));
    j += ",\"ina_1\":" + String(jb(g_inaAok));
    j += ",\"ina_2\":" + String(jb(g_inaBok));
    j += ",\"aht20\":" + String(jb(g_ahtOk)) + "}";
    j += ",\"memory\":{\"heap_free\":" + String((uint32_t)ESP.getFreeHeap());
    j += ",\"heap_max_block\":" + String((uint32_t)ESP.getMaxAllocHeap()) + "}";
    j += ",\"ports_summary\":{\"count\":" + String(NUM_PORTS);
    j += ",\"powered\":" + String(powered);
    j += ",\"occupied\":" + String(occupied);
    j += ",\"faulted\":" + String(faulted) + "}";
    return j + "}";
}

// ===========================================================================
// Network: WiFi, web server, OTA, mDNS
//
// Nothing here blocks boot — the ports work from power-up whether or not the
// network ever comes up. The WiFi events only set flags; the work happens in
// loop().
// ===========================================================================

volatile bool g_gotIP = false;
volatile bool g_wifiLost = false;
volatile uint8_t g_wifiReason = 0;

// Over-the-air updates (pio run -e ota -t upload). ArduinoOTA also starts mDNS
// under the hostname, which is what makes <hostname>.local resolve. Started
// once; later calls do nothing.
void startOTA()
{
    static bool started = false;
    if (started)
        return;
    started = true;
    ArduinoOTA.setHostname(g_device.hostname().c_str());
    ArduinoOTA.onStart([]() { Serial.println("[ota] update starting"); });
    ArduinoOTA.onEnd([]() { Serial.println("\n[ota] update complete"); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) { Serial.printf("[ota] %u%%\r", t ? (p * 100) / t : 0); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] error %u\n", e); });
    ArduinoOTA.begin();
    MDNS.addService("http", "tcp", 80); // advertise the web UI as well
    Serial.printf("[ota] ready — http://%s.local/\n", g_device.hostname().c_str());
}

// Start (or restart) a background connection with the stored settings.
// Returns at once; GOT_IP arrives as an event.
void connectWiFi()
{
    const HubWiFiConfig &w = g_wifi.config();
    if (w.ssid.length() == 0)
    {
        Serial.println("[wifi] no SSID stored — not connecting");
        if (WiFi.isConnected())
            WiFi.disconnect(false, false);
        return;
    }
    Serial.printf("[wifi] connecting to '%s' in background...\n", w.ssid.c_str());
    WiFi.setHostname(g_device.hostname().c_str()); // the name the router sees
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);      // credentials live in our settings, not NVS
    WiFi.setAutoReconnect(true); // the stack retries on its own as well
    if (WiFi.isConnected())
        WiFi.disconnect(false, false); // reconnecting with changed settings

    // Static address when DHCP is off and all three fields parse, with DNS
    // pointed at the gateway. Anything incomplete falls back to DHCP rather
    // than leaving the hub unreachable.
    IPAddress ip, gw, sn;
    if (!w.dhcp && ip.fromString(w.ip) && gw.fromString(w.gateway) && sn.fromString(w.subnet))
    {
        WiFi.config(ip, gw, sn, gw);
        Serial.printf("[wifi] static address %s\n", w.ip.c_str());
    }
    else
    {
        if (!w.dhcp)
            Serial.println("[wifi] static address incomplete or invalid — using DHCP");
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
    WiFi.begin(w.ssid.c_str(), w.password.c_str());
}

// Got an IP: start the web server and OTA (each only once), and tell the
// settings groups the network is up (location starts NTP).
void startNetServices()
{
    Serial.printf("[wifi] connected — IP %s\n", WiFi.localIP().toString().c_str());
    webServer.setRest({apiHubJson, apiPortsJson, apiPortJson, apiPortScheduleJson,
                       [](bool on) {
                           for (auto &p : ports)
                               p.setEnabled(on);
                       }});
    webServer.begin(
        buildStatusJson,
        [](const String &t) {
            g_website.setTheme(t);
            config.save();
        },
        [](int n, bool on) {
            if (n >= 1 && n <= NUM_PORTS)
                ports[n - 1].setEnabled(on);
        },
        [](int n, const String &name) {
            if (n >= 1 && n <= NUM_PORTS)
                g_ports.setName(n - 1, name);
        },
        []() { return g_schedules.toJsonString(); },
        [](const String &body) {
            if (g_schedules.setFromJson(body))
                config.save();
        },
        []() { config.save(); });
    Serial.printf("[web] http://%s/\n", WiFi.localIP().toString().c_str());
    startOTA();
    HubRegistry::dispatchNetUp(WiFi.localIP().toString().c_str());
}

// Keep the connection up. The stack's auto-reconnect handles most drops; this
// is the backstop for when it gives up (router rebooted, out of range for a
// while): after 15 s down, force a fresh connection every 30 s.
void serviceWiFi()
{
    if (g_wifiLost)
    {
        g_wifiLost = false;
        Serial.printf("[wifi] disconnected (reason %u) — will retry\n", g_wifiReason);
    }
    if (g_wifi.config().ssid.length() == 0)
        return;

    static uint32_t lastTry = 0;
    static uint32_t downSince = 0;
    if (WiFi.status() == WL_CONNECTED)
    {
        downSince = 0;
        return;
    }
    uint32_t now = millis();
    if (downSince == 0)
        downSince = now;
    if (now - downSince < 15000 || now - lastTry < 30000)
        return;
    lastTry = now;
    Serial.println("[wifi] still down — forcing reconnect");
    WiFi.disconnect(false, false);
    WiFi.begin(g_wifi.config().ssid.c_str(), g_wifi.config().password.c_str());
}

// ===========================================================================
// Serial console — type 'help'
// ===========================================================================

// An INA3221 alert line's state (active LOW).
const char *alertStr(uint8_t pin) { return digitalRead(pin) == LOW ? "ASSERT" : "clear"; }

const char *wifiStateStr()
{
    switch (WiFi.status())
    {
    case WL_CONNECTED:       return "connected";
    case WL_NO_SSID_AVAIL:   return "SSID not found";
    case WL_CONNECT_FAILED:  return "connect failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "disconnected";
    case WL_IDLE_STATUS:     return "idle";
    default:                 return "unknown";
    }
}

// 'info' — the hub's whole current state in one read-only dump.
void printInfo()
{
    uint32_t up = millis() / 1000;
    Serial.println("---- Smart USB Hub ----------------------------------------");
    Serial.printf("firmware   : %s\n", HUB_FW_VERSION);
    Serial.printf("last boot  : %s\n", resetReasonStr(esp_reset_reason()));
    Serial.printf("uptime     : %lud %luh %lum %lus\n", (unsigned long)(up / 86400),
                  (unsigned long)(up % 86400 / 3600), (unsigned long)(up % 3600 / 60), (unsigned long)(up % 60));
    Serial.printf("heap free  : %u bytes (largest block %u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

    Serial.println("-- network ------------------------------------------------");
    const HubWiFiConfig &w = g_wifi.config();
    Serial.printf("name       : %s\n", g_device.config().device_name.c_str());
    Serial.printf("hostname   : %s.local\n", g_device.hostname().c_str());
    Serial.printf("addressing : %s\n", w.dhcp ? "DHCP" : "static");
    Serial.printf("ssid       : %s\n", w.ssid.length() ? w.ssid.c_str() : "<not set>");
    Serial.printf("password   : %s\n", w.password.length() ? "set" : "<not set>");
    Serial.printf("state      : %s\n", wifiStateStr());
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("ip         : %s   (gw %s)\n", WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str());
        Serial.printf("rssi       : %d dBm\n", WiFi.RSSI());
        Serial.printf("web ui     : http://%s/\n", WiFi.localIP().toString().c_str());
    }
    Serial.printf("mac        : %s\n", WiFi.macAddress().c_str());

    Serial.println("-- time ---------------------------------------------------");
    struct tm tmNow;
    if (localNow(tmNow))
    {
        char buf[40];
        strftime(buf, sizeof(buf), "%a %d %b %Y %H:%M:%S", &tmNow);
        Serial.printf("local time : %s\n", buf);
    }
    else
        Serial.println("local time : not synced");
    const HubLocationConfig &loc = g_location.config();
    Serial.printf("location   : %s %s  UTC%+d\n", loc.city.c_str(), loc.country.c_str(), loc.utc_offset);
    Serial.printf("ntp server : %s\n", loc.ntp_server.c_str());

    Serial.println("-- power rails --------------------------------------------");
    Serial.printf("main 5V    : %s\n", main5vPresent() ? "present" : "ABSENT");
    Serial.printf("upstream 5V: %s\n", upstream5vPresent() ? "present" : "ABSENT");

    Serial.println("-- devices ------------------------------------------------");
    Serial.printf("expander   : %s\n", ioex.connected() ? "ok" : "NOT RESPONDING");
    Serial.printf("ina3221 #1 : %s (ports 1-3)\n", g_inaAok ? "ok" : "not found");
    Serial.printf("ina3221 #2 : %s (ports 4-6)\n", g_inaBok ? "ok" : "not found");
    if (g_ahtOk)
        Serial.printf("aht20      : ok  %.1f C  %.1f %%RH\n", g_tempC, g_humid);
    else
        Serial.println("aht20      : not found");
    Serial.printf("led bright : %u/255%s\n", (unsigned)g_leds.config().brightness,
                  g_leds.config().enabled ? "" : "  (LEDs disabled)");

    Serial.println("-- ports --------------------------------------------------");
    Serial.println(" #  power  device  fault  state       volts    current    power   next change");
    float totalMa = 0.0f, totalMw = 0.0f;
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        USBPort &port = ports[i];
        PortReading r = readPort(i);
        char meas[40];
        if (r.ok)
            snprintf(meas, sizeof(meas), "%6.2fV %8.1fmA %8.1fmW", r.volts, r.ma, r.mw);
        else
            snprintf(meas, sizeof(meas), "%s", "   no monitor fitted   ");
        totalMa += r.ma;
        totalMw += r.mw;

        char next[40] = "-";
        NextChange nc = nextChange(i);
        if (nc.valid)
            snprintf(next, sizeof(next), "%s in %dm (%s)", nc.on ? "ON" : "OFF", nc.mins, nc.at);

        Serial.printf(" %u  %-5s  %-6s  %-5s  %-10s  %s  %s\n", i + 1, port.isEnabled() ? "ON" : "off",
                      port.isOccupied() ? "yes" : "-", port.hasFault() ? "FAULT" : "-",
                      portStateStr(port), meas, next);
    }
    Serial.printf("total draw : %.1f mA  %.1f mW\n", totalMa, totalMw);

    // Live entries per port, with disabled / invalid ones counted separately.
    Serial.print("schedules  :");
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        int live = liveScheduleCount(i);
        int other = (int)g_schedules.config().ports[i].size() - live;
        Serial.printf("  p%u:%d", i + 1, live);
        if (other)
            Serial.printf("(+%d off/bad)", other);
    }
    Serial.println();
    Serial.println("-----------------------------------------------------------");
}

// Run one console command.
void handleSerialCommand(String cmd)
{
    cmd.trim();
    if (cmd.startsWith("set_ssid "))
    {
        String v = cmd.substring(9);
        v.trim();
        g_wifi.setCredentials(v, g_wifi.config().password); // saves
        Serial.printf("[cmd] ssid set to '%s' — type 'wifi' to connect\n", v.c_str());
    }
    else if (cmd.startsWith("set_pass "))
    {
        String v = cmd.substring(9);
        v.trim();
        g_wifi.setCredentials(g_wifi.config().ssid, v); // saves
        Serial.println("[cmd] password set — type 'wifi' to connect");
    }
    else if (cmd.startsWith("port "))
    {
        // port <1-6> on|off
        int sp = cmd.indexOf(' ', 5);
        int p = (sp > 0) ? cmd.substring(5, sp).toInt() : 0;
        String state = (sp > 0) ? cmd.substring(sp + 1) : "";
        state.trim();
        if (p >= 1 && p <= NUM_PORTS && (state == "on" || state == "off"))
        {
            ports[p - 1].setEnabled(state == "on");
            Serial.printf("[cmd] port %d %s\n", p, state.c_str());
        }
        else
            Serial.println("[cmd] usage: port <1-6> on|off");
    }
    else if (cmd == "mon")
    {
        for (uint8_t i = 0; i < NUM_PORTS; i++)
        {
            PortReading r = readPort(i);
            if (r.ok)
                Serial.printf("  port %u: %.3f V  %.1f mA  %.1f mW\n", i + 1, r.volts, r.ma, r.mw);
            else
                Serial.printf("  port %u: monitor not present\n", i + 1);
        }
        Serial.printf("  INA1 alerts  TC:%s CRIT:%s WARN:%s\n",
                      alertStr(INA1_TC_PIN), alertStr(INA1_CRIT_PIN), alertStr(INA1_WARN_PIN));
        Serial.printf("  INA2 alerts  TC:%s CRIT:%s WARN:%s\n",
                      alertStr(INA2_TC_PIN), alertStr(INA2_CRIT_PIN), alertStr(INA2_WARN_PIN));
    }
    else if (cmd == "info")
        printInfo();
    else if (cmd == "wifi")
        connectWiFi();
    else if (cmd == "restart")
    {
        Serial.println("[cmd] restarting...");
        delay(100);
        ESP.restart();
    }
    else if (cmd == "help")
    {
        Serial.println("Commands:");
        Serial.println("  info               - full hub status (wifi, rails, ports, devices)");
        Serial.println("  port <1-6> on|off  - turn a port's 5V on/off");
        Serial.println("  mon                - per-port V/mA/mW + INA alerts");
        Serial.println("  set_ssid <ssid>    - store WiFi SSID");
        Serial.println("  set_pass <pass>    - store WiFi password");
        Serial.println("  wifi               - connect WiFi with stored creds");
        Serial.println("  restart            - reboot the ESP32-S3");
        Serial.println("  help               - this list");
    }
    else if (cmd.length())
        Serial.printf("[cmd] unknown: '%s'\n", cmd.c_str());
}

// Collect typed characters into a line and run it on Enter. Characters are
// echoed back (the USB serial port doesn't echo), and backspace works.
void processSerial()
{
    static String line;
    while (Serial.available())
    {
        char c = Serial.read();
        if (c == '\n' || c == '\r')
        {
            Serial.println();
            if (line.length())
            {
                handleSerialCommand(line);
                line = "";
            }
        }
        else if (c == '\b' || c == 127)
        {
            if (line.length())
            {
                line.remove(line.length() - 1);
                Serial.print("\b \b");
            }
        }
        else
        {
            line += c;
            Serial.write(c);
        }
        Serial.flush(); // send the echo now rather than when the buffer fills
    }
}

// ===========================================================================
// setup / loop
// ===========================================================================

void setup()
{
    Serial.begin(115200);
    // Reported first: the reset reason survives the reset, so this shows a
    // brownout or crash even when a restart loop is too quick to catch.
    Serial.printf("[boot] reset reason: %d (%s)\n", (int)esp_reset_reason(), resetReasonStr(esp_reset_reason()));

    // LEDs stay dark until settings (and so brightness) are loaded.
    strip.begin();
    strip.clear();
    strip.show();

    Wire.begin(I2C_SDA, I2C_SCL);

    // The expander drives every port's power enable and reads its fault line —
    // without it there's no port control at all.
    if (ioex.begin())
        Serial.println("[ioex] XL9555 present and responding at 0x20");
    else
        Serial.printf("[ioex] XL9555 NOT responding at 0x20 (I2C error %u) — "
                      "port power + fault control are DEAD\n",
                      ioex.last_error());

    // Power monitors: set the shunt on every channel, then check each chip
    // answers with the right manufacturer ID.
    for (uint8_t ch = 1; ch <= 3; ch++)
    {
        ina_a.set_shunt_resistor(ch, INA_SHUNT_OHMS);
        ina_b.set_shunt_resistor(ch, INA_SHUNT_OHMS);
    }
    g_inaAok = ina_a.begin(INA3221_ADDR_GND);
    if (g_inaAok)
        Serial.println("[ina] INA3221 #1 (ports 1-3) present at 0x40");
    else
        Serial.printf("[ina] INA3221 #1 (ports 1-3) NOT found at 0x40 (err %u) — monitoring off for ports 1-3\n",
                      ina_a.last_error());
    g_inaBok = ina_b.begin(INA3221_ADDR_VS);
    if (g_inaBok)
        Serial.println("[ina] INA3221 #2 (ports 4-6) present at 0x41");
    else
        Serial.printf("[ina] INA3221 #2 (ports 4-6) NOT found at 0x41 (err %u) — monitoring off for ports 4-6\n",
                      ina_b.last_error());

    // Some DHT20 library versions call Wire.begin() with no pins inside
    // begin(), moving the bus to the default pins — so put it back afterwards.
    aht.begin();
    Wire.begin(I2C_SDA, I2C_SCL);
    g_ahtOk = aht.isConnected();
    Serial.println(g_ahtOk ? "[aht] AHT20 present at 0x38" : "[aht] AHT20 NOT found at 0x38 — temperature/humidity off");

    config.begin();         // mount LittleFS and load /settings.json
    g_schedules.validate(); // flag bad entries before anything can fire

    strip.setBrightness(g_leds.config().brightness);
    startBootSweep(g_wifi.config().ssid.length() == 0);

    // Every port starts off; only those flagged power-on-boot come up.
    for (uint8_t i = 0; i < NUM_PORTS; i++)
    {
        ports[i].begin();
        if (g_ports.config().power_on_boot[i])
            ports[i].enable();
    }

    pinMode(EXP_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(EXP_INT_PIN), onExpanderInt, FALLING);
    pinMode(BUTTON_PIN, INPUT);
    pinMode(MAIN_5V_SENSE_PIN, INPUT);
    pinMode(UPSTREAM_5V_SENSE_PIN, INPUT);
    for (uint8_t p : {INA1_TC_PIN, INA1_CRIT_PIN, INA1_WARN_PIN, INA2_TC_PIN, INA2_CRIT_PIN, INA2_WARN_PIN})
        pinMode(p, INPUT);

    // WiFi runs entirely in the background; everything above is already live.
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { g_gotIP = true; },
                 ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(
        [](arduino_event_id_t, arduino_event_info_t info) {
            g_wifiReason = info.wifi_sta_disconnected.reason;
            g_wifiLost = true;
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    g_wifi.consumeChanged(); // loading settings isn't a change to react to
    connectWiFi();
    Serial.println("[boot] setup done");
}

void loop()
{
    processSerial();
    ArduinoOTA.handle();

    // ---- network ----
    if (g_gotIP)
    {
        g_gotIP = false;
        startNetServices();
    }
    if (g_wifi.consumeChanged()) // WiFi settings edited — reconnect with them
        connectWiFi();
    serviceWiFi();

    // ---- port hardware ----
    if (g_expanderIrq)
    {
        g_expanderIrq = false;
        serviceExpander();
    }
    for (auto &port : ports)
        if (port.senseDirty())
            port.serviceSense();

    // ---- settings groups (location lookup) and scheduler ----
    HubRegistry::dispatchLoop();

    bool scheduleChanged = (g_schedules.version() != g_evCacheVer);
    refreshEventCache();
    // As soon as the clock is valid, put ports where their schedules say they
    // should be — covers changes missed while the hub was off.
    static bool reconciled = false;
    if (!reconciled && timeSynced())
    {
        reconciled = true;
        reconcileSchedules();
        scheduleChanged = true;
    }
    struct tm tmNow;
    if (scheduleChanged && localNow(tmNow))
        refreshNextInfo(weekMinute(tmNow));
    serviceSchedules();

    // ---- temperature: every 2 s (the sensor needs >= 1 s between reads) ----
    static uint32_t lastTemp = 0;
    if (g_ahtOk && millis() - lastTemp >= 2000)
    {
        lastTemp = millis();
        if (aht.read() == DHT20_OK)
        {
            g_tempC = aht.getTemperature();
            g_humid = aht.getHumidity();
            g_tempValid = true;
        }
    }

    // ---- button: each press flips every port to its opposite state ----
    static int lastBtn = LOW;
    static uint32_t lastBtnMs = 0;
    int btn = digitalRead(BUTTON_PIN);
    if (btn != lastBtn && millis() - lastBtnMs > 30) // 30 ms debounce
    {
        lastBtnMs = millis();
        lastBtn = btn;
        if (btn == HIGH)
            for (auto &port : ports)
                port.setEnabled(!port.isEnabled());
    }

    // ---- LEDs at ~50 fps: boot sweep first, then the port states ----
    static uint32_t lastFrame = 0;
    uint32_t now = millis();
    if (now - lastFrame >= 20)
    {
        lastFrame = now;
        if (!renderBootSweep())
            renderLeds();
    }
}

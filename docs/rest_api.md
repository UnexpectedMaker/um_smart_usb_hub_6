# Smart USB Hub — REST API

The hub has a JSON API over HTTP, so scripts, test rigs and home automation can
do everything the web page does: switch ports, read each port's voltage,
current and power, check for devices and faults, and change settings and
schedules.

Every endpoint below is documented with a **complete example payload** — what
the device actually sends, not an abridged shape. Writes show the request and
the exact response body. Ready-to-use code for curl, Python, JavaScript and Home
Assistant is in [Examples](#examples).

**Contents:** [Getting started](#getting-started) ·
[Conventions](#conventions) · [Endpoint summary](#endpoint-summary) ·
[Reads](#reads) · [Writes](#writes) · [Examples](#examples) ·
[The schedule entry object](#the-schedule-entry-object) ·
[Current limitations](#current-limitations)

---

## Getting started

### The hub's address

The API lives on the same address as the web page:

- **`http://usb-hub.local`** — the default hostname. You can change it in the
  web page's Settings (Device → Hostname); the new name applies after a restart.
- **The hub's IP address** — use this if `.local` names don't resolve on your
  machine. The serial console's `info` command shows it, and so does your
  router.

All the examples here use `http://usb-hub.local`. Swap in your own address.

### No key or login

There's no authentication: like the web page, the API is open to anything on
the same network. Keep the hub on a network you trust.

### Try it

```sh
curl http://usb-hub.local/api/hub
```

You should get back a JSON object with the hub's name, firmware version,
temperature, total current draw and more.

### Which endpoint do I want?

| To… | Use |
|---|---|
| Read the hub as a whole (power, temperature, clock, network) | [`GET /api/hub`](#get-apihub) |
| Read all six ports | [`GET /api/ports`](#get-apiports) |
| Read one port in detail, with its schedule | [`GET /api/ports/{n}`](#get-apiportsn) |
| Switch one port on or off | [`POST /api/port?n=&on=`](#post-apiportn1-6on01) |
| Rename one port in the web UI | [`POST /api/port/name?n=`](#post-apiportnamen1-6) |
| Switch every port on or off | [`POST /api/hub/ports?on=`](#post-apihubportson01) |
| Choose which ports power up at boot | [`POST /api/settings`](#post-apisettings) with `{"ports":{"power_on_boot":[...]}}` |
| Change any other setting | [`POST /api/settings`](#post-apisettings) |
| Replace the schedules | [`POST /api/schedules`](#post-apischedules) |

---

## Conventions

- **Base path**: `/api`
- **Responses**: `application/json`, UTF-8. Always a JSON **object** at the top
  level (never a bare array) so fields can be added without breaking clients.
- **Simple actions** take **query parameters** (easy from `curl`, shell, Home
  Assistant). **Bulk data** (schedules, settings) takes a **JSON body**.
- **Port numbers** are `1..6`, matching the silkscreen and the LEDs.
- **No authentication.** The API is open to anyone on the LAN.
- **Time** is device-local, resolved from the `location` UTC offset + NTP.
  All schedule times are local, 24-hour `"HH:MM"`.
- Floats are emitted with fixed precision: volts 2 dp, mA/mW 1 dp, temperature
  and humidity 1 dp.

### Status codes

| Code | Meaning |
|---|---|
| `200` | OK |
| `400` | Missing or unparseable parameter (only `/api/hub/ports` returns this) |
| `404` | Unknown endpoint, or a port outside `1..6` |

### Error bodies

Unknown path, including a port outside `1..6` (`/api/ports/9`):

```json
{ "error": "not found" }
```

Bad `on` parameter on `/api/hub/ports`:

```json
{ "error": "bad param", "detail": "on must be 0 or 1" }
```

Everything else is lenient: a malformed `/api/port` query, bad JSON on
`/api/schedules`, or bad JSON on `/api/settings` all return `200` and silently
apply nothing. See *Current limitations*.

---

## Endpoint summary

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/hub` | Whole-hub snapshot (power, sensors, network, time, devices) |
| `GET` | `/api/ports` | State of all six ports |
| `GET` | `/api/ports/{n}` | One port in detail, including its schedule |
| `GET` | `/api/ports/{n}/schedule` | One port's schedule |
| `GET` | `/api/status` | Combined payload backing the web UI |
| `GET` | `/api/schedules` | All six ports' schedules |
| `GET` | `/api/settings/schema` | Settings structure + current values |
| `POST` | `/api/port?n=&on=` | Switch one port |
| `POST` | `/api/port/name?n=` | Rename one port |
| `POST` | `/api/schedules` | Replace all schedules |
| `POST` | `/api/settings` | Update one or more settings groups |
| `POST` | `/api/theme?value=` | Store the UI theme |
| `POST` | `/api/location/detect` | Trigger IP geolocation lookup |
| `POST` | `/api/hub/ports?on=` | Set **all** ports on/off at once |

Static assets, also served from flash: `GET /`, `GET /hub.css`, `GET /hub.js`.

**Two schedule representations exist.** The REST endpoints (`/api/ports/...`)
emit `mode` as a **string** and omit the `on` boolean. The web-UI endpoints
(`/api/status`, `/api/schedules`) emit `mode` as an **integer** alongside `on`.
Both are documented below. Don't mix them.

---

# Reads

## GET `/api/hub`

Everything that isn't per-port.

```
curl http://192.168.1.100/api/hub
```

```json
{
  "name": "Smart USB Hub",
  "fw": "1.0.0",
  "uptime_s": 838,
  "reset_reason": "POWERON",
  "power": {
    "main_5v": true,
    "upstream_5v": true,
    "total_ma": 452.0,
    "total_mw": 2260.0
  },
  "sensors": {
    "temp_c": 30.8,
    "humidity": 29.6,
    "valid": true
  },
  "clock": {
    "synced": true,
    "iso": "2026-08-02T16:33:38+10:00",
    "time": "16:33",
    "date": "Sun 02 Aug 2026",
    "wday": 0,
    "secs": 59618,
    "utc_offset": 10
  },
  "network": {
    "connected": true,
    "ip": "192.168.1.100",
    "ssid": "MyNetwork",
    "rssi": -54,
    "hostname": "usb-hub"
  },
  "devices": {
    "expander": true,
    "ina_1": true,
    "ina_2": true,
    "aht20": true
  },
  "memory": { "heap_free": 209800, "heap_max_block": 163828 },
  "ports_summary": { "count": 6, "powered": 1, "occupied": 0, "faulted": 0 }
}
```

When the clock hasn't synced and WiFi is down, the same request returns:

```json
{
  "name": "Smart USB Hub",
  "fw": "1.0.0",
  "uptime_s": 7,
  "reset_reason": "POWERON",
  "power": { "main_5v": true, "upstream_5v": false, "total_ma": 0.0, "total_mw": 0.0 },
  "sensors": { "temp_c": 0.0, "humidity": 0.0, "valid": false },
  "clock": { "synced": false, "iso": "", "time": "", "date": "", "wday": 0, "secs": 0, "utc_offset": 10 },
  "network": { "connected": false, "ip": "", "ssid": "MyNetwork", "rssi": 0, "hostname": "usb-hub" },
  "devices": { "expander": true, "ina_1": true, "ina_2": true, "aht20": true },
  "memory": { "heap_free": 231044, "heap_max_block": 163828 },
  "ports_summary": { "count": 6, "powered": 0, "occupied": 0, "faulted": 0 }
}
```

**Field notes**

- `power.main_5v` — the downstream 5V rail that actually feeds the ports, from
  the main 5V sense (IO7). `true` = present.
- `power.upstream_5v` — upstream (host-side) USB 5V, from the upstream sense
  (IO6). `true` = present.
- `power.total_ma` / `total_mw` — summed across both INA3221s, counting only
  ports whose monitor is present.
- `sensors.valid` — `false` until the first successful AHT20 read, or forever if
  the sensor is absent. `temp_c` and `humidity` are `0.0` then, not meaningful.
- `clock.synced` — `false` until NTP lands; the other clock fields are empty or
  zero while it is false. `utc_offset` comes from settings and is valid either
  way.
- `network.hostname` — the `device.hostname` setting, lower-cased and stripped
  of anything that isn't a letter, digit or `-`. The hub answers at
  `<hostname>.local`.
- `devices.*` — I2C presence. A `false` here explains why the related readings
  are missing rather than zero.
- `reset_reason` — why the **previous** boot ended: `POWERON`, `SW` (software restart, including after an OTA update), `USB`
  (reset over USB, e.g. flashing), `PANIC`, `TASK_WDT` / `INT_WDT` / `WDT`
  (watchdogs), `BROWNOUT`, `PWR_GLITCH`, `EXT`, `DEEPSLEEP`, `JTAG`, `EFUSE`,
  `CPU_LOCKUP`, `SDIO`, or `UNKNOWN`.

---

## GET `/api/ports`

All six ports. Always six entries, always in port order.

```
curl http://192.168.1.100/api/ports
```

```json
{
  "ports": [
    {
      "n": 1,
      "enabled": false,
      "occupied": false,
      "fault": false,
      "state": "off",
      "monitor": true,
      "volts": 0.00,
      "ma": 0.0,
      "mw": 0.0,
      "schedule": { "count": 0, "next": null }
    },
    {
      "n": 2,
      "enabled": true,
      "occupied": false,
      "fault": false,
      "state": "pulse",
      "monitor": true,
      "volts": 5.07,
      "ma": 0.4,
      "mw": 2.0,
      "schedule": { "count": 0, "next": null }
    },
    {
      "n": 3,
      "enabled": true,
      "occupied": true,
      "fault": true,
      "state": "fault",
      "monitor": true,
      "volts": 4.61,
      "ma": 1980.2,
      "mw": 9129.1,
      "schedule": { "count": 0, "next": null }
    },
    {
      "n": 4,
      "enabled": false,
      "occupied": true,
      "fault": false,
      "state": "blue",
      "monitor": false,
      "volts": 0.00,
      "ma": 0.0,
      "mw": 0.0,
      "schedule": { "count": 0, "next": null }
    },
    {
      "n": 5,
      "enabled": true,
      "occupied": false,
      "fault": false,
      "state": "pinkpulse",
      "monitor": true,
      "volts": 0.02,
      "ma": 0.0,
      "mw": 0.0,
      "schedule": {
        "count": 1,
        "next": { "in_s": 3542, "on": false, "at": "Sun 17:30", "entry": 0 }
      }
    },
    {
      "n": 6,
      "enabled": true,
      "occupied": true,
      "fault": false,
      "state": "green",
      "monitor": true,
      "volts": 5.05,
      "ma": 120.4,
      "mw": 607.9,
      "schedule": {
        "count": 2,
        "next": { "in_s": 82, "on": false, "at": "Sun 16:35", "entry": 0 }
      }
    }
  ]
}
```

**Field notes**

- `enabled` — power switched on by firmware. Not the same as current actually
  flowing: with `main_5v` absent a port can be `enabled` and still dead.
- `occupied` — a device is plugged in (sense line, works with power off).
- `fault` — over-current latched by the TPS2552 for that port.
- `state` — the LED state token, so an API client can mirror the physical LED
  exactly. One of:

  | `state` | LED | Meaning |
  |---|---|---|
  | `off` | dark | not enabled, nothing plugged in |
  | `blue` | solid blue | device present, power off |
  | `green` | solid green | powered, device present |
  | `pulse` | pulsing blue | powered, nothing plugged in |
  | `pink` | solid pink | enabled but **no main 5V**, device present |
  | `pinkpulse` | pulsing pink | enabled but **no main 5V**, no device |
  | `fault` | flashing red | over-current |

- `monitor` — `false` when that port's INA3221 is absent; `volts`/`ma`/`mw` are
  then `0` and should be shown as unavailable, not as real readings.
- `schedule.count` — **live** entries only (enabled and not clashing).
- `schedule.next` — `null` when there is nothing upcoming or the clock isn't
  synced. `in_s` counts down in seconds; `at` is `"Ddd HH:MM"` local; `entry` is
  the index into that port's full schedule array, so a client can highlight which
  rule fires next.

---

## GET `/api/ports/{n}`

One port: everything from `/api/ports`, **plus** `power_on_boot` and the full
schedule entry list.

```
curl http://192.168.1.100/api/ports/6
```

```json
{
  "n": 6,
  "enabled": true,
  "occupied": true,
  "fault": false,
  "state": "green",
  "monitor": true,
  "volts": 5.05,
  "ma": 120.4,
  "mw": 607.9,
  "power_on_boot": false,
  "schedule": {
    "count": 2,
    "next": { "in_s": 82, "on": false, "at": "Sun 16:35", "entry": 0 },
    "entries": [
      {
        "days": 127,
        "time": "07:20",
        "mode": "on_for",
        "dur": 60,
        "until": "17:00",
        "every": 30,
        "allday": true,
        "once": false,
        "enabled": true,
        "invalid": false
      },
      {
        "days": 8,
        "time": "22:00",
        "mode": "off",
        "dur": 60,
        "until": "17:00",
        "every": 30,
        "allday": true,
        "once": false,
        "enabled": true,
        "invalid": true,
        "invalid_reason": "clash"
      }
    ]
  }
}
```

A port with no schedule:

```json
{
  "n": 1,
  "enabled": false,
  "occupied": false,
  "fault": false,
  "state": "off",
  "monitor": true,
  "volts": 0.00,
  "ma": 0.0,
  "mw": 0.0,
  "power_on_boot": false,
  "schedule": { "count": 0, "next": null, "entries": [] }
}
```

`404` with `{ "error": "not found" }` if `n` is outside `1..6`.

---

## GET `/api/ports/{n}/schedule`

Just the schedule for one port — the same `count`, `next` and `entries` as
above, without the live electrical state.

```
curl http://192.168.1.100/api/ports/6/schedule
```

```json
{
  "n": 6,
  "count": 2,
  "next": { "in_s": 82, "on": false, "at": "Sun 16:35", "entry": 0 },
  "entries": [
    {
      "days": 62,
      "time": "07:20",
      "mode": "on_until",
      "dur": 60,
      "until": "17:00",
      "every": 30,
      "allday": true,
      "once": false,
      "enabled": true,
      "invalid": false
    },
    {
      "days": 1,
      "time": "09:00",
      "mode": "on_every",
      "dur": 5,
      "until": "18:00",
      "every": 30,
      "allday": false,
      "once": false,
      "enabled": true,
      "invalid": false
    }
  ]
}
```

Empty schedule:

```json
{ "n": 2, "count": 0, "next": null, "entries": [] }
```

`404` with `{ "error": "not found" }` if `n` is outside `1..6`.

---

## GET `/api/status`

The single payload the web UI polls every second. Everything in one object, with
**short field names** and the integer schedule representation. Separate from the
REST endpoints above — it exists for the page, but it is readable by anything.

```
curl http://192.168.1.100/api/status
```

```json
{
  "name": "Smart USB Hub",
  "fw": "1.0.0",
  "rst": "POWERON",
  "theme": "dark",
  "ip": "192.168.1.100",
  "uptime_s": 838,
  "heap": 209800,
  "maxblk": 163828,
  "main5v": true,
  "upstream5v": true,
  "temp": { "valid": true, "c": 30.8, "h": 29.6 },
  "clock": {
    "synced": true,
    "time": "16:33",
    "date": "Sun 02 Aug 2026",
    "wday": 0,
    "secs": 59618
  },
  "ports": [
    {
      "n": 1, "enabled": false, "occupied": false, "fault": false,
      "state": "off", "name": "", "mon": true, "v": 0.00, "ma": 0.0, "mw": 0.0, "sched": 0
    },
    {
      "n": 2, "enabled": true, "occupied": false, "fault": false,
      "state": "pulse", "name": "", "mon": true, "v": 5.07, "ma": 0.4, "mw": 2.0, "sched": 0
    },
    {
      "n": 3, "enabled": true, "occupied": true, "fault": true,
      "state": "fault", "name": "Load", "mon": true, "v": 4.61, "ma": 1980.2, "mw": 9129.1, "sched": 0
    },
    {
      "n": 4, "enabled": false, "occupied": true, "fault": false,
      "state": "blue", "name": "", "mon": false, "v": 0.00, "ma": 0.0, "mw": 0.0, "sched": 0
    },
    {
      "n": 5, "enabled": true, "occupied": false, "fault": false,
      "state": "pinkpulse", "name": "", "mon": true, "v": 0.02, "ma": 0.0, "mw": 0.0,
      "sched": 1,
      "next": { "in": 3542, "on": false, "at": "Sun 17:30", "i": 0 }
    },
    {
      "n": 6, "enabled": true, "occupied": true, "fault": false,
      "state": "green", "name": "Logger", "mon": true, "v": 5.05, "ma": 120.4, "mw": 607.9,
      "sched": 2,
      "next": { "in": 82, "on": false, "at": "Sun 16:35", "i": 0 }
    }
  ],
  "total": { "ma": 452.0, "mw": 2260.0 }
}
```

**Differences from the REST endpoints**

| `/api/status` | REST equivalent |
|---|---|
| `rst` | `reset_reason` |
| `mon`, `v` | `monitor`, `volts` |
| `sched` (int) | `schedule.count` |
| `next.in`, `next.i` | `schedule.next.in_s`, `schedule.next.entry` |
| `next` key **absent** when nothing upcoming | `schedule.next` present and `null` |
| `heap`, `maxblk` | `memory.heap_free`, `memory.heap_max_block` |
| `temp.c`, `temp.h` | `sensors.temp_c`, `sensors.humidity` |
| `main5v`, `upstream5v` | `power.main_5v`, `power.upstream_5v` |
| `theme` (UI only) | — |
| port `name` (UI label) | — |

---

## GET `/api/schedules`

All six ports' schedules in one object, in the **web UI's integer form**: an
`on` boolean plus a numeric `mode`. `ports[0]` is port 1.

```
curl http://192.168.1.100/api/schedules
```

```json
{
  "ports": [
    [],
    [],
    [],
    [],
    [
      {
        "days": 127,
        "time": "08:00",
        "on": true,
        "mode": 1,
        "every": 30,
        "allday": true,
        "dur": 540,
        "until": "17:00",
        "once": false,
        "enabled": true,
        "invalid": false
      }
    ],
    [
      {
        "days": 62,
        "time": "07:20",
        "on": true,
        "mode": 2,
        "every": 30,
        "allday": true,
        "dur": 60,
        "until": "17:00",
        "once": false,
        "enabled": true,
        "invalid": false
      },
      {
        "days": 8,
        "time": "22:00",
        "on": false,
        "mode": 0,
        "every": 30,
        "allday": true,
        "dur": 60,
        "until": "17:00",
        "once": false,
        "enabled": true,
        "invalid": true
      }
    ]
  ]
}
```

A hub with nothing scheduled returns six empty arrays:

```json
{ "ports": [[], [], [], [], [], []] }
```

**`mode` / `on` mapping**

| `on` | `mode` | REST `mode` string |
|---|---|---|
| `false` | `0` | `off` |
| `true` | `0` | `on` |
| `true` | `1` | `on_for` |
| `true` | `2` | `on_until` |
| `true` | `3` | `on_every` |

`invalid` is reported but **never stored** — it is recomputed on every load and
edit. This form does not include `invalid_reason`.

---

## GET `/api/settings/schema`

The structure *and* the current values of every settings group marked visible —
this is what the settings page builds itself from. Groups with an empty `fields`
array are returned but drawn by nothing.

```
curl http://192.168.1.100/api/settings/schema
```

```json
{
  "groups": [
    {
      "name": "device",
      "label": "Device",
      "values": { "device_name": "Smart USB Hub", "hostname": "usb-hub" },
      "fields": [
        { "type": "string", "key": "device_name", "label": "Device name" },
        { "type": "string", "key": "hostname", "label": "Hostname",
          "help": "Reach the hub at <hostname>.local. Takes effect after a restart." }
      ]
    },
    {
      "name": "wifi",
      "label": "WiFi",
      "values": {
        "ssid": "MyNetwork",
        "password": "hunter2",
        "dhcp": true,
        "ip": "",
        "gateway": "",
        "subnet": ""
      },
      "fields": [
        { "type": "string", "key": "ssid", "label": "SSID" },
        { "type": "string", "key": "password", "label": "Password" },
        { "type": "bool", "key": "dhcp", "label": "DHCP",
          "help": "Off = use the static address below. DNS goes to the gateway." },
        { "type": "string", "key": "ip", "label": "Static IP" },
        { "type": "string", "key": "gateway", "label": "Gateway" },
        { "type": "string", "key": "subnet", "label": "Subnet mask" }
      ]
    },
    {
      "name": "location",
      "label": "Location & Time",
      "values": {
        "city": "Melbourne",
        "country": "AU",
        "utc_offset": 10,
        "ntp_server": "pool.ntp.org"
      },
      "fields": [
        { "type": "string", "key": "city", "label": "City" },
        { "type": "string", "key": "country", "label": "Country" },
        { "type": "int", "key": "utc_offset", "label": "UTC offset", "min": -12, "max": 14, "step": 1 },
        { "type": "string", "key": "ntp_server", "label": "NTP server" }
      ]
    },
    {
      "name": "leds",
      "label": "LEDs",
      "values": { "enabled": true, "brightness": 50 },
      "fields": [
        { "type": "bool", "key": "enabled", "label": "Enabled" },
        { "type": "int", "key": "brightness", "label": "Brightness", "min": 0, "max": 255, "step": 5 }
      ]
    },
    {
      "name": "ports",
      "label": "Ports",
      "values": {
        "power_on_boot": [false, false, false, false, false, true],
        "names": ["", "", "Load", "", "", "Logger"]
      },
      "fields": []
    }
  ]
}
```

**Field notes**

- **`values.password` is returned in clear text.** Anything on the LAN can read
  the WiFi password from this endpoint.
- Field `type` is one of `bool`, `int`, `string`.
- `min`/`max`/`step` appear only on `int` fields.
- `help` is an optional one-line hint shown under the field.
- Group order follows registration order and is **not guaranteed stable** — key
  off `name`, not position.
- The `schedules` and `website` groups are hidden from this endpoint. Schedules
  have their own endpoints; the theme has `/api/theme`.

---

# Writes

## POST `/api/port?n=<1-6>&on=<0|1>`

Switch one port's 5V. Runtime only — this does not alter schedules, and a
scheduled event will still switch the port at its next occurrence.

```
curl -X POST 'http://192.168.1.100/api/port?n=6&on=1'
```

**Returns the full `/api/status` body**, read back *after* applying, so the
caller sees real device state rather than assuming success:

```json
{
  "name": "Smart USB Hub",
  "fw": "1.0.0",
  "rst": "POWERON",
  "theme": "dark",
  "ip": "192.168.1.100",
  "uptime_s": 841,
  "heap": 209764,
  "maxblk": 163828,
  "main5v": true,
  "upstream5v": true,
  "temp": { "valid": true, "c": 30.8, "h": 29.6 },
  "clock": { "synced": true, "time": "16:33", "date": "Sun 02 Aug 2026", "wday": 0, "secs": 59621 },
  "ports": [
    { "n": 1, "enabled": false, "occupied": false, "fault": false, "state": "off", "name": "", "mon": true, "v": 0.00, "ma": 0.0, "mw": 0.0, "sched": 0 },
    { "n": 2, "enabled": false, "occupied": false, "fault": false, "state": "off", "name": "", "mon": true, "v": 0.00, "ma": 0.0, "mw": 0.0, "sched": 0 },
    { "n": 3, "enabled": false, "occupied": false, "fault": false, "state": "off", "name": "", "mon": true, "v": 0.00, "ma": 0.0, "mw": 0.0, "sched": 0 },
    { "n": 4, "enabled": false, "occupied": false, "fault": false, "state": "off", "name": "", "mon": true, "v": 0.00, "ma": 0.0, "mw": 0.0, "sched": 0 },
    { "n": 5, "enabled": false, "occupied": false, "fault": false, "state": "off", "name": "", "mon": true, "v": 0.00, "ma": 0.0, "mw": 0.0, "sched": 0 },
    { "n": 6, "enabled": true, "occupied": true, "fault": false, "state": "green", "name": "Logger", "mon": true, "v": 5.05, "ma": 120.4, "mw": 607.9, "sched": 0 }
  ],
  "total": { "ma": 120.4, "mw": 607.9 }
}
```

`on` accepts any integer; non-zero is on, `0` is off. If `n` or `on` is missing,
**nothing is switched** and the current status is returned unchanged with `200`.
An `n` outside `1..6` is ignored the same way.

---

## POST `/api/port/name?n=<1-6>`

Stores the display name for one port. Send an empty string to return the card
label to `Port`.

```
curl -X POST 'http://192.168.1.100/api/port/name?n=6' \
  -H 'Content-Type: application/json' \
  -d '{"name":"Logger"}'
```

Names are trimmed, control characters are dropped and the stored value is
limited to 24 characters.

**Returns the full `/api/status` body**, read back after saving.

---

## POST `/api/hub/ports?on=<0|1>`

Switch **all six ports** with one request. Runtime only — this does not alter
schedules, and a scheduled event will still switch a port at its next
occurrence.

`on` accepts `1`/`0`, `true`/`false` or `on`/`off`.

```
curl -X POST 'http://192.168.1.100/api/hub/ports?on=1'      # everything on
curl -X POST 'http://192.168.1.100/api/hub/ports?on=0'      # everything off
```

**Returns the same body as `GET /api/ports`**, read back *after* switching:

```json
{
  "ports": [
    { "n": 1, "enabled": true, "occupied": false, "fault": false, "state": "pulse",
      "monitor": true, "volts": 5.07, "ma": 0.4, "mw": 2.0,
      "schedule": { "count": 0, "next": null } },
    { "n": 2, "enabled": true, "occupied": false, "fault": false, "state": "pulse",
      "monitor": true, "volts": 5.07, "ma": 0.3, "mw": 1.5,
      "schedule": { "count": 0, "next": null } },
    { "n": 3, "enabled": true, "occupied": false, "fault": false, "state": "pulse",
      "monitor": true, "volts": 5.06, "ma": 0.4, "mw": 2.0,
      "schedule": { "count": 0, "next": null } },
    { "n": 4, "enabled": true, "occupied": false, "fault": false, "state": "pulse",
      "monitor": true, "volts": 5.07, "ma": 0.3, "mw": 1.5,
      "schedule": { "count": 0, "next": null } },
    { "n": 5, "enabled": true, "occupied": false, "fault": false, "state": "pulse",
      "monitor": true, "volts": 5.06, "ma": 0.4, "mw": 2.0,
      "schedule": { "count": 0, "next": null } },
    { "n": 6, "enabled": true, "occupied": true, "fault": false, "state": "green",
      "monitor": true, "volts": 5.05, "ma": 120.4, "mw": 607.9,
      "schedule": { "count": 2,
        "next": { "in_s": 82, "on": false, "at": "Sun 16:35", "entry": 0 } } }
  ]
}
```

A missing or unparseable `on` returns `400` and switches nothing:

```json
{ "error": "bad param", "detail": "on must be 0 or 1" }
```

> This is **not** the same as the user button, which flips each port to its
> opposite state — a mixed set stays mixed. This endpoint drives all six to the
> same state.

---

## POST `/api/schedules`

Replaces **all six ports'** schedules in one shot and persists them to flash.
Uses the integer `on`/`mode` form.

```
curl -X POST 'http://192.168.1.100/api/schedules' \
  -H 'Content-Type: application/json' \
  -d '{"ports":[[],[],[],[],[],[
        {"days":62,"time":"07:20","on":true,"mode":2,"until":"17:00",
         "dur":60,"every":30,"allday":true,"once":false,"enabled":true}
      ]]}'
```

The response is the **saved** set, read back from the device after validation —
so `invalid` tells you which rules it will refuse to act on:

```json
{
  "ports": [
    [],
    [],
    [],
    [],
    [],
    [
      {
        "days": 62,
        "time": "07:20",
        "on": true,
        "mode": 2,
        "every": 30,
        "allday": true,
        "dur": 60,
        "until": "17:00",
        "once": false,
        "enabled": true,
        "invalid": false
      }
    ]
  ]
}
```

- A rule marked `invalid` is **not an HTTP error** — it is stored and reported,
  matching how the web UI flags a bad row.
- Omitted fields fall back to their defaults (`days:0`, `time:"00:00"`,
  `on:false`, `mode:0`, `dur:60`, `until:"17:00"`, `every:30`, `allday:true`,
  `once:false`, `enabled:true`).
- **All six arrays are required.** The body must carry `ports` with exactly six
  entries; a shorter array is rejected outright and nothing is saved. Send empty
  arrays for the ports you aren't changing — this is a whole-hub replace, so
  omitting a port's entries deletes them.
- **Malformed or rejected JSON returns `200` with the unchanged current set** and
  nothing is saved. Compare the response to what you sent to tell the difference.

---

## POST `/api/settings`

Partial update: send only the groups and keys you want changed. Each named group
is updated, its change handler fires (so e.g. a new `utc_offset` re-applies the
timezone immediately), then everything is saved to flash.

```
curl -X POST 'http://192.168.1.100/api/settings' \
  -H 'Content-Type: application/json' \
  -d '{"leds":{"brightness":120},"location":{"utc_offset":10,"city":"Melbourne"}}'
```

```json
{ "ok": true }
```

- The response is **always** `{"ok":true}`, even for unknown group names, and
  even for malformed JSON — in which case nothing is written. Re-read
  `/api/settings/schema` to confirm what actually landed.
- Group names and keys match `/api/settings/schema` exactly.
- Keys you leave out keep their current values.

---

## POST `/api/theme?value=<dark|light>`

Stores the web UI theme and saves it to flash. Returned by `/api/status` as
`theme`.

```
curl -X POST 'http://192.168.1.100/api/theme?value=light'
```

```json
{ "ok": true }
```

A missing `value` parameter changes nothing and still returns `{"ok":true}`.

---

## POST `/api/location/detect`

Requests a one-shot IP geolocation lookup that fills `city`, `country` and
`utc_offset`. The response only confirms the request was **queued** — the
lookup itself runs in the main loop, never in the web handler.

```
curl -X POST 'http://192.168.1.100/api/location/detect'
```

```json
{ "ok": true }
```

Poll `/api/settings/schema` (or `/api/hub` for `clock.utc_offset`) a couple of
seconds later to see the result. There is no endpoint that reports whether the
lookup succeeded.

---

# Examples

The same jobs in four ways: **curl** for the command line and shell scripts,
**Python** and **JavaScript** for test scripts and tools, and **Home Assistant**
for home automation.

Every example uses `http://usb-hub.local` — swap in your hub's address if
yours is different.

## curl

### Read

```sh
# The hub as a whole: power, temperature, clock, network, devices
curl http://usb-hub.local/api/hub

# All six ports
curl http://usb-hub.local/api/ports

# One port in detail, including its schedule
curl http://usb-hub.local/api/ports/3
```

With [`jq`](https://jqlang.org/) installed you can pull out just what you need:

```sh
# Current draw of every port, one per line
curl -s http://usb-hub.local/api/ports | jq -r '.ports[] | "port \(.n): \(.ma) mA"'

# Is port 2 powered?
curl -s http://usb-hub.local/api/ports/2 | jq .enabled

# Temperature
curl -s http://usb-hub.local/api/hub | jq .sensors.temp_c
```

### Switch ports

```sh
# Port 3 on, then off
curl -X POST 'http://usb-hub.local/api/port?n=3&on=1'
curl -X POST 'http://usb-hub.local/api/port?n=3&on=0'

# Every port on, then off
curl -X POST 'http://usb-hub.local/api/hub/ports?on=1'
curl -X POST 'http://usb-hub.local/api/hub/ports?on=0'
```

### Power-cycle a port

```sh
#!/bin/sh
# Power-cycle one port: off, wait, on.   Usage: ./cycle.sh 3
HUB=http://usb-hub.local
curl -s -X POST "$HUB/api/port?n=$1&on=0" > /dev/null
sleep 2
curl -s -X POST "$HUB/api/port?n=$1&on=1" > /dev/null
```

### Change settings

```sh
# Ports 1 and 2 power up at boot, the rest stay off
curl -X POST http://usb-hub.local/api/settings \
  -H 'Content-Type: application/json' \
  -d '{"ports":{"power_on_boot":[true,true,false,false,false,false]}}'

# Dim the LEDs
curl -X POST http://usb-hub.local/api/settings \
  -H 'Content-Type: application/json' \
  -d '{"leds":{"brightness":20}}'
```

`power_on_boot` needs all six values, port 1 first.

## Python

Uses the [`requests`](https://requests.readthedocs.io/) library
(`pip install requests`).

```python
import time
import requests

HUB = "http://usb-hub.local"  # or the hub's IP, e.g. "http://192.168.1.50"


def hub_status():
    """The hub as a whole: power, temperature, clock, network, devices."""
    r = requests.get(f"{HUB}/api/hub", timeout=5)
    r.raise_for_status()
    return r.json()


def ports():
    """All six ports, port 1 first."""
    r = requests.get(f"{HUB}/api/ports", timeout=5)
    r.raise_for_status()
    return r.json()["ports"]


def port(n):
    """One port (1-6) in detail, including its schedule."""
    r = requests.get(f"{HUB}/api/ports/{n}", timeout=5)
    r.raise_for_status()
    return r.json()


def set_port(n, on):
    """Switch port n (1-6) on or off."""
    r = requests.post(f"{HUB}/api/port", params={"n": n, "on": int(on)}, timeout=5)
    r.raise_for_status()


def set_all_ports(on):
    """Switch every port on or off. Returns the ports as read back afterwards."""
    r = requests.post(f"{HUB}/api/hub/ports", params={"on": int(on)}, timeout=5)
    r.raise_for_status()
    return r.json()["ports"]


def power_cycle(n, off_seconds=2):
    """Turn a port off, wait, and turn it back on."""
    set_port(n, False)
    time.sleep(off_seconds)
    set_port(n, True)


def set_power_on_boot(flags):
    """Which ports power up at boot: six booleans, port 1 first."""
    r = requests.post(f"{HUB}/api/settings",
                      json={"ports": {"power_on_boot": flags}}, timeout=5)
    r.raise_for_status()


if __name__ == "__main__":
    hub = hub_status()
    print(f"{hub['name']}  firmware {hub['fw']}  "
          f"{hub['sensors']['temp_c']} °C  {hub['power']['total_ma']:.0f} mA total")

    for p in ports():
        power = "ON " if p["enabled"] else "off"
        device = "device" if p["occupied"] else "empty"
        fault = "  FAULT" if p["fault"] else ""
        reading = f"{p['volts']:.2f} V  {p['ma']:6.1f} mA" if p["monitor"] else "no monitor"
        print(f"port {p['n']}: {power}  {device:6}  {reading}{fault}")
```

### Log a device's power draw

Watch what a device draws over time — through its start-up, or across its
operating states — while the other ports stay powered:

```python
def log_power(n, seconds=30, interval=0.5):
    """Print port n's voltage, current and power every `interval` seconds."""
    end = time.time() + seconds
    while time.time() < end:
        p = port(n)
        print(f"{time.strftime('%H:%M:%S')}  {p['volts']:5.2f} V  "
              f"{p['ma']:7.1f} mA  {p['mw']:7.1f} mW")
        time.sleep(interval)


# Power-cycle port 3 and capture its start-up
set_port(3, False)
time.sleep(2)
set_port(3, True)
log_power(3, seconds=20)
```

### Repeat a startup test

```python
def startup_test(n, runs=10, settle=5, threshold_ma=50):
    """Power-cycle port n `runs` times and check the device comes up drawing
    at least `threshold_ma` after `settle` seconds."""
    passed = 0
    for i in range(1, runs + 1):
        power_cycle(n)
        time.sleep(settle)
        p = port(n)
        ok = p["occupied"] and not p["fault"] and p["ma"] >= threshold_ma
        passed += ok
        print(f"run {i}: {'pass' if ok else 'FAIL'}  {p['ma']:.1f} mA")
    print(f"{passed}/{runs} passed")
```

## JavaScript

Uses the built-in `fetch`, so it runs in **Node.js 18 or later** with nothing to
install. Save it as `hub.mjs` and run `node hub.mjs`.

```js
const HUB = "http://usb-hub.local"; // or the hub's IP, e.g. "http://192.168.1.50"

async function api(path, method = "GET", body) {
  const res = await fetch(HUB + path, {
    method,
    headers: body ? { "Content-Type": "application/json" } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!res.ok) throw new Error(`${method} ${path}: HTTP ${res.status}`);
  return res.json();
}

const hubStatus = () => api("/api/hub");
const ports = async () => (await api("/api/ports")).ports;
const port = (n) => api(`/api/ports/${n}`);
const setPort = (n, on) => api(`/api/port?n=${n}&on=${on ? 1 : 0}`, "POST");
const setAllPorts = (on) => api(`/api/hub/ports?on=${on ? 1 : 0}`, "POST");
const setPowerOnBoot = (flags) => api("/api/settings", "POST", { ports: { power_on_boot: flags } });
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function powerCycle(n, offMs = 2000) {
  await setPort(n, false);
  await sleep(offMs);
  await setPort(n, true);
}

const hub = await hubStatus();
console.log(`${hub.name}  firmware ${hub.fw}  ${hub.sensors.temp_c} °C  ${hub.power.total_ma} mA total`);

for (const p of await ports()) {
  const reading = p.monitor ? `${p.volts.toFixed(2)} V  ${p.ma.toFixed(1)} mA` : "no monitor";
  console.log(`port ${p.n}: ${p.enabled ? "ON " : "off"}  ${p.occupied ? "device" : "empty "}  ${reading}${p.fault ? "  FAULT" : ""}`);
}

await powerCycle(3);
```

**From a web page:** the hub doesn't send CORS headers, so a page served from
anywhere else can't call the API from a browser — the browser blocks it. Call it
from Node (as above), from your own server, or from a page served by the hub
itself.

## Home Assistant

Home Assistant talks to the hub with its built-in
[RESTful](https://www.home-assistant.io/integrations/rest/),
[RESTful Command](https://www.home-assistant.io/integrations/rest_command/) and
[Template](https://www.home-assistant.io/integrations/template/) integrations —
nothing to install. Add these to `configuration.yaml`, then restart Home
Assistant.

If Home Assistant can't resolve `usb-hub.local`, use the hub's IP address.

### 1. Commands to switch ports

```yaml
rest_command:
  usb_hub_port:
    url: "http://usb-hub.local/api/port?n={{ port }}&on={{ power }}"
    method: post
  usb_hub_all_ports:
    url: "http://usb-hub.local/api/hub/ports?on={{ power }}"
    method: post
```

The parameter is called `power` rather than `on`, because YAML reads a bare
`on` as `true`.

Call them from any automation or script:

```yaml
- action: rest_command.usb_hub_port
  data:
    port: 3
    power: 1        # 1 = on, 0 = off
- action: rest_command.usb_hub_all_ports
  data:
    power: 0
```

### 2. Sensors

Hub-wide readings, plus each port's power, device, fault and current. Port 1
is shown; copy its four entries for ports 2–6, changing the name and the index
(`ports[1]` for port 2, through `ports[5]` for port 6).

```yaml
rest:
  - resource: http://usb-hub.local/api/hub
    scan_interval: 30
    sensor:
      - name: "USB Hub Temperature"
        unique_id: usb_hub_temperature
        value_template: "{{ value_json.sensors.temp_c }}"
        availability: "{{ value_json.sensors.valid }}"
        unit_of_measurement: "°C"
        device_class: temperature
        state_class: measurement
      - name: "USB Hub Humidity"
        unique_id: usb_hub_humidity
        value_template: "{{ value_json.sensors.humidity }}"
        availability: "{{ value_json.sensors.valid }}"
        unit_of_measurement: "%"
        device_class: humidity
        state_class: measurement
      - name: "USB Hub Total Current"
        unique_id: usb_hub_total_current
        value_template: "{{ value_json.power.total_ma }}"
        unit_of_measurement: "mA"
        device_class: current
        state_class: measurement
      - name: "USB Hub Total Power"
        unique_id: usb_hub_total_power
        value_template: "{{ (value_json.power.total_mw / 1000) | round(2) }}"
        unit_of_measurement: "W"
        device_class: power
        state_class: measurement
    binary_sensor:
      - name: "USB Hub 5V Supply"
        unique_id: usb_hub_5v_supply
        value_template: "{{ value_json.power.main_5v }}"
        device_class: power

  - resource: http://usb-hub.local/api/ports
    scan_interval: 5
    binary_sensor:
      - name: "USB Hub Port 1 Power"
        unique_id: usb_hub_port_1_power
        value_template: "{{ value_json.ports[0].enabled }}"
        device_class: power
      - name: "USB Hub Port 1 Device"
        unique_id: usb_hub_port_1_device
        value_template: "{{ value_json.ports[0].occupied }}"
        device_class: plug
      - name: "USB Hub Port 1 Fault"
        unique_id: usb_hub_port_1_fault
        value_template: "{{ value_json.ports[0].fault }}"
        device_class: problem
      # ... ports 2-6
    sensor:
      - name: "USB Hub Port 1 Current"
        unique_id: usb_hub_port_1_current
        value_template: "{{ value_json.ports[0].ma }}"
        availability: "{{ value_json.ports[0].monitor }}"
        unit_of_measurement: "mA"
        device_class: current
        state_class: measurement
      # ... ports 2-6
```

### 3. A switch for each port

A proper on/off switch per port, built from the command and the power sensor
above. After switching, it refreshes the sensor straight away instead of
waiting for the next poll. Port 1 is shown; copy it for ports 2–6.

```yaml
template:
  - switch:
      - name: "USB Hub Port 1"
        unique_id: usb_hub_port_1
        state: "{{ is_state('binary_sensor.usb_hub_port_1_power', 'on') }}"
        turn_on:
          - action: rest_command.usb_hub_port
            data: { port: 1, power: 1 }
          - action: homeassistant.update_entity
            target: { entity_id: binary_sensor.usb_hub_port_1_power }
        turn_off:
          - action: rest_command.usb_hub_port
            data: { port: 1, power: 0 }
          - action: homeassistant.update_entity
            target: { entity_id: binary_sensor.usb_hub_port_1_power }
      # ... ports 2-6
```

### 4. Automations

```yaml
automation:
  # Power-cycle the device on port 3 every night
  - alias: "USB Hub - nightly power-cycle of port 3"
    triggers:
      - trigger: time
        at: "03:00:00"
    actions:
      - action: switch.turn_off
        target: { entity_id: switch.usb_hub_port_3 }
      - delay: "00:00:05"
      - action: switch.turn_on
        target: { entity_id: switch.usb_hub_port_3 }

  # Everything off when the last person leaves
  - alias: "USB Hub - all off when away"
    triggers:
      - trigger: state
        entity_id: zone.home
        to: "0"
    actions:
      - action: rest_command.usb_hub_all_ports
        data: { power: 0 }

  # Tell me when a port trips its over-current protection
  - alias: "USB Hub - port fault"
    triggers:
      - trigger: state
        entity_id:
          - binary_sensor.usb_hub_port_1_fault
          - binary_sensor.usb_hub_port_2_fault
          - binary_sensor.usb_hub_port_3_fault
          - binary_sensor.usb_hub_port_4_fault
          - binary_sensor.usb_hub_port_5_fault
          - binary_sensor.usb_hub_port_6_fault
        to: "on"
    actions:
      - action: persistent_notification.create
        data:
          title: "USB hub fault"
          message: "{{ trigger.to_state.name }}: over-current on that port."
```

The hub's own schedules (set on its web page) run on the hub itself, so they
keep working whether Home Assistant is running or not. Use Home Assistant for
anything that depends on the rest of your home.

---

# The schedule entry object

One entry is **one rule**. A port's schedule is an ordered array of them.

| Field | Type | Meaning |
|---|---|---|
| `days` | int | Weekday bitmask. **bit0 = Sunday** … bit6 = Saturday. `127` = every day, `62` = Mon–Fri, `65` = weekend |
| `time` | `"HH:MM"` | When it fires (local). For `on_every` + `allday:false` this is the window **start** |
| `mode` | string / int | REST: `off` · `on` · `on_for` · `on_until` · `on_every`. Web UI: `on` bool + `mode` `0..3` |
| `dur` | int | Minutes. On-time for `on_for`, and the pulse length for `on_every` |
| `until` | `"HH:MM"` | End time for `on_until`; window **stop** for `on_every` when `allday:false` |
| `every` | int | Repeat interval in minutes for `on_every`. Minimum **30** |
| `allday` | bool | `on_every` only: repeat all day, or only between `time` and `until` |
| `once` | bool | Fire at the next occurrence, then **disable itself** (kept in the list, re-armable) |
| `enabled` | bool | Row on/off. Disabled rows are ignored but retained |
| `invalid` | bool | **Read-only.** Set by the device when the rule contradicts another or itself |
| `invalid_reason` | string | **Read-only**, REST form only, present only when `invalid` |

`dur`, `until` and `every` are **always stored**, whatever the mode. Switching
mode never discards another mode's value.

**Modes**

| `mode` | Behaviour |
|---|---|
| `off` | Switch the port off at `time` |
| `on` | Switch on at `time`, open-ended (stays on until something else turns it off) |
| `on_for` | On at `time`, off `dur` minutes later. May cross midnight |
| `on_until` | On at `time`, off at `until`. Stores the **end time**, so editing `time` doesn't drag the end. Crossing midnight is fine (`22:00`→`06:00` = 8h) |
| `on_every` | Repeating pulse: on for `dur`, every `every` minutes. `allday:true` = all day on each selected day; `allday:false` = only between `time` and `until` |

**`invalid_reason` values**

| Value | Cause |
|---|---|
| `clash` | Same `time` and a shared day as another entry. On ON vs OFF the OFF loses; two identical actions invalidate the later one |
| `inside_window` | Fires strictly inside another entry's `on_for` / `on_until` window |
| `ends_at_start` | `on_until` ending at its own start time |
| `interval_too_short` | `on_every` with `every` below 30 |
| `duration_exceeds_interval` | `on_every` whose `dur` doesn't fit inside `every` |
| `window_ends_at_start` | `on_every` window starting and stopping at the same time |

> The firmware expands every entry into discrete switch **events** across the
> week. Port state at any instant is "whatever the most recent past event said",
> which is how overlapping rules resolve deterministically — including after a
> reboot, where the device replays to the correct current state.

---

# Current limitations

How the API behaves in firmware 1.0.0. Planned fixes are in
[future_updates.md](future_updates.md).

1. **Inconsistent error handling.** Only `/api/hub/ports` validates its
   parameter and returns `400`. Everywhere else, bad query params and malformed
   JSON bodies return `200` and silently change nothing, so a client can't tell a
   rejected write from an applied one without re-reading.
2. **No authentication.** Anyone on the LAN can switch ports, and
   `/api/settings/schema` returns the WiFi password in clear text.
3. **Few REST setters.** Apart from `/api/hub/ports` and the port-name setter,
   the writes are the ones the web UI uses: `/api/port`, `/api/schedules` and
   `/api/settings` (which also sets `power_on_boot`). There are no per-port
   boot or schedule REST setters yet.
4. **No restart endpoint.** Restarting is serial-only (`restart`) — and a
   hostname change only takes effect after a restart.
5. **Two schedule representations** (string `mode` vs integer `mode` + `on`)
   for the same data.

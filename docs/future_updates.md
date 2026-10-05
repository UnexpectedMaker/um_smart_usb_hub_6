# Smart USB Hub — Future Updates

Ideas and agreed-but-deferred work. **Nothing here is in firmware 1.0.0.**
Items are described, not designed — the details get agreed before anything is
built.

---

## REST API

### Per-port setters

Agreed shape, not built.

#### POST `/api/ports/{n}?on=&boot=`

Set one port's power and/or its power-on-boot flag. Both parameters optional; at
least one required.

| Param | Values | Effect |
|---|---|---|
| `on` | `0`/`1` | Switch the port now. Runtime only — schedules unaffected |
| `boot` | `0`/`1` | Persist this port's power-on-boot state to flash |

```
curl -X POST 'http://192.168.1.100/api/ports/3?on=1'
curl -X POST 'http://192.168.1.100/api/ports/3?boot=1'
```

`on` and `boot` are deliberately independent: switching a port now shouldn't
silently change what it does at the next boot, and vice versa.

Would respond with the same body as `GET /api/ports/{n}`, read back after
applying.

#### PUT `/api/ports/{n}/schedule`

Replace that **one** port's entire schedule and persist it, using the REST
string-`mode` form.

```
curl -X PUT 'http://192.168.1.100/api/ports/6/schedule' \
  -H 'Content-Type: application/json' \
  -d '{"entries":[{"days":62,"time":"07:20","mode":"on_until","until":"17:00"}]}'
```

Would respond with the same body as `GET /api/ports/{n}/schedule`, read back
after validation, with `invalid`/`invalid_reason` filled in. Omitted fields take
their defaults (`dur:60`, `until:"17:00"`, `every:30`, `allday:true`,
`once:false`, `enabled:true`).

Whole-array replacement is the starting proposal: it keeps client and device in
agreement, and avoids entry IDs on a device with no stable identity for a row.
Whether per-entry add / edit / delete is also wanted is still open.

### Restart endpoint

Restarting is serial-only today (`restart`). A hostname change only takes
effect after a restart, so the web UI and API need a way to do it.

### Consistent error handling

Only `/api/hub/ports` validates its input and returns `400`. Every other write
returns `200` on bad query parameters or malformed JSON and silently changes
nothing, so a client can't tell a rejected write from an applied one without
re-reading.

### Authentication

There is none. Anyone on the network can switch ports, and
`/api/settings/schema` returns the WiFi password in clear text. Needs a scheme
(token header or similar) and the password should stop being sent back.

### Calls from other web pages (CORS)

The hub sends no CORS headers, so a web page served from anywhere else can't
call the API from a browser. Scripts, Node, Home Assistant and the hub's own
page are unaffected. Adding the headers would let people build their own
dashboards in the browser.

### One schedule format

The same schedule data has two representations: a string `mode` on the REST
endpoints, and an integer `mode` plus an `on` boolean on the web-UI endpoints.
Settle on one.

---

## Hub-level schedule

A schedule that acts on the whole hub rather than one port. The paths
`GET` / `PUT /api/hub/schedule` are reserved so nothing else takes them. **Not
designed** — no behaviour is specified or assumed.

---

## Per-port statistics and history

**Not tracked today.** Each port holds only its live enabled / occupied / fault
state — no timestamps or counters — so everything reported is instantaneous.

### Power on/off durations

How long a port has been in its current state, and how long in the previous
one: "Port 3 has been **on** for 4h 12m", "Port 5 has been **off** for 2d 6h",
plus total powered time per port.

To settle before building:
- `millis()` wraps after ~49 days, so durations need 64-bit or wrap-safe maths.
- Is "on" the firmware's enabled flag, or genuinely powered (enabled **and**
  main 5V present)? They differ whenever the 5V supply is missing — the pink
  LED states.
- Does it reset on reboot, or persist?

### Power cycle counts

On and off transitions per port, ideally split by **cause**: button, web UI,
REST API, schedule, or boot state. Fault events per port alongside them — an
over-current that keeps recurring is worth surfacing.

### Device detection counts

Insertions and removals per port, time since the last of each, and total time a
device has been present (separate from time powered).

### Decisions common to all three

- **Persistence** — RAM only (lost on reboot) or written to flash? Flash has a
  limited number of writes, so a persisted counter needs a slow flush interval
  or wear levelling; it can't be written on every change.
- **Reset** — a "clear statistics" action, per port or hub-wide?
- **Where it shows** — new fields on `GET /api/ports/{n}`, a dedicated
  `/stats` endpoint, and where on the web page.
- **Lifetime vs since power-up** — both are useful, at the cost of more storage
  and UI.

---

## Firmware

- **Hostname changes without a restart.** Today the new name only takes effect
  after a restart.
- **Power-on-boot in the web UI.** Which ports come up at boot can only be set
  through the API (`/api/settings`) today; the Settings panel has no control
  for it.
- **Hub IC reset line (IO2).** It's wired to the ESP32-S3 but the firmware
  doesn't use it. Whether the firmware should hold it, or use it to reset the
  hub chip, is undecided.
- **MQTT.** Planned for later; not designed.
- **Drive the port LEDs with RMT directly.** The six LEDs currently go through
  the Adafruit NeoPixel library. Six LEDs on one pin don't need a third-party
  library — talk to the ESP32's RMT peripheral directly and drop the
  dependency.

---

## Web UI

- **Timeline in the light theme.** Days other than today are darkened by 40%,
  which is subtle on the dark theme but turns the light theme's rows into heavy
  grey bars.

---

## Hardware

- **Schematic PDF**, for anyone without KiCad.

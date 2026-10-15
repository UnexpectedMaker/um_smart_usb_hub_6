# Smart USB Hub — Coding Conventions

How the firmware is laid out and written, and how to add a new saved setting.
Follow these when contributing so new code reads like the code around it.

---

## Project layout

```
firmware/
  src/
    main.cpp       pin map, LEDs, scheduler, JSON builders, WiFi/OTA, serial console, setup/loop
    ports/         USBPort — one port's power, fault and device sense
    settings/      HubSettings — every saved setting and how it's stored
    web/           HubWebServer (routes and API) and HubWebUI (the embedded page)
    esphome/       EspHomeApi — Home Assistant through the ESPHome native API
  lib/
    UM_INA3221/    INA3221 power monitor driver
    UM_LCA9555/    XL9555 / LCA9555 I/O expander driver
    nlohmann_json/ JSON library (third party — never edit)
```

- Put new code in the folder for what it does, or a new folder under `src/` if
  it's a new part of the firmware. Includes name the folder:
  `#include "settings/HubSettings.h"`.
- Keep related code together. Several small classes can share one file; don't
  split one feature across extra files without a reason.
- Only third-party or standalone driver code goes in `lib/`.

## Files

Every source file starts with a licence header and a description of what the
file is for:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Unexpected Maker
//
// Part of the Smart USB Hub firmware. This is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License, version 3 or (at your option) any later version. See LICENSE.
//
// USBPort.h — one downstream USB-C port.
//
// What the file contains and how it fits in...
```

## Comments

- Every function and method has a comment saying what it does — in the header
  for declarations, above the definition otherwise. JavaScript functions in the
  web UI too.
- Comments explain *why* when it isn't obvious: hardware quirks, ordering
  requirements, limits.
- Keep comments true. If you change what code does, change its comment.

## Naming

| Kind | Style | Examples |
|---|---|---|
| Classes and structs | `PascalCase` | `USBPort`, `HubPortsConfig`, `PortReading` |
| Functions and methods | `camelCase` | `readPort()`, `buildStatusJson()`, `setEnabled()` |
| Global objects | `g_` prefix | `g_ports`, `g_schedules`, `g_tempC` |
| Private members | `_` prefix | `_config`, `_enabled` |
| Constants and macros | `UPPER_SNAKE` | `NUM_PORTS`, `PORT_NAME_MAX`, `I2C_SDA` |
| JSON keys | `snake_case` | `power_mode`, `device_name`, `utc_offset` |

## Formatting

- C++: 4-space indent, braces on their own line.
- JavaScript, CSS and HTML in `HubWebUI.h`: 2-space indent.
- No tabs.

## Ports

- Ports are numbered **1–6** everywhere a person or client sees them: the web
  UI, the API, the serial console.
- Inside the firmware they're indexed **0–5** (`ports[i]`, `names[i]`).
  Convert at the edge: `ports[n - 1]`.

## Hardware and timing

- Interrupt handlers only set a flag. The real work happens in task code.
- Nothing blocks boot or the main loop waiting for the network. WiFi, the web
  server and NTP all come up in the background.

## Memory

- Buffers and data live in PSRAM, allocated with
  `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` (see
  `PortHistory` and `EnvHistory`). Internal SRAM is only for what the IDF
  needs it for: DMA, ISRs, task stacks and lwIP.
- `std::vector`, `String` and `malloc` of 4 KB or less always come from
  internal SRAM (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`), so don't use
  them for buffers.
- Allocate once, at start-up. Nothing is allocated per message, per request
  or per pass of the main loop. Build text into fixed or stack buffers with
  `snprintf`.

## Network connections

- Sockets used from the main loop are non-blocking. Never call
  `WiFiClient::write` there: it retries for up to 10 s on a stalled client.
  Send with `MSG_DONTWAIT`, and drop a client that can't take a whole frame.
- Every long-lived connection has a dead-client timeout, so stale
  connections can't fill the slots.
- lwIP queues at most 16 segments per socket. Many small sends in a row
  (with `TCP_NODELAY`) hit that limit; leave Nagle on or batch the frames.

## I2C bus

The expander, both INA3221s and the AHT20 share one I2C bus, used from more
than one task (`loop()`, the web server, the [port profiler](profiler.md)).
Every I2C access holds the bus lock, `I2CLock` (`ports/USBPort.h`), for the
whole transaction.

`I2CLock` is a scope guard around a recursive FreeRTOS mutex. Declaring it
takes the mutex, waiting if another task has it. The mutex is released
automatically when the block it was declared in ends — at the closing `}`, or
on any `return` out of the block:

```cpp
{
    I2CLock lock;          // takes the mutex
    ioex.write(pin, LOW);  // bus is ours
}                          // block ends: mutex released
```

- Lock in the function that does the I2C transfer, around just the transfer.
- Never put a delay or a busy-poll inside the locked block. Split a slow
  device read into "start" and "collect" steps, each locked on its own (see the
  AHT20 read in `loop()`).
- Recursive: a locked function can call another that also locks.

## Serial output

- Every line ends in CR+LF: use `Serial.println()` or end `printf` strings
  with `\r\n`, so output lines up in any terminal.
- Log lines start with a tag in square brackets: `[wifi]`, `[sched]`,
  `[config]`.
- Tables (like `info`) print their header and rows with the same `printf`
  column widths.

## Text

- Limit text by **characters, not bytes**. Names and other user text are UTF-8,
  and a multi-byte character must never be cut in half.
- Any user-entered text placed in hand-built JSON goes through `jsonEsc()`.

## Web UI

- The page is embedded in `src/web/HubWebUI.h` and served from flash. Nothing
  is loaded from the internet: no CDNs, no web fonts, icons are inline SVG.
- The page only shows what the hub reports. After a change, show the state
  read back from the hub, never a guess.

## API

- Simple actions take query parameters (`/api/port?n=3&on=1`). Bulk data takes
  a JSON body (`/api/schedules`, `/api/settings`).
- Writes reply with the real state read back afterwards where that makes sense.
- Settings are changed through `POST /api/settings`. Don't add an endpoint for a
  single setting.
- Per-port data appears in both `/api/status` (the web UI) and the REST
  endpoints (`/api/ports`, `/api/ports/{n}`).
- Keep [`rest_api.md`](rest_api.md) in step with any API change.

---

# Adding to a saved setting

Every saved setting belongs to a **settings group** in
`src/settings/HubSettings.h`: a plain config struct plus a small class built on
`HubSettingsT`. Each group is stored in its own file on the hub,
`/settings/<group>.json`, and is listed in `/api/settings/schema`.

The worked example below is how port names were added to the `ports` group.

## 1. Add the field to the config struct, with a default

```cpp
// Longest port name, in characters (not bytes).
#define PORT_NAME_MAX 24

struct HubPortsConfig
{
    std::array<String, HUB_NUM_PORTS> power_mode{{"manual", "manual", "manual", "manual", "manual", "manual"}};
    std::array<String, HUB_NUM_PORTS> names; // "" = unnamed
};
```

Plain types, `String`, `std::array` and `std::vector` all work. The default you
give here is what a hub uses until the value is set.

## 2. Add it to the JSON macro

```cpp
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HubPortsConfig, power_mode, names);
```

That's all it takes to load and save it. Use the `_WITH_DEFAULT` form:
a hub whose stored file was written before the field existed simply gets the
default from step 1, and keeps everything else it had.

Don't hand-write the JSON loading for a group.

## 3. Clean or validate it, if it needs it

If a value needs tidying or limits, override `fromJson`, call the base version
first, then fix the values up:

```cpp
// HubSettings.h
class PortsSettings : public HubSettingsT<HubPortsConfig>
{
public:
    const char *name() const override { return "ports"; }
    const char *label() const override { return "Ports"; }

    // Loads as usual, then tidies every name: control characters turned into
    // spaces, surrounding spaces trimmed, cut to PORT_NAME_MAX characters.
    void fromJson(const nlohmann::json &j) override;
};

// HubSettings.cpp
void PortsSettings::fromJson(const nlohmann::json &j)
{
    HubSettingsT<HubPortsConfig>::fromJson(j);
    for (auto &name : _config.names)
        name = cleanPortName(name);
}
```

Every way a value gets in — loading at boot, the web UI, the API — goes through
`fromJson`, so this is the one place to enforce it.

`fromJson` applies only the keys it's given: anything not in the incoming JSON
keeps its current value. If the incoming JSON doesn't convert (a wrong type, a
short array), the group is left exactly as it was.

## 4. Let people set it

**Through the API.** Nothing to add — `POST /api/settings` already takes any
group by name:

```sh
curl -X POST http://usb-hub.local/api/settings \
  -H 'Content-Type: application/json' \
  -d '{"ports":{"names":["Pi","","Logger","","",""]}}'
```

The hub applies it, saves that group, and calls the group's
`onConfigChanged()`.

**In the Settings panel.** A bool, number, text field, fixed choice, or row of
per-port buttons (`PortBools`, like the `power_mode` rows), is shown in the
Settings panel by adding it to the group's `uiSchema()`. Port names aren't
there — they're edited on the port cards — so they're not in
`PortsSettings::uiSchema()`.

**From your own code.** Change the value, then save the group:

```cpp
g_schedules.disableEntry(port, index);
g_schedules.save();   // writes /settings/schedules.json
```

## 5. Report it

Add it wherever clients read that data. For per-port values that's both JSON
builders in `main.cpp`:

```cpp
// buildStatusJson() — the web UI
j += ",\"name\":\"" + jsonEsc(g_ports.config().names[i]) + "\"";

// portJson() — the REST API
j += ",\"name\":\"" + jsonEsc(g_ports.config().names[i]) + "\"";
```

User text always goes through `jsonEsc()`. If the serial console's `info`
shows that kind of data, add it there too.

## 6. Use it in the web UI

The page receives it in `/api/status` (`p.name` for each port) and saves it
back through `POST /api/settings`. When a group holds an array, as `names`
does, send the whole array — changing one entry means sending all six, with the
others unchanged:

```js
const names = [1, 2, 3, 4, 5, 6].map((i) => {
  const p = lastPorts.find((x) => x.n === i);
  return (p && p.name) || '';
});
names[n - 1] = name;
fetch('/api/settings', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ ports: { names } })
});
```

## 7. Document it

Add the field to the examples in [`rest_api.md`](rest_api.md): the
`/api/settings/schema` example for the group, and every response that now
includes it.

## Adding a whole new group

1. Write the config struct with defaults, and its
   `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` line.
2. Write a class on `HubSettingsT<YourConfig>` with a unique `name()` (its file
   name and API key) and a `label()` (its heading in the Settings panel).
3. Declare one global instance (`extern YourSettings g_yours;` in the header,
   the definition in `HubSettings.cpp`). It registers itself — loading, saving
   and `/api/settings` pick it up with no other changes.
4. Override `show_on_website()` to return `false` if it shouldn't appear in the
   Settings panel.

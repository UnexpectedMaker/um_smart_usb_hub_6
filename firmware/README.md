# Smart USB Hub — Firmware

**Designed and made by [Unexpected Maker](https://unexpectedmaker.com).**

Firmware for the Unexpected Maker Smart USB Hub: a 6-port USB 2.0 hub with an
ESP32-S3 that controls and monitors every downstream port.

For each port the hub can:

- switch the port's 5V on and off
- detect a plugged-in device, even with the port's power off
- detect an over-current fault
- measure voltage, current and power
- show the port's state on its own RGB LED

All of it is available from a built-in web page, a JSON API, a serial console
and the button on the hub. Ports can also be switched on a weekly schedule.

## Hardware

| Part | Job |
|---|---|
| ESP32-S3-WROOM-1U (16 MB flash, 8 MB PSRAM) | runs this firmware |
| WCH CH339W | USB 2.0 hub, 1 upstream + 6 downstream USB-C ports |
| TPS2552 ×6 | per-port 5V load switch with over-current fault |
| XL9555 | I/O expander for the 6 power enables and 6 fault lines |
| INA3221 ×2 | voltage / current / power, 3 ports each |
| AHT20 | temperature and humidity |
| WS2812B ×6 | one RGB LED per port |

The pin map is at the top of `src/main.cpp`.

## Version

This is firmware **1.0.0**. The version a hub is running is shown in the web
page's side panel (Firmware), by the `info` serial command, and in the `fw`
field of the JSON API. It's set by `HUB_FW_VERSION` at the top of
`src/main.cpp`.

## Building and flashing

You need [PlatformIO](https://platformio.org/), either the VS Code extension or
the command line. The first build downloads the ESP32 toolchain and libraries.

```sh
pio run                      # build
pio run -t upload            # build and flash over USB
```

### Updating over WiFi

Once the hub is running this firmware and is on your network, you can flash it
without a cable:

```sh
pio run -e ota -t upload
```

This uploads to `usb-hub.local`. If `.local` names don't resolve on your
machine, put the hub's IP address in `upload_port` under `[env:ota]` in
`platformio.ini`.

## Serial console

The hub has a text console over USB. It's how you give a new hub its WiFi
details, and it's always there for checking on the hub or controlling it
without a browser.

### Opening it

The ESP32-S3 is connected to one of the hub's own USB ports, so plugging the
hub's upstream USB-C into your computer makes it appear as a USB serial
device. Open it at **115200 baud** with any serial terminal:

- PlatformIO: `pio device monitor`
- Arduino IDE: Tools → Serial Monitor
- macOS / Linux: `screen /dev/cu.usbmodemXXXX 115200` (your device name will
  differ)

The console echoes what you type and supports backspace. Press Enter to run a
command, and type **`help`** at any time to list them all.

The start-up messages are printed before your computer has finished
connecting, so you'll usually miss them — that's normal. Type **`info`** to
see the hub's full current state.

### Commands

| Command | Does |
|---|---|
| `help` | list the commands |
| `info` | full status: network, time, power rails, devices, every port |
| `port <1-6> on\|off` | switch a port |
| `mon` | each port's voltage, current and power, plus the INA3221 alert lines |
| `set_ssid <name>` | store the WiFi network name |
| `set_pass <password>` | store the WiFi password |
| `wifi` | connect with the stored details |
| `restart` | reboot the hub |

## First-time setup

A new hub has no WiFi details, so its web page isn't reachable yet. You give
it your network once, over the [serial console](#serial-console); after that
everything can be changed from the web page.

You can tell which state it's in at power-up: the six LEDs sweep **red** when
no WiFi details are stored, and **green** when they are.

### 1. Open the serial console

Connect the hub to your computer and open the console as described in
[Opening it](#opening-it).

### 2. Enter your WiFi details

```
set_ssid YourNetworkName
set_pass YourPassword
wifi
```

- `set_ssid` and `set_pass` take everything after the command, so names and
  passwords with spaces work. Both are saved straight away and survive a
  restart.
- `wifi` connects using what you've stored. The hub also connects on its own
  at every power-up from now on.
- The ESP32-S3 only supports **2.4 GHz** WiFi.

### 3. Check it connected

Within a few seconds the console prints lines like:

```
[wifi] connected — IP 192.168.1.50
[ota] ready — http://usb-hub.local/
```

If nothing appears, type `info` and look at the network section. `state`
shows what went wrong — `SSID not found` usually means a typo or a 5 GHz-only
network, `connect failed` usually means a wrong password. Fix it with
`set_ssid` / `set_pass` and run `wifi` again.

### 4. Open the web page

Go to `http://usb-hub.local/`, or the IP address from step 3 if `.local`
names don't work on your computer. From the Settings panel you can change the
WiFi details, use a static address, rename the hub, set your location and
time zone, and adjust the LEDs — no serial console needed.

## Using the hub

### Port LEDs

| LED | Port |
|---|---|
| off | power off, nothing plugged in |
| solid blue | power off, device plugged in |
| pulsing blue | power on, nothing plugged in |
| solid green | power on, device plugged in |
| flashing red | over-current fault |
| solid pink | power on but no 5V supply, device plugged in |
| pulsing pink | power on but no 5V supply, nothing plugged in |

### Button

Each press flips every port to the opposite state.

### Web page

- Turn each port on or off, and see its live voltage, current and power
- Weekly schedules per port: switch off, switch on, on for a duration, on
  until a time, or on for a short burst at a regular interval. A timeline shows
  the week, and entries that clash are flagged.
- Settings: device name, hostname, WiFi (DHCP or static address), location and
  time zone, LED brightness
- Light and dark themes

The page is served by the hub itself and needs no internet connection.

### JSON API

The web page is built on a JSON API that other tools can use too, for example
`GET /api/hub`, `GET /api/ports` and `POST /api/hub/ports?on=1` to switch every
port on. Every endpoint, with full example requests and responses, is in the
[API reference](../docs/rest_api.md).

## Project layout

```
src/
  main.cpp       pin map, LEDs, scheduler, JSON, WiFi/OTA, serial console, setup/loop
  ports/         USBPort — one port's power, fault and device sense
  settings/      HubSettings — everything stored in flash (/settings.json)
  web/           HubWebServer (routes and API) and HubWebUI (the embedded page)
lib/
  UM_INA3221/    INA3221 power monitor driver
  UM_LCA9555/    XL9555 / LCA9555 I/O expander driver
  nlohmann_json/ JSON library (third party)
platformio.ini   build configuration
partitions.csv   flash layout: two app slots for OTA, plus storage for settings
```

## Licence

Copyright (c) 2026 [Unexpected Maker](https://unexpectedmaker.com).

The firmware, including the `UM_INA3221` and `UM_LCA9555` driver libraries, is
released under the **GNU General Public License v3.0 or later** — see
[LICENSE](LICENSE).

You're welcome to use it, change it, and use it in your own products. If you
distribute it — or a product running it, or a modified version — you must make
the complete source of that firmware available under the same licence.

### Third-party code

These keep their own licences:

| Code | Where | Licence |
|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) 3.12.0 | `lib/nlohmann_json/json.h` | MIT, Copyright (c) 2013-2025 Niels Lohmann |
| [GitHub Octicons](https://github.com/primer/octicons) | icons in `src/web/HubWebUI.h` | MIT, Copyright (c) GitHub Inc. |
| [Phosphor Icons](https://github.com/phosphor-icons/core) | USB icon in `src/web/HubWebUI.h` | MIT, Copyright (c) 2020 Phosphor Icons |
| [GitHub Primer](https://github.com/primer/primitives) | colour themes in `src/web/HubWebUI.h` | MIT, Copyright (c) GitHub Inc. |
| [modern-normalize](https://github.com/sindresorhus/modern-normalize) | CSS reset in `src/web/HubWebUI.h` | MIT, Copyright (c) Sindre Sorhus |

Libraries downloaded at build time (ESPAsyncWebServer, AsyncTCP, Adafruit
NeoPixel, DHT20 and the Arduino ESP32 core) are under their own licences.

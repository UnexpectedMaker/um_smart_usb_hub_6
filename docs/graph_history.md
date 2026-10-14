# Graph History

This document describes the web graph feature added on top of the port
profiler. It covers the visible UI, the firmware data path, and the main design
decisions behind the implementation.

## What changed

- Two buttons on the top bar, a graph and a clock, pick what clicking a port
  shows: its history graph or its schedule. Selecting one deselects the other.
- The large graph can display voltage, current and power together or as any
  selected subset.
- The graph always shows all the history held for the port, filling the
  width, and grows as new readings come in. With more readings than pixels,
  each pixel column is drawn as the average of the readings in it.
- Drag across the graph to zoom in on that span of time; double-click to zoom
  back out. While zoomed, the arrows on the graph's left and right edges move
  that end of the zoom, and the scrollbar thumb moves the zoomed span through
  the history. The length of time shown is always shown under the graph.
- The Y axis is set from all the history held, so it doesn't change when
  zooming or scrolling. Scrolling over a scale label adjusts its top level.
- Averaging buttons: Off, 2, 4 or 8. This is the same setting as the port
  cards' averaging.
- Port events (power off/on, device removed/inserted) are marked on the graph
  with a dashed line, an arrow and a label.
- The window, Head, X Linear / X Log and Y Lock buttons are still shown, but
  do nothing.
- Every port card shows a mini graph of its newest 10 seconds; clicking it
  cycles mA, mW and V.
- The sidebar environment panel now has temperature and humidity trends.
- Graph UI preferences and profiler averaging are stored in the `profiler`
  settings group.

## Theory of operation

The INA3221 devices are still sampled by the port profiler. Graph history does
not poll the INA devices directly and does not add extra I2C reads.

The profiler runs at 100 Hz and writes every port's reading straight into a
PSRAM ring per port, 60 minutes long, every 10 ms. Ports that are off, have no
monitor or fail a read get an invalid slot, so a slot's position always gives
its time. See `profiler.md` for the format.

Each port also has an event list (power off/on, device removed/inserted),
recorded by the profiler at the 10 ms period it sees the change.

The hub sends the history raw, in binary pages of up to 10 minutes, exactly as
stored (3 bytes per reading), with the events that fall within each page. The
browser turns the raw readings into volts and mA, and works out power as
volts × mA. See `rest_api.md` for the page layout.

When a port's graph is opened, the browser fetches the newest 10 minutes, then
fills in the older history 10 minutes at a time, newest first, until the hub
has no older readings. Meanwhile it polls for new readings five times a second.
The browser keeps each port's history it has received, up to 60 minutes per
port, when another port is shown, so going back to a port only fetches what's
new. Zooming, scrolling, averaging and changing metrics all redraw from the
browser's copy, with nothing fetched from the hub.

The mini graphs show each port's newest 10 seconds. The browser fetches one
other port's new readings per poll, in turn.

History is only ever served for one port at a time; `port=all` has been
removed from the endpoint. The environment graph uses a separate five-minute
PSRAM ring for temperature and humidity.

## Data flow

1. The profiler samples powered ports at 100 Hz and writes every port's slot
   and any events into `PortHistory` in PSRAM.
2. `/api/history` sends one binary page at a time: a header, the page's
   events and its raw readings, copied straight out of PSRAM as they're sent.
3. The browser converts the readings and stores them by capture count.
4. The canvas renderer draws the selected metrics and the events from the
   browser's copy.
5. `/api/status` provides live text values (the history's newest readings)
   and stored UI prefs.

Environment history follows the same shape:

1. The AHT20 is read on its normal cadence.
2. `EnvHistory` stores compact temperature/humidity samples in PSRAM.
3. `/api/environment/history` streams the sidebar graph after reloads.

## Storage

Port history uses 1,080,000 bytes of PSRAM per port (360,000 slots of 3
bytes), plus up to 256 events per port. Each slot is packed as:

| Bits | Field |
|---|---|
| 1 | Valid |
| 10 | Bus voltage, raw (8 mV steps, 0–8.184 V) |
| 13 | Shunt voltage, raw, signed (40 µV steps) |

The graph history is PSRAM-only. It survives web UI reloads and reconnects, but
not a device reboot or power loss.

Temperature and humidity history is also PSRAM-only and stores the five-minute
window needed by the sidebar graph.

## Settings

All persistent graph options use the existing settings framework.

Settings panel, in the `profiler` group:

- `averaging`: `0`, `2`, `4` or `8`

Web UI graph preferences, also in the `profiler` group (not shown in the
Settings panel):

- `mini_metric`
- `graph_metric`
- `graph_voltage_zero`

Historical, no longer used by the web UI, and to be removed: `mini_graphs`,
`graph_window`, `graph_scale` and `graph_y_lock`. They're still stored and
returned for now.

The web UI writes these through `POST /api/settings`, the same path used by the
rest of the settings system.

## Design decisions

- Keep INA polling owned by the profiler.
  This avoids multiple UI features competing for the I2C bus.

- Store voltage and current only.
  Power is derived on demand, which saves PSRAM and keeps each sample compact.

- Keep 100 Hz only, for 60 minutes.
  Every reading is kept, and every reading is sent to the browser.

- The profiler writes the history directly.
  One writer on one clock, so no reading is missed or duplicated, and events
  line up with the readings.

- Keep history in RAM rather than flash.
  This avoids flash wear and keeps writes cheap enough for high-rate capture.

- Use the existing settings patterns.
  UI preferences and averaging go through the same storage and API flow as other
  hub settings.

- Send the history in binary, raw.
  No text or float conversion on the hub: a page is a straight copy out of
  PSRAM, about 180 KB for 10 minutes.

- Send it in 10-minute pages, newest first.
  The graph shows the newest data straight away, and no single response is the
  whole 1 MB.

- The browser keeps the history it receives.
  Zooming, scrolling and averaging don't fetch anything, and switching back to
  a port only fetches what's new.

## TODO

- Consider reporting history fetch timing or throttling state for diagnostics.
- Revisit whether any history should survive reboot once a flash-safe storage
  strategy exists.

# Graph History

This document describes the web graph feature added on top of the port
profiler. It covers the visible UI, the firmware data path, and the main design
decisions behind the implementation.

## What changed

**Top bar.** Two buttons, a graph and a clock, pick what clicking a port shows:
its history graph or its schedule. Selecting one deselects the other.

**The graph** (one port at a time):

- Shows voltage, current and power together or any of them (V / mA / mW
  buttons).
- Always shows all the history held for the port, filling the width, and grows
  as new readings come in, unless zoomed in.
- With more readings than pixels, readings are grouped in blocks about a pixel
  wide, lined up on the capture count so the live view doesn't shimmer. Each
  block is drawn as its average, with a faint band from its lowest to its
  highest reading, so short spikes still show.
- Readings missing while the port was on (a missed period or a failed read)
  are shaded red. Time the port was switched off isn't.
- Port events (power off/on, device removed/inserted) are marked with a dashed
  line, an arrow and a label. Labels that would overlap stack downwards.
- Averaging buttons: Off, 2, 4, 8 or 16. The same setting as the port cards'
  averaging, saved to flash 3 s after the last change.
- Legend at the top right: Voltage, Current, Power, Missing. Then the
  Head indicator, lit while the view follows the newest reading.

**Zoom and the brush:**

- Drag across the plot to zoom to that span (time only). Double-click the plot
  to zoom back out. The scroll wheel over the plot zooms in and out around the
  middle of the view.
- The shortest zoom is 1 second (100 readings).
- The brush under the graph covers all the history held. Its blue thumb is the
  part shown, with the length of time shown inside it; it never gets narrower
  than that label. Drag the thumb to move the zoom; drag either edge to move
  that end of the zoom.
- Dragging the thumb's right edge to the end locks the view to the newest
  reading (Head): it moves along as readings arrive, and stays locked while the
  left edge is dragged. The scroll wheel never locks it, and unlocks it.

**Cursor:**

- A vertical line with an arrow on top, always in view; it moves to the middle
  if a zoom leaves it out. Click anywhere on the plot to move it there (a press
  that moves less than 6 px is a click, more is a zoom drag), or drag the line,
  which can be grabbed up to 12 px either side. Dragging past either side of
  the plot moves the view along.
- A box beside it shows its clock time and how long ago that was, and the
  value of each metric shown. A dot marks where it crosses each line.
- While the view follows the newest reading, the cursor stays at the same place
  on the plot.

**Y scales** (the V, mA and mW label columns on the left):

- Set from all the history held, so they don't change when zooming or
  scrolling.
- Hovering a column tints it. Double-click it to fit that metric to what's in
  view, with 12.5% clear above and below. Drag it up or down to move that
  metric's line. Scroll over it to stretch or squash the scale around the
  metric's average in view. Single-click the V column to toggle voltage
  auto-zero.
- Every scale stays between 0 and its limit (6 V, 1802 mA, 10,811 mW), with
  12.5% of the height to spare beyond each.

**Measurements.** While zoomed in, a box between the averaging buttons and the
legend shows the lowest, highest and average of each metric in view.

**Mini graphs.** Every port card shows a mini graph of its newest 10 seconds,
whichever view is selected; clicking it cycles mA, mW and V. The line stays
below the labels, and each scale's top is at least 10 mA, 50 mW or 1 V.

**Also:** the sidebar environment panel has temperature and humidity trends.
The graph preferences and averaging are stored in the `profiler` settings
group.

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

None of the `profiler` group is shown in the Settings panel. All of it is set
by the graph's controls:

- `averaging`: `0`, `2`, `4`, `8` or `16`, from the averaging buttons
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

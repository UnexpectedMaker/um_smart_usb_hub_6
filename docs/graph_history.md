# Graph History

This document describes the web graph feature added on top of the port
profiler. It covers the visible UI, the firmware data path, and the main design
decisions behind the implementation.

## What changed

- Each port can show a large trend graph by clicking its voltage, current or
  power reading.
- The large graph can display voltage, current and power together or as any
  selected subset.
- The graph view can select the 100 Hz, 10 Hz or 1 Hz retained history tier.
- The graph window can be changed between fixed time spans, with a head button
  to return to live data.
- The X axis can be linear or logarithmic time.
- The Y axis can be locked, and the visible scale can be adjusted by scrolling
  on the left scale labels.
- Optional mini graphs can be shown on the port cards.
- The sidebar environment panel now has temperature and humidity trends.
- Graph UI preferences are stored in the existing `website` settings group.
- Profiler averaging is stored in the normal settings system as
  `monitor.averaging`.

## Theory of operation

The INA3221 devices are still sampled by the port profiler. Graph history does
not poll the INA devices directly and does not add extra I2C reads.

The profiler runs at 100 Hz and keeps the newest readings for each powered
port. The main loop periodically reads the profiler cache and appends compact
voltage/current samples into PSRAM-backed ring buffers. Power is not stored; it
is calculated as `volts * milliamps` when history JSON is built.

Each port has three retained tiers:

| Tier | Retained window |
|---|---|
| `100 Hz` | `10 min` |
| `10 Hz` | `20 min` |
| `1 Hz` | `16 h` |

The large graph's frequency selector chooses which retained tier is displayed.
It does not change the profiler capture rate, so switching graph tiers does not
clear the device-side history.

The browser keeps its own cache of graph samples. Live graph updates add to that
cache opportunistically, and background fetches try to fill missing ranges
without blocking normal status updates. When the user scrolls back in time, the
graph prefers cached browser data and asks the hub for missing ranges only when
needed.

The mini graphs use the same history endpoint with `port=all`, but request a
small fixed window so the response remains lightweight. The environment graph
uses a separate five-minute PSRAM ring for temperature and humidity.

## Data flow

1. The profiler samples powered ports at 100 Hz.
2. `PortHistory` copies profiler values into PSRAM rings.
3. `/api/history` streams compact `[time, volts, ma, mw]` point arrays.
4. The browser merges returned points into its local cache.
5. The canvas renderer draws the selected metrics from the cached window.
6. `/api/status` continues to provide live text values and stored UI prefs.

Environment history follows the same shape:

1. The AHT20 is read on its normal cadence.
2. `EnvHistory` stores compact temperature/humidity samples in PSRAM.
3. `/api/environment/history` streams the sidebar graph after reloads.

## Storage

Port history uses up to 1 MB of PSRAM per port. Each stored sample is packed as:

| Field | Type | Meaning |
|---|---|---|
| `ms` | `uint32_t` | `millis()` timestamp |
| `mv` | `uint16_t` | volts multiplied by 1000 |
| `ma10` | `uint16_t` | milliamps multiplied by 10 |

The graph history is PSRAM-only. It survives web UI reloads and reconnects, but
not a device reboot or power loss.

Temperature and humidity history is also PSRAM-only and stores the five-minute
window needed by the sidebar graph.

## Settings

All persistent graph options use the existing settings framework.

Visible settings:

- `monitor.averaging`: `0`, `2`, `4` or `8`

Hidden web UI preferences in the `website` group:

- `mini_graphs`
- `mini_metric`
- `graph_metric`
- `graph_hz`
- `graph_window`
- `graph_scale`
- `graph_voltage_zero`
- `graph_y_lock`

The web UI writes these through `POST /api/settings`, the same path used by the
rest of the settings system.

## Design decisions

- Keep INA polling owned by the profiler.
  This avoids multiple UI features competing for the I2C bus.

- Store voltage and current only.
  Power is derived on demand, which saves PSRAM and keeps each sample compact.

- Use three fixed-rate PSRAM tiers.
  The 100 Hz tier keeps short high-resolution detail, while the 10 Hz and 1 Hz
  tiers extend the useful history window without large JSON responses.

- Keep history in RAM rather than flash.
  This avoids flash wear and keeps writes cheap enough for high-rate capture.

- Use the existing settings and JSON patterns.
  UI preferences and averaging go through the same storage and API flow as other
  hub settings.

- Keep browser caching separate from device history.
  The device remains the source of truth, while the browser cache improves
  scrolling and redraw responsiveness.

- Decimate long responses before sending JSON.
  This caps response size and render cost when the user views long windows.

- Stream history JSON directly to the web response.
  This avoids building large Arduino `String` payloads and keeps heap pressure
  bounded while serving graph data.

## TODO

- Add a lightweight way for the browser to request only missing cache ranges.
- Consider reporting history fetch timing or throttling state for diagnostics.
- Revisit whether any history should survive reboot once a flash-safe storage
  strategy exists.
- Add automated checks around history window clamping and offset behavior.

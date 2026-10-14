# Port Profiler

Reads voltage and current on every powered port at a fixed 100 Hz, in a
background task that runs all the time, from boot, and writes every reading straight into the PSRAM port
history (`g_history`, `firmware/src/history/PortHistory.*`). The history is the
only store of port readings: the graphs, `/api/status`, the REST port
responses and the serial `info` and `mon` commands all read from it. Code is in
`firmware/src/main.cpp`, under `// ---- Port profiling ----`.

## Functions

| Function | Does |
|---|---|
| `void profilerStart()` | Starts the sampling task. Called once from `setup()`; the task never stops. Does nothing if it's already running, or if the PSRAM history couldn't be allocated. |
| `bool profilerSetAveraging(int samples)` | Sets how many readings `profilerGetPort()` averages: `0`, `2`, `4`, `8` or `16`. Returns `false` and changes nothing for any other value. Default `0`. |
| `ProfileSample profilerGetPort(int port)` | Returns one port's reading (below). Use it for every port reading. |

The selected averaging value is stored in the normal settings system under
`profiler.averaging`. The web UI sets it from the graph's averaging buttons,
through `POST /api/settings`; it isn't in the Settings panel. It's saved to
flash once it has gone unchanged for 3 seconds (`PROFILER_SAVE_DELAY_MS`).

## `profilerGetPort(int port)`

`port` is the port index, **0–5** (port 1 = 0).

Returns a `ProfileSample`:

```cpp
struct ProfileSample
{
    float volts; // port voltage, V
    float ma;    // port current, mA
    float mw;    // power, mW — calculated as volts × ma
};
```

Reads the port's newest 16 slots in the
history (160 ms) and skips invalid ones.

- **Averaging `0`:** the newest valid reading.
- **Averaging `2`, `4`, `8` or `16`:** the mean of the newest 2, 4, 8 or 16 valid
  readings. Just after a port powers on, fewer exist, and it averages those.
- **`{0, 0, 0}`** when none of those slots is valid.
- It only reads the history, so any number of callers can use it at once and
  all get the same answer.

**If the profiler isn't running** (the PSRAM history couldn't be allocated
at boot): reads the port straight from its INA3221, with no averaging.
**`{0, 0, 0}`** when its INA3221 wasn't found at boot.

**Always `{0, 0, 0}`** when `port` is outside 0–5 or the port is off. An off
port is never read.

## Sampling

- The task runs once per 10 ms period. A period's **capture count** is the
  number of 10 ms periods since boot, so its time is count × 10 ms.
- Each period it reads two INA3221 registers (bus voltage, shunt voltage) per
  **powered** port, and writes one slot for **every** port: the raw reading, or
  an invalid slot when the port is off, its INA3221 wasn't found or the read
  failed.
- It sleeps until 1 ms into the next period, so it can run up to 9 ms late and
  still read in its own period. A cycle that runs later than that leaves
  the period it missed invalid, rather than storing a reading under the wrong
  time.
- It compares each port's power and device state with the previous period's,
  and adds any change to that port's event list at the current capture count:
  power off (1), power on (2), device removed (3), device inserted (4).

## History

- 100 Hz only, 60 minutes per port: 360,000 slots per port, 3 bytes each,
  6,480,000 bytes for six ports, in PSRAM.
- Each slot: 1 valid bit, 10-bit bus voltage (8 mV steps, 0–8.184 V), 13-bit
  signed shunt voltage (40 µV steps). No timestamp: a slot's time comes from its
  position.
- Events: up to 256 per port, `{capture count, code}`, the oldest overwritten.
- One writer (the profiler task), any number of readers. A reader only returns
  a slot that was fully written and hasn't been overwritten while it was being
  copied.
- PSRAM only: kept across web reconnects, lost on reboot.
- Sent to the web UI raw, in binary pages of up to 10 minutes
  (`PortHistory::page()` and `readRaw()`; layout in `rest_api.md`).

## Rules

- `profilerGetPort()` is safe from any task. It doesn't touch the I2C bus,
  except when the profiler isn't running, when it reads the INA3221 under
  `I2CLock`.
- The profiler shares the I2C bus with the expander, the AHT20 and the other
  INA3221 reads, so all I2C access holds the bus lock, `I2CLock`
  (`ports/USBPort.h`).

  `I2CLock` is a scope guard around a recursive FreeRTOS mutex. Declaring it
  takes the mutex, waiting if another task has it. The mutex is released
  automatically when the block it was declared in ends — at the closing `}`,
  or on any `return` out of the block:

  ```cpp
  {
      I2CLock lock;          // takes the mutex
      ioex.write(pin, LOW);  // bus is ours
  }                          // block ends: mutex released
  ```

  Keep the block to the I2C transfer itself. Don't put a delay or a
  busy-poll inside it: the profiler can't read while the lock is held.

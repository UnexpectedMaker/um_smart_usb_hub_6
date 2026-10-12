# Port Profiler

Samples voltage and current on every powered port at a fixed 100 Hz, in a
background task, and keeps the last 8 samples per port. Code is in
`firmware/src/main.cpp`, under `// ---- Port profiling ----`.

## Functions

| Function | Does |
|---|---|
| `void profilerStart()` | Starts the sampling task. Does nothing if it's already running. |
| `void profilerStop()` | Stops the task and returns once it has exited (within 10 ms). Clears all samples. Does nothing if it isn't running. |
| `bool profilerSetAveraging(int samples)` | Sets how many samples `profilerGetPort()` averages: `0`, `2`, `4` or `8`. Returns `false` and changes nothing for any other value. Default `0`. |
| `ProfileSample profilerGetPort(int port)` | Returns one port's reading (below). |

The selected averaging value is stored in the normal settings system under
`monitor.averaging` and can be changed through `POST /api/settings`.

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

- **Averaging `0`:** the newest sample.
- **Averaging `2`, `4` or `8`:** the mean of the newest 2, 4 or 8 samples.
  Just after a port powers on, fewer samples exist, and it averages those.
- **`{0, 0, 0}`** when:
  - `port` is outside 0–5
  - the port is off
  - the port's INA3221 wasn't found at boot
  - the profiler isn't running, or hasn't sampled the port yet

## Sampling

- The task always runs at 100 Hz, whatever rate you read at. To get 50 Hz or
  10 Hz data, call `profilerGetPort()` every 20 ms or 100 ms.
- Each cycle reads two INA3221 registers (bus voltage, shunt voltage) per
  **powered** port. Ports that are off aren't read, and their samples are
  cleared.
- The samples are a ring buffer of 8 per port. The newest overwrites the oldest.
- The web status, REST port responses and graph history use this cached profiler
  data, so normal UI refreshes do not perform extra INA3221 reads.
- Graph history stores profiler voltage/current snapshots in PSRAM at 100 Hz,
  10 Hz and 1 Hz tiers. Power is calculated when history is served. The
  profiler remains the only service that polls the INA devices.

## Rules

- Call `profilerStart()` / `profilerStop()` from normal task code
  (`loop()`, a web handler). Never call `profilerStop()` from inside the
  profiler task: it would wait forever.
- `profilerGetPort()` is safe from any task. It doesn't touch the I2C bus.
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

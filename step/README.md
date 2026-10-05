# 3D files

STEP models of the Smart USB Hub board and its case, ready to print or remix.
Most CAD tools open STEP files directly.

| File | What it is |
|---|---|
| `UM 6 Port USB HUB R3.step` | The R3 board — use it to design your own case or mount |
| `CASE/USB 6 Port Smart Hub Case Top.step` | Case top |
| `CASE/USB 6 Port Smart Hub Case Bottom.step` | Case bottom |
| `CASE/Light_Pipe_Insert.dxf` | Light pipe insert that carries each port's RGB LED through the case |

The KiCad project for the board is in [`../hardware`](../hardware/).

## Printing your own case

The case from the [Unexpected Maker store](https://unexpectedmaker.com/shop/smarthub)
is 3D printed in matt black ABS. Print the top and bottom from the STEP files
in `CASE/`.

### Light pipe insert

`Light_Pipe_Insert.dxf` is a 2D outline. Make it either way:

- **Laser cut** it from **2 mm thick clear acrylic**, or
- **3D print** it: extrude the outline to **2 mm** and print it in a clear
  material.

### Hardware you'll need

| Qty | Part | Notes |
|---|---|---|
| 3 | M2.5 heat-set threaded inserts | Any height — there's plenty of clearance |
| 3 | M2.5 screws | Anything from 6 mm up to 12–16 mm long — there's plenty of room |
| 4 | 6 mm diameter stick-on rubber feet | Optional |

## Licence

Copyright (c) 2026 [Unexpected Maker](https://unexpectedmaker.com).

Licensed under the CERN Open Hardware Licence Version 2 — Strongly Reciprocal
(CERN-OHL-S v2). See [LICENSE](LICENSE).

If you distribute these files, a modified version of them, or anything made
from them, you must make the complete source available under the same licence.

Source location: <https://github.com/UnexpectedMaker/um_smart_usb_hub_6>

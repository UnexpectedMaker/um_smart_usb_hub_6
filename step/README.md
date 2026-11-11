# 3D files

STEP models of the Smart USB Hub board and its case, ready to print or remix.
Most CAD tools open STEP files directly.

| File | What it is |
|---|---|
| `UM 6 Port USB HUB R3.step` | The R3 board — use it to design your own case or mount |
| `CASE V2/USB 6 Port Smart Hub Case Top V2.step` | Case top, V2 |
| `CASE V2/USB 6 Port Smart Hub Case Bottom V2.step` | Case bottom, V2 |
| `CASE V2/Light_Pipe_Insert.dxf` | Light pipe insert that carries each port's RGB LED through the case |
| `CASE/` | The original case, with the same three files |

The KiCad project for the board is in [`../hardware`](../hardware/).

## Case V2

V2 is easier to print and goes together better than the original case:

- **No supports needed.** Both parts print without supports.
- **Light pipe guides.** The top has guides that hold the acrylic light pipes
  in place and keep them straight, so they no longer need to be glued.
- **Better PCB fit.** The bottom has extra support for the PCB, and more
  tolerance for the board outline. JLCPCB's routing isn't always consistently
  accurate, so the extra room lets everything fit together nicely.

The light pipe insert is unchanged from the original.

## Printing your own case

The case from the [Unexpected Maker store](https://unexpectedmaker.com/shop/smarthub)
is 3D printed in matt black ABS. Print the top and bottom from the STEP files
in `CASE V2/`.

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

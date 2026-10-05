# Hardware

KiCad 10 project for the Unexpected Maker 6 Port Smart USB Hub (R3).

Open `UM 6 Port USB HUB R3/UM 6 Port USB HUB R3.kicad_pro` in KiCad 10 or later.

## Self-contained project

Everything that is not part of a standard KiCad installation is included in the project folder. No external or personal libraries are needed, and there are no absolute paths in any project file.

- **Footprints** – every non-stock footprint used on the board lives in the project library `UM_Hub.pretty`. It is registered through the project `fp-lib-table` using `${KIPRJMOD}`, so KiCad picks it up automatically when the project is opened.
- **3D models** – every non-stock model is in `3d models/` and is referenced relative to the project (`${KIPRJMOD}/3d models/...`). The 0805 resistor model is embedded in its footprint.
- **Stock parts** – the remaining footprints and models come from the default libraries installed with KiCad (`Capacitor_SMD`, `Package_DFN_QFN`, `Package_TO_SOT_SMD`, `RF_Module`, `MountingHole` and the standard `KICAD*_3DMODEL_DIR` models).
- **Symbols** – the schematic carries its own copy of every symbol it uses, so it opens and edits without any external symbol libraries.

## Contents of `UM 6 Port USB HUB R3/`

| Item | What it is |
|---|---|
| `UM 6 Port USB HUB R3.kicad_pro` | KiCad project file |
| `UM 6 Port USB HUB R3.kicad_sch` | Schematic |
| `UM 6 Port USB HUB R3.kicad_pcb` | PCB layout |
| `UM_Hub.pretty/` | Project footprint library (17 footprints) |
| `fp-lib-table` | Registers `UM_Hub` for this project |
| `3d models/` | STEP models for the non-stock parts |
| `bom/ibom.html` | Interactive HTML BOM |

## Manufacturing

If you are getting this board manufactured, the PCB stackup must match the following so that the USB differential pairs are impedance matched.

PCB Stackup: JLC04161H-3313 (Finished thickness 1.56mm ±10%)

90R Diff Pair Impedance:
- Trace 0.1545mm
- Gap 0.232mm
- Co-Planar Clearance 0.194mm

## Licence

Copyright (c) 2026 [Unexpected Maker](https://unexpectedmaker.com).

The hardware design files are licensed under the CERN Open Hardware Licence
Version 2 — Strongly Reciprocal (CERN-OHL-S v2). See [LICENSE](LICENSE).

If you distribute this design, a modified version of it, or hardware made from
it, you must make the complete design source available under the same licence.

Source location: <https://github.com/UnexpectedMaker/um_smart_usb_hub_6>

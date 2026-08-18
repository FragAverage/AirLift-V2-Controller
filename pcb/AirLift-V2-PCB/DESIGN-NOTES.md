# AirLift V2 + MFL Controller — PCB Design Notes

Generated PCB (schematic + layout + routing) for the AirLift V2 + MFL controller,
built from `AirLift-V2-PCB-DesignSpec.md`. This file documents judgment calls,
known warnings, and items a human should verify before sending to fab.

## Status

- Schematic: **ERC clean — 0 errors**, 6 cosmetic `lib_symbol_mismatch` warnings
  (embedded copies of stock diode/transistor symbols differ trivially from the
  live library copies; harmless, does not affect netlist or fab).
- PCB: **DRC clean — 0 errors, 0 unconnected nets**, 2 cosmetic warnings
  (`silk_overlap` / `silk_over_copper` between Q_REV's silkscreen and C_BULK's
  reference designator text — purely visual, does not affect fabrication or
  assembly; can be nudged in the GUI if desired).
- Board size: ~130 x 115 mm (compact — down from an original 247 x 104 mm
  strip, then a 156 x 149 mm grid, tightened further by packing more zones
  in 2 sub-columns and shrinking the gap between functional blocks from
  10mm to 6.5mm). Components are grouped into a functional grid: connector +
  power protection + output driver in one column near the harness connector,
  the ESP32 DevKit carrier as a central spine, LIN/CAN bus transceivers
  grouped together, and the MFL optocoupler + ignition-sense analog inputs
  kept farthest from the power/switching section. The layout is now solved
  programmatically from each zone's real footprint bounding box (see
  `_solve_grid()` in `build_pcb.py`) rather than hand-tuned coordinates, so
  further size/spacing tweaks (edit `GAP`/`MARGIN` at the top of that file)
  automatically reflow the whole board without manual recalculation. The
  schematic mirrors the same grouping with a 3x3 grid of function blocks
  (was previously a single row spanning 2 meters).
- High-current nets (PWR_IN, +12V_PROT, OUTPUT1 — rated up to 5A) are routed on
  a dedicated `Power5A` netclass at **1.5mm trace width / 0.3mm clearance**;
  all other signal/logic nets use the default 0.2mm class.

## Judgment calls made during design

1. **Reverse-battery protection**: implemented as a P-MOSFET "ideal diode"
   (Q_REV = IRF4905, gate pulled to GND via 10k) rather than a series diode,
   to avoid the ~0.5-0.7V forward-drop heat dissipation at up to 5A that a
   diode would incur.
2. **3.3V rail**: added a local AMS1117-3.3 LDO (U_33) rather than relying on
   the ESP32 DevKit's onboard 3.3V regulator/pin, since the devkit's own
   regulator may not have headroom for additional board loads (LIN/CAN
   transceivers, optocoupler pull-ups, etc.) — this was not explicitly
   specified in the requirements doc and is a conservative addition.
3. **MFL input isolation**: PC817 optocoupler (U_OPTO) plus a TVS clamp
   (SMAJ26A, D3) on the car-side input, with a 1k series resistor (R1) sized
   for a nominal 12V input into the optocoupler LED. Verify R1/R2 values
   against your actual MFL button voltage divider before final assembly —
   the spec's MFL signal characteristics were not fully pinned down.
4. **Ignition-sense input**: resistor divider (R9/R10) with a zener clamp
   (D_Z1, BZX84C3V6) sized to the DevKit's ADC input range, since alternator
   ripple can exceed 15V transiently.
5. **Extends-based symbols flattened**: IRF4905 and other library symbols
   that use KiCad's `extends` (symbol inheritance) were flattened (base
   symbol's graphics/pins copied directly in) because `kicad-cli` 9.0.9's
   headless ERC fails to load schematics containing `extends`-based symbols.
   This is a tooling workaround, not a functional/electrical change — it is
   the source of the 6 cosmetic `lib_symbol_mismatch` ERC warnings above.

## Known open items — please review before fab

1. **ESP32 DevKit header spacing — MUST BE MEASURED ON YOUR BOARD (blocking)**:
   `J_DEVKIT_L`/`J_DEVKIT_R` are two 1x15 female sockets meant to receive the
   DevKit's two male pin rows like a socketed shield. They are placed
   side-by-side, pin-1-to-pin-1 aligned (confirmed correct against the DOIT
   ESP32 DevKit V1 pinout), spaced **25.4mm (1.00") center-to-center** —
   but that number is an **unconfirmed placeholder**, not a verified
   datasheet value. "ESP32 DevKit V1" is sold under one name by many
   vendors with genuinely different board widths (checking two independent
   pinout references for nominally the same board turned up 28.2mm and
   23.37mm overall board width, respectively — a real, well-known mismatch
   between clones). If the real spacing on your physical board differs from
   25.4mm even by a millimeter, the DevKit will not seat in these sockets.
   **Before fab: measure center-to-center pin spacing on your actual board
   with calipers** (or count grid holes if you know the pitch) and tell me
   the number so I can correct `DEVKIT_ROW_SPACING_MM` in `build_pcb.py`
   and re-route.
2. **Connector current rating vs. spec (needs your decision)**: the requirements
   doc specifies up to 5A on `OUTPUT1` (J1 pin 10), but the connector used
   (JAE MX23A12NF1, 2.5mm pitch, 2x6, right-angle) is rated **3A per contact**
   per its datasheet. Options: (a) confirm 5A is actually a transient/peak
   rating your load never sustains, (b) parallel two contacts for OUTPUT1 if
   the connector pinout allows it, or (c) switch to a connector family rated
   ≥5A per contact. This was flagged in the original design spec and is not
   resolved in this layout — the board currently routes OUTPUT1 as a single
   3A-rated contact carrying a 5A-spec load.
3. **Custom/unverified footprint**: `AirLift_Custom:MX23A12NF1_2x6_P2.50mm_RA`
   was hand-built from the JAE MX23A12NF1 datasheet drawing and has **not**
   been verified against a real part or manufacturer footprint library.
   Recommend cross-checking pad size/pitch/keepout against the datasheet (or
   ordering the connector and doing a fit-check) before committing to fab.
4. **MFL divider values (R1/R2) and IGN divider values (R9/R10)** are
   reasonable placeholders based on typical automotive signal levels — verify
   against actual measured signals from the target vehicle harness.
5. **Cosmetic silkscreen overlap** near Q_REV/C_BULK (see Status above) — no
   functional impact, but worth a quick nudge in the KiCad GUI if you want a
   cleaner silkscreen for the first article.
6. **Reference designators use a descriptive naming scheme** (e.g. `U_LIN1`,
   `Q_REV`, `C_BULK` instead of `U1`, `Q1`, `C1`) for readability during
   design. These are valid KiCad references, but some BOM tools flag
   non-numeric-suffixed refs as "unannotated" (shown as e.g. `C_BULK?` in the
   exported BOM). This is cosmetic — nets and footprints are correctly
   assigned regardless — but if your fab/assembly house wants strictly
   sequential `U1, U2, ...`-style refs, run KiCad's "Annotate Schematic" tool
   before ordering.
7. **Isolation clearance around the MFL optocoupler barrier**: the design
   spec calls for keeping car-side (9-15V) and MCU-side (3.3V) copper
   separated across U_OPTO. Component placement keeps the two sides in
   separate zones, but the routed board has not been given a manual
   creepage/clearance audit beyond the standard DRC clearance rules — worth
   a visual check in the KiCad 3D/layer viewer before fab if this will see
   real automotive transients.

## Deliverables in this package

- `AirLift-V2-PCB.kicad_sch` / `.kicad_pcb` / `.kicad_pro` — full KiCad 9 project
- `AirLift_Custom.kicad_sym` / `AirLift_Custom.pretty/` — custom TJA1020 symbol
  and MX23A12NF1 footprint
- `deliverables/gerbers.zip` — Gerber (RS-274X) + Excellon drill files
- `deliverables/BOM.csv` — bill of materials (Reference, Value, Footprint)
- `deliverables/pick_and_place.csv` — component placement (pos) file, both sides
- `deliverables/pdf/schematic.pdf` — full schematic printout
- `deliverables/pdf/board.pdf` — board layer printout (Cu/silk/mask/edge)
- `routed_final_render.png` — 3D render of the finished, routed board

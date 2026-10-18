# Joymap v4 mapping regressions

The unified mapping stack includes GamecontrollerDB defaults, the guided mapping
UI, stick inversion, and analog L2/R2 delivery. System maps retain the existing
v3 fields and append the v4 button/trigger fields.

## Corrected behavior

- A mouse-only mapping session starts from the effective system map, so saving
  mouse changes retains a fresh controller's GCDB joypad and trigger defaults.
- Mouse skip/Previous actions do not clear L2/R2 axes. Gamepad trigger unmapping
  still clears the corresponding analog trigger.
- Trigger tokens preserve half selection independently of inversion. `+aN` and
  `-aN` select the positive and negative halves; `~` reverses travel within that
  half. Values outside the selected half are released. Export retains the suffix.
- Full-range `aN` runs minimum-to-maximum and `aN~` runs maximum-to-minimum.
  Polarity no longer depends on whether a trigger is held during enumeration.
  Controllers with reversed ranges need an explicit inverted GCDB/user mapping.
- Inverted trigger halves sharing one physical axis can independently hold both
  synthetic digital trigger keys at the center. Distinct curves sharing the
  same half receive separate digital keys; identical bindings may intentionally
  alias. Releases precede new presses.
- Combined Joy-Cons retain saved v4 stick inversion. Only inherited single-Joy-Con
  defaults receive the combined-pair orientation adjustment.
- OSD/framebuffer stick navigation uses the same inversion as gameplay.
- Skipping the initial DPAD detector skips its Right/Down pair. For a claimed
  mapping device, Previous returns to the initial detector when no horizontal
  stage was captured. Re-entering it discards the old Down snapshot, including
  digital-DPAD/hat recognition, keeping later stick X/Y stages in order.
- Core-selector and Misc return paths use the named Exit/Help menu entries.

## Menu button: tap, combo or hold

The gamepad Menu (OSD) binding is system-map slots 21 and 22
(`SYS_BTN_OSD_1/2`), little-endian `uint32` at file offsets 0x54 and 0x58:

| Mode | Slot 21 | Slot 22 | Bytes at 0x54 |
|---|---|---|---|
| Tap (Guide, `BTN_MODE`) | `0x013C` | `0x013C` | `3C 01 00 00 3C 01 00 00` |
| Combo (Select + Start) | `0x013A` | `0x013B` | `3A 01 00 00 3B 01 00 00` |
| Hold 2 s (Select) | `0xFFFF` | `0x013A` | `FF FF 00 00 3A 01 00 00` |

- In hold mode, `OSD_HOLD_MARKER` occupies the low half of slot 21. Its high
  half is reserved for the hold time in 100 ms units; 0 means 2 s.
- No key code reaches 0xFFFF; synthetic axis keys end at 0x37F. Code that
  compares event codes with slot 21 therefore never matches the marker. A v4
  build without hold support sees no gamepad Menu button (keyboard F12 still
  works). Upstream builds read `*_v3.map` and never see the marker.
- At runtime a hold-mode button acts normally in the core. Held for the hold
  time (polled in `input_poll()`), it sends the Menu press; its release ends the
  press. When that press opened the OSD, the release is not passed to the OSD.
- Holding it again for the hold time closes the OSD.
- In a core's own mapping screen, a tap maps the button and a completed hold
  then release cancels (Menu-hold).
- At the Menu core's Menu prompt:
  - tap a button for a single button;
  - press a second button within 2 s for a combo;
  - or hold one button alone for 2 s for hold mode.
  Line 2 shows the hold filling, then `Menu: hold 2s`.
- GamecontrollerDB export writes a hold button as `guide`, since SDL strings
  have no hold.

## PlayStation button names

Core mapping stays on the virtual SNES layout. In a core's `J1`/`jn`/`jp`
button names, `map_joystick()` also accepts PlayStation names for the same
positions:
- `Circle` = A
- `Cross` = B
- `Triangle` = X
- `Square` = Y
- `L1` = L
- `R1` = R

`L2`, `R2`, `L3` and `R3` keep their own slots. A plain `X` is always the
SNES X, never the PlayStation Cross.

## Host regression suite

Run on Linux/WSL with Python 3 and a C++ compiler:

```sh
python3 tools/test_joymap_v4.py --output bin/joymap-v4-tests
python3 tools/test_joymap_v4.py --output bin/joymap-v4-tests-asan --sanitize
```

The harness compiles the production `input.cpp` and `gamecontroller_db.cpp` with
synthetic evdev/GCDB input and in-memory configuration storage. Unsigned `char`
matches the ARM target. It checks partial-map save/load, skip/Previous state,
trigger normalization and export, held-input cache lookups, shared-axis digital
transitions, Joy-Con orientation, and OSD directions. It also checks the
Menu-button hold mode against a fake millisecond clock:
- encoding;
- tap versus hold;
- capture at the Menu prompt, including combo-first, early release and
  Previous;
- save and load;
- GamecontrollerDB export.
Source guards cover its `input_cb()`/`input_poll()` wiring. Ordinary-mapping
controls and deliberately wrong expectations exercise the checker itself.

Joy-Con/OSD integration and synthetic event emission use source-derived seams;
the two menu return paths use source guards, not a complete UI emulator. Unknown
source shapes fail closed. `--revision <commit>` reads a Git source snapshot for
red-baseline comparison without changing the working tree. Generated source
snapshots, manifests, logs, and binaries belong in ignored evidence directories.

## Remaining acceptance gates

Host tests and an ARM build do not qualify physical controllers. Check the exact
built binary on MiSTer for fresh GCDB mouse-only save/reload, mouse skip/Previous,
held-trigger USB hotplug, combined Joy-Con saved inversion, inverted OSD and
framebuffer navigation, DPAD skip/back, and the two menu return highlights.
Verify analog and digital trigger delivery with a core supporting `UIO_ATRIG`.
For Menu hold mode, check on hardware:
- capture at the Menu prompt (tap, combo and hold);
- a hold opening and closing the OSD in a core, with the button still working
  in the game on a tap;
- a hold cancelling a core's button definition;
- the lightgun and mouse-mode toggles.

Generic GCDB digital-button bindings to inverted half-axes have a pre-existing
representation limitation: swapping an axis's synthetic minimum/maximum key
cannot express all pressed-near-center semantics. The L2/R2 trigger metadata and
normalization corrections do not redesign that unrelated digital-button path.

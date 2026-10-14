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
transitions, Joy-Con orientation, and OSD directions. Ordinary-mapping controls
and deliberately wrong expectations exercise the checker itself.

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

Generic GCDB digital-button bindings to inverted half-axes have a pre-existing
representation limitation: swapping an axis's synthetic minimum/maximum key
cannot express all pressed-near-center semantics. The L2/R2 trigger metadata and
normalization corrections do not redesign that unrelated digital-button path.

# SFZ Import (v1 scope)

## Headers
Support `<control>`, `<global>`, `<master>`, `<group>`, `<region>` with inheritance.

## Core opcodes
- `<control>`: `default_path`
- `<region>`: `sample`, `lokey`, `hikey`, `key`, `pitch_keycenter`, `lovel`, `hivel`, `transpose`, `tune`, `offset`, `end`, `loop_start`, `loop_end`, `loop_mode`, `volume`, `pan`, `seq_position`, `seq_length`, `seq_mode`, `xfin_lovel`, `xfin_hivel`, `xfout_lovel`, `xfout_hivel`, `off_by`, `trigger`

## Current importer behavior notes
- `seq_position` regions are imported as ordered round-robin positions within a generated group.
- `seq_length` is imported into ordered round-robin playback, so missing sequence slots stay silent instead of wrapping to another region.
- `seq_mode=random` is imported into Audiocity's existing cycle-random round-robin mode, and the importer now assigns a round-robin group even when no explicit `seq_position` values are present.
- `trigger=release` regions are imported and fire on MIDI note-off instead of note-on.
- `loop_mode=loop_continuous` regions keep looping after note-off, and that import-to-playback path is now covered by offline tests.
- `loop_mode=one_shot` maps to one-shot trigger behavior with no active loop.
- Velocity crossfades (`xfin_*`, `xfout_*`) are imported into the zone model, preserved through Mapping edits/state restore, and now covered by offline import-to-playback tests across low, mid, and high velocities.

## Behavior
- Unknown opcodes are ignored but recorded in diagnostics.
- Missing sample files: region is skipped and a diagnostic is emitted.
- Unsupported values for known opcodes such as unsupported `trigger`, `loop_mode`, and `seq_mode` values emit warnings so imports do not fail silently.

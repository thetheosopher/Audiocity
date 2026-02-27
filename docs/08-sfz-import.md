# SFZ Import (v1 scope)

## Headers
Support `<control>`, `<global>`, `<master>`, `<group>`, `<region>` with inheritance.

## Core opcodes
- `<control>`: `default_path`
- `<region>`: `sample`, `lokey`, `hikey`, `key`, `pitch_keycenter`, `lovel`, `hivel`, `transpose`, `tune`, `offset`, `loop_start`, `loop_end`, `loop_mode`

## Behavior
- Unknown opcodes are ignored but recorded in diagnostics.
- Missing sample files: region is skipped and a diagnostic is emitted.

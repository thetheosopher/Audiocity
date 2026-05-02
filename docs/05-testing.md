# Testing Strategy

## Offline render harness
- Runs engine without an audio device.
- Feeds timestamped MIDI into blocks.
- Writes output WAV for comparisons.

## Golden tests
- Compare hashes or error thresholds.
- Fixtures cover:
  - envelopes
  - voice stealing
  - looping
  - round robin determinism
  - quality-tier interpolation differences, determinism, and runtime switching across CPU/Fidelity/Ultra playback modes
  - preload segmentation and runtime preload changes for single-sample and imported-program playback
  - single-sample file-backed disk streaming across preload rebuilds
  - imported-program bounded disk-stream cache, processor-worker prime servicing, and note-on/lookahead hit-miss telemetry
  - SFZ import (#include/default_path/seq_length/release-trigger)
  - Mapping state structural round-trip, imported-program state subtree round-trip, legacy replay fallback, and imported-program derived-state summaries
  - Mapping zone operations and velocity fade edits
  - atomic imported-program batch mapping apply/delete rollback semantics
  - chronological editor undo-history behavior across imported-program mapping snapshots and sample/settings edits, including coalescing, undo labels, and duplicate/split structural zone operations
  - SFZ diagnostics for unsupported opcode values

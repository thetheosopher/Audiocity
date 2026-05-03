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
  - quality-tier resampler differences, determinism, runtime switching across CPU/Fidelity/Ultra playback modes, and an objective high-frequency spectral preservation check that requires Ultra to retain more main-tone energy with lower side-energy than Fidelity at a non-integer pitch ratio
  - preload segmentation and runtime preload changes for single-sample and imported-program playback
  - single-sample file-backed disk streaming across preload rebuilds
  - imported-program bounded disk-stream cache, processor-worker prime servicing, and note-on/lookahead hit-miss telemetry
  - SFZ import (#include/default_path/seq_length/release-trigger)
  - Mapping state structural round-trip, imported-program state subtree round-trip, legacy replay fallback, and imported-program derived-state summaries
  - Mapping zone create, duplicate, split, delete, chromatic remap, key-range spread, root-note derivation from key centers, and velocity fade edits, including explicit sample-asset selection for new zones
  - atomic imported-program batch mapping apply/delete rollback semantics
  - chronological editor undo-history behavior across imported-program mapping snapshots and sample/settings edits, including coalescing, undo labels, and create/duplicate/split structural zone operations
  - real-time modulation routing for mod wheel, aftertouch, velocity, and two macro controls across pitch, filter, and amp destinations, including processor parameter/state plumbing and the first in-plugin modulation surface
  - REX slice decoding, transient slice-program construction for regular samples, manual sample-slice splitting/merging, chromatic slice-program import from `.rex/.rx2`, map-to-root-note snapping, and imported-program path compatibility across generic, explicit sample-slice, and legacy state properties
  - SFZ diagnostics for unsupported opcode values

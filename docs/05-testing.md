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
  - preload segmentation and runtime preload changes for single-sample and imported-program playback
  - SFZ import (#include/default_path/seq_length/release-trigger)
  - Mapping state structural round-trip, imported-program state subtree round-trip, legacy replay fallback, and imported-program derived-state summaries
  - Mapping zone operations and velocity fade edits
  - SFZ diagnostics for unsupported opcode values

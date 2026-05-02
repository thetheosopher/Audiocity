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
  - SFZ import (#include/default_path/seq_length/release-trigger)
  - Mapping state structural round-trip and imported-program state subtree round-trip
  - Mapping zone operations and velocity fade edits
  - SFZ diagnostics for unsupported opcode values

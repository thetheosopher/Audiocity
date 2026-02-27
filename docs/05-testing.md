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
  - SFZ import (#include/default_path)

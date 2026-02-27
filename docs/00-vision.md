# Vision — Audiocity

## Product
Audiocity is a high‑performance sampler built as a single JUCE `AudioProcessor` and shipped as:
- Standalone Windows app
- VST3 instrument plugin

## Principles
- Real-time safety: no allocations, locks, file I/O, or logging on the audio thread.
- Spec-first: docs drive the implementation.
- Tests-first for risky behavior: offline render + golden tests.

## Non-goals
- Standalone third-party FX hosting (DAW provides FX in plugin mode).

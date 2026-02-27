# Architecture — Audiocity

## High-level
- The sampler engine lives behind a JUCE `AudioProcessor`.
- The same processor is built as VST3 and run in a standalone shell.

## Threads
- **Audio thread**: voice rendering, mixing, (built-in) DSP. Strict RT rules.
- **UI/message thread**: UI interaction and parameter edits.
- **Worker threads (post-MVP)**: sample decoding, disk streaming, indexing.

## State model
- UI writes parameter/state changes into a lock-free queue or double-buffered snapshots.
- Audio thread reads immutable snapshots per block.

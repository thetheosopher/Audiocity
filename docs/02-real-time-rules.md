# Real-time Rules (Hard Constraints)

## Forbidden on audio thread

- Heap allocation (`new`, `malloc`, growing `std::vector`)
- Locks/mutexes/condition variables
- File I/O
- Logging
- Plugin scanning/instantiation

## Required patterns

- Pre-allocate voices/buffers in `prepareToPlay()`.
- Use lock-free queues for events/state.
- Keep `processBlock()` deterministic and bounded.

## Thread ownership

| State | Owner/writer | Readers and transfer mechanism |
| --- | --- | --- |
| Engine controls and live voices | Audio thread only | UI/host writes APVTS atomics. `processBlock()` loads one `EngineControlSnapshot`, diffs it with the last applied snapshot, and applies only changed groups. UI getters read APVTS; active-voice UI state comes from audio-to-UI atomics. |
| UI MIDI and panic requests | UI/host producer | A bounded lock-free FIFO transfers note requests, while a coalescing atomic latch makes panic lossless under FIFO pressure. `EngineCore::noteOn`, `noteOff`, and `panic` run on the audio thread. |
| Sample/program/segment data | Serialized non-audio structural writer | Writers build immutable snapshots and publish them through `RtSnapshotCell`. Fixed audio/worker/message reader epochs defer reclamation until a quiescent point, and the audio thread never takes the structural writer mutex. Publication generations stop voices before a new sample/program generation is rendered; a program sequence also prevents pairing partially published metadata/audio generations. |
| Display metadata and waveform copies | Serialized non-audio writer | UI/state code reads display state; it is not used as mutable render state. |
| Import and library-scan jobs | Editor-owned `OwnedJobWorker` instances | Each service has one active job and at most one replacement. Submit cancels the active generation and replaces the queued generation. Editor teardown cancels and joins both workers before component references are destroyed. |

`EngineCore::loadPreparedSample()` and `setSampleData()` remain single-thread convenience APIs for engine-only tests/tools. The plugin boundary uses `publishPreparedSample()` and `publishSampleData()`, which publish structural data without changing live controls.

## Cancellation bounds for owned jobs

- Directory traversal checks cancellation before every file. Peak generation checks before every 4,096-frame decoder read and every output bucket.
- Ordinary sample/audio import checks before every 65,536-frame decoder read. Binary/container file reads check every 1 MiB. XML/record/asset traversal checks between elements or records.
- REX work checks while waiting for the SDK mutex, between slice-info calls, before and after each slice render, and during 1 MiB file reads. An individual vendor `REXRenderSlice` call cannot be interrupted; teardown joins after that one slice call returns.
- Cancellation reaches a terminal state at the next boundary above. There is no unowned/detached fallback, and callbacks validate both a `SafePointer` and the current generation before publishing.

## Verification

- `audiocity_preset_runtime_smoke` asserts that unchanged blocks perform zero additional control-group applies, then combines rapid APVTS-style writes, immutable sample replacement, and MIDI rendering while checking finite output.
- `audiocity_offline_tests` verifies serial replacement, cancellation-aware chunk reads, and destructor cancel/join behavior.

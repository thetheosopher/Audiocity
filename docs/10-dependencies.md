# Dependencies (suggested)

> This project is primarily personal/internal, so pragmatic choices are acceptable.

## MVP
- **doctest** or **Catch2**: unit tests for offline render harness.
- **nlohmann/json**: patch/settings/diagnostics serialization.

## v1 (Browser + Mapping + SFZ)
- **SQLite**: browser index, tags, favorites, peaks cache.
- **dr_wav**: lightweight WAV preview/import.
- **TagLib** (optional): metadata browsing.
- **Reason Studios REX SDK 1.9.2**: `.rex` / `.rx2` import (requires `REX Shared Library.dll` deployed beside plugin/standalone binaries).

## v1.1 (Quality + Streaming)
- **libsamplerate**: HQ resampling tier.
- **libsndfile** (optional): broader file format import.
- **libsoxr** (optional): alternative HQ resampler.

## v2
- **KISS FFT**: FFT for slicing/analysis.

## v3
- **sfizz** (optional): SFZ compatibility reference.

## Policy
- Pin versions (tags/commits) and record in `DEPENDENCIES.md`.
- Keep the audio-thread dependency surface minimal.
- For optional runtime codecs (like REX), fail soft and keep feature disabled when runtime binaries are unavailable.

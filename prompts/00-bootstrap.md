# Prompt: Bootstrap Audiocity

Copy/paste into Copilot Chat:

---
Read `docs/00-vision.md`, `docs/01-architecture.md`, `docs/02-real-time-rules.md`, `docs/03-ui-sampler-style.md`, and `docs/06-roadmap.md`.

Create the initial repository layout for **Audiocity**:
- `src/plugin/` for `AudioProcessor` + UI
- `src/engine/` for voices/zones/DSP
- `tests/` for offline render harness

Set up a JUCE + CMake project that builds **VST3** and **Standalone** from the same processor.
Set the project/product name to **Audiocity** everywhere (CMake targets, plugin name, window title).
Add a sampler-style UI shell with tabs: Browser, Mapping, Editor, Settings, Diagnostics.
Add buildable stubs for engine + tests.
Do not allocate/lock in the audio callback.
---

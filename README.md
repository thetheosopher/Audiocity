# Audiocity — Hybrid Sampler (Standalone + VST3)

This bundle contains the **specs and prompts** to build **Audiocity**, a hybrid sampler (Standalone + VST3) using JUCE/C++.

## Contents
- `docs/` — authoritative specifications (roadmap, RT rules, SFZ import, browser index, etc.)
- `.github/copilot-instructions.md` — persistent Copilot guidance for this repo
- `prompts/` — ready-to-run Copilot prompts per milestone

## Recommended folder structure

```
Audiocity/
  docs/
  prompts/
  .github/
  src/
  tests/
  third_party/
```

## Quick start
1. Create a new folder `Audiocity`.
2. Copy the contents of this zip to the folder root.
3. Open the folder in VS Code.
4. In Copilot Chat, paste and run `prompts/00-bootstrap.md`.
5. Iterate milestone-by-milestone with prompts.

## Build and test (Windows)
Fast path:
- `./scripts/bootstrap.ps1`

1. Configure:
  - `cmake --preset default`
2. Build plugin targets (Standalone + VST3 shared processor):
  - `cmake --build --preset default --target Audiocity_All`
3. Build offline tests:
  - `cmake --build --preset default --target audiocity_offline_tests`
4. Run tests (Visual Studio generator requires config):
  - `ctest --test-dir build -C Debug --output-on-failure`

### Notes
- VST3 output is generated under `build/Audiocity_artefacts/Debug/VST3/`.
- Standalone output is generated under `build/Audiocity_artefacts/Debug/Standalone/`.
- VS Code CMake Tools test runs are configured to pass `-C Debug` via workspace settings.

## Working agreement
- Specs in `docs/` are the source of truth.
- Every milestone expands tests.
- Keep the audio thread RT-safe.

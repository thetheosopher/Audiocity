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

## Preset management (built-in)
- The Sample tab includes a preset dropdown with `Save`, `Rename`, and `Delete` actions.
- Presets are saved as XML files in the user preset folder:
  - Windows: `%APPDATA%/Audiocity/Presets/`
  - Extension: `.acp`
- Selecting a preset in the dropdown reloads that persisted sample-playback state.

### What preset XML stores
- Current sample playback settings needed to load/play the sample (pitch, playback mode, loop/trim, envelopes, filter, output, pad/CC mappings, and related sample playback parameters).
- If the sample source is generated or captured audio, the sample data is embedded in the preset file.
- If the sample source is a file, the preset stores the sample file path.

### What preset XML does not store
- State associated with non-sample-playback tabs/workflows (for example: library browsing state, generate-tab editor state, capture-tab recording UI state).
- Host preset behavior is unchanged (`getStateInformation`/`setStateInformation` still manages full plugin state for DAW save/load).

### Preset XML example (.acp)
```xml
<AudiocityPatch samplePath="C:/Samples/Kick.wav" rootMidiNote="60" playbackMode="2" ... />
```

Notes:
- The `.acp` file is the serialized sample-playback patch payload.
- For generated/captured sample sources, embedded sample data is stored inside the XML payload.

### Common preset load issues
- `Preset XML payload is invalid.`
  - The file is truncated/corrupted or not valid XML.
- Preset loads but referenced sample is missing.
  - File-based presets store sample paths; move/restore the sample file or re-save preset with an embedded source (generated/captured).

## Windows installer (WiX)
- Installer project: `installer/AudiocityInstaller.wixproj`
- WiX source: `installer/AudiocityInstaller.wxs`
- License shown in installer: `installer/MIT-LICENSE.rtf` (MIT)

The installer exposes optional features so users can install:
- Standalone application (`Audiocity.exe` + `REX Shared Library.dll`)
- VST3 plugin (`Audiocity.vst3` + `REX Shared Library.dll` and bundle metadata)

VST3 installs to the standard system location:
- `C:\Program Files\Common Files\VST3\Audiocity.vst3`

Build MSI directly with MSBuild (WiX Toolset v3 required):
- `msbuild installer/AudiocityInstaller.wixproj /p:Configuration=Debug /p:Platform=x64 /p:ProductVersion=0.1.0`

Installer outputs are written under:
- `output/installer/Debug/`
- `output/installer/Release/`

Optional CMake target:
- Configure with `-DAUDIOCITY_ENABLE_WIX_INSTALLER=ON`
- Build target `audiocity_wix_installer`

Preset-based packaging flow:
- `cmake --preset package-debug`
- `cmake --build --preset package-debug`
- `cmake --preset package-release`
- `cmake --build --preset package-release`

## Working agreement
- Specs in `docs/` are the source of truth.
- Every milestone expands tests.
- Keep the audio thread RT-safe.

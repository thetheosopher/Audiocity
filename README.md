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

Cleanup rebuildable artifacts (keeps installer `.msi`/`.zip` packages):
- `./scripts/cleanup_artifacts.ps1`
- Optional deeper cleanup (also prunes `output/` while keeping `.msi`/`.zip`): `./scripts/cleanup_artifacts.ps1 -IncludeOutput`

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

## Release 0.9.0
- Current project version: `0.9.0`
- Release standalone artifact: `build/Audiocity_artefacts/Release/Standalone/Audiocity.exe`
- Release VST3 artifact: `build/Audiocity_artefacts/Release/VST3/Audiocity.vst3`
- Release installer artifact: `output/installer/Release/AudiocityInstaller.msi`

### Highlights
- Added standalone preset workflow (`Save` / `Rename` / `Delete`) with `.acp` XML payloads.
- Added robust preset load failure handling with delete/keep recovery flow.
- Expanded Generate options (up to 8192 samples), updated defaults, and removed Noise waveform mode.
- Added Sample Information panel improvements and live playback/loop range synchronization.
- Added persistent library peak-preview cache with invalidation on library root change and file-size changes.

Build release + installer in one command:
- `cmake --build build --config Release --target audiocity_wix_installer`

### VC++ runtime on target machines (`MSVCP140.dll`)
- If the target machine is missing Microsoft VC++ runtime, startup can fail with errors like `MSVCP140.dll` not found.
- To produce a self-contained Release build (static MSVC runtime), use:
  - `cmake --preset package-release-selfcontained`
  - `cmake --build --preset package-release-selfcontained`
- This generates binaries that do not require the VC++ redistributable for the MSVC C/C++ runtime.
- For non-self-contained builds, you can ship a bootstrapper EXE that installs VC++ Redistributable automatically.

### Bootstrapper EXE (installs VC++ runtime + MSI)
- Place Microsoft `VC_redist.x64.exe` at `installer/redist/VC_redist.x64.exe`.
- Build with:
  - `cmake --preset package-release-bootstrapper`
  - `cmake --build --preset package-release-bootstrapper`
- Output bundle:
  - `output/installer/Release/AudiocitySetup.exe`
- The bootstrapper chains:
  1. VC++ Redistributable x64 installer (silent)
  2. `AudiocityInstaller.msi`

### Desktop shortcut behavior
- The MSI now creates a per-user Desktop shortcut for the installing user when `Standalone Application` is selected.
- Shortcut target is the installed `Audiocity.exe` in `INSTALLFOLDER`.

### ASIO devices in Standalone
- ASIO is not auto-enabled unless the Steinberg ASIO SDK headers are available at build time.
- Enable with CMake options:
  - `-DAUDIOCITY_ENABLE_ASIO=ON`
  - `-DAUDIOCITY_ASIO_SDK_DIR=<path-to-asiosdk>` (must contain `common/iasiodrv.h`)
- If `AUDIOCITY_ENABLE_ASIO=ON` and headers are missing, CMake configure fails explicitly.
- Helper integration script:
  - `./scripts/integrate_asio_sdk.ps1 -SourceDir <path-to-extracted-asio-sdk> -CleanDestination`
- Default integrated location is `third_party/asiosdk`.
- Convenience preset:
  - `cmake --preset package-release-bootstrapper-asio`
  - `cmake --build --preset package-release-bootstrapper-asio`

### Optional code signing (MSI + bootstrapper)
- Enable signing in CMake with:
  - `-DAUDIOCITY_ENABLE_CODESIGN=ON`
  - `-DAUDIOCITY_CODESIGN_CERT_SHA1=<thumbprint>`
- Optional overrides:
  - `-DAUDIOCITY_SIGNTOOL_EXECUTABLE="C:/Program Files (x86)/Windows Kits/10/bin/<ver>/x64/signtool.exe"`
  - `-DAUDIOCITY_CODESIGN_TIMESTAMP_URL="http://timestamp.digicert.com"`
- Signed preset workflow:
  - `cmake --preset package-release-bootstrapper-signed`
  - replace `<SET_CERT_THUMBPRINT>` in `CMakePresets.json`
  - `cmake --build --preset package-release-bootstrapper-signed`
- When enabled and configured, signing is applied automatically to:
  - `output/installer/<Config>/AudiocityInstaller.msi`
  - `output/installer/<Config>/AudiocitySetup.exe`

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

The MSI now supports both install scopes from the UI:
- Per-machine (all users, admin rights required)
- Per-user (current user only, no elevation)

VST3 install location depends on selected scope:
- Per-machine: `C:\Program Files\Common Files\VST3\Audiocity.vst3`
- Per-user: `%LOCALAPPDATA%\Programs\Common\VST3\Audiocity.vst3`

Build MSI directly with MSBuild (WiX Toolset v3 required):
- `msbuild installer/AudiocityInstaller.wixproj /p:Configuration=Debug /p:Platform=x64 /p:ProductVersion=0.9.0`

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

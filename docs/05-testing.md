# Testing Strategy

## Offline render harness

- Runs engine without an audio device.
- Feeds timestamped MIDI into blocks.
- Writes output WAV for comparisons.
- The single executable exposes four disjoint suites: `engine`, `import`, `state`, and `preset`.
- List or select cases without rebuilding:

```powershell
./build/ci-windows/tests/Debug/audiocity_offline_tests.exe --list
./build/ci-windows/tests/Debug/audiocity_offline_tests.exe --suite import
./build/ci-windows/tests/Debug/audiocity_offline_tests.exe --filter AssetResolver
```

Each offline case belongs to exactly one suite. CTest registers the suites separately as `audiocity_offline_engine`, `audiocity_offline_import`, `audiocity_offline_state`, and `audiocity_offline_preset`.

The MP-3 acceptance benchmark is intentionally a Release-only performance gate. It creates
50,000 real one-byte files before starting the timers, then measures production incremental
scan delivery and index search separately. Run it with:

```powershell
cmake --build build/ci-windows --config Release --target audiocity_offline_tests --parallel 2
./build/ci-windows/tests/Release/audiocity_offline_tests.exe --filter LibraryFileIndex50kScanSearchIntegration
```

The reported fixture-setup and full-reconciliation durations are diagnostic; the enforced
budgets are under 500 ms to the first bounded batch and under 50 ms for search. Debug runs use
a smaller real tree to retain traversal, batching, search, and cancellation coverage without
applying machine-sensitive timing limits.

## Local matrix

Configure and build the complete Debug test surface, then run all nine unique CTest entries:

```powershell
cmake --preset ci-windows
cmake --build --preset ci-windows --target audiocity_offline_tests audiocity_ui_snapshot_harness audiocity_preset_runtime_smoke audiocity_ui_snapshot_header_smoke audiocity_ui_snapshot_core_smoke
ctest --preset ci-windows
```

Use CTest labels to narrow the matrix without bypassing its registration:

```powershell
ctest --preset ci-windows --label-regex '^import$'
ctest --preset ci-windows --tests-regex 'packaging|preset_runtime'
```

## Test-build consolidation benchmark

The MP-7 build-time comparison uses isolated source and build directories for the pre-change
`HEAD` and the working tree, the same Visual Studio 2022 toolchain/JUCE checkout, Debug, and
`--parallel 2`. It builds the two targets that previously recompiled the shared production and
JUCE sources independently:

```powershell
cmake --build <build-dir> --config Debug --target audiocity_offline_tests audiocity_engine_profile --parallel 2
cmake -E touch <source-dir>/src/engine/EngineCore.cpp
cmake --build <build-dir> --config Debug --target audiocity_offline_tests audiocity_engine_profile --parallel 2
```

The 2026-09-06 local comparison measured 127.12 s before versus 98.95 s after for a clean build
(22.2% lower), and 9.50 s before versus 7.54 s after for the one-file incremental rebuild
(20.6% lower). The consolidated support library now owns both production and JUCE module
translation units; the executables and all importer fuzz targets compile only their harness
source and link that support library. These are reproducible same-host engineering measurements,
not a guarantee about absolute GitHub-hosted runner duration.

## Release product and host gates

The shipped wrappers are a separate Release build; a Debug test-only build is not sufficient evidence that the products package:

```powershell
cmake --build --preset ci-windows-release
./scripts/verify_release_artifacts.ps1 -BuildDir build/ci-windows -Configuration Release
cmake --build build/ci-windows --config Release --target audiocity_engine_profile --parallel 2
./scripts/check_render_deadline.ps1 -BuildDir build/ci-windows -Configuration Release
./scripts/validate_vst3.ps1 -BuildDir build/ci-windows -Configuration Release -StrictnessLevel 5
```

`verify_release_artifacts.ps1` requires the Standalone executable, complete VST3 bundle/binary, and every static installer input. It recursively compares the exact relative `.acp` paths and SHA-256 content of both copied factory-preset banks with the authoritative source bank. CI passes `-CompileInstaller`, using the Inno Setup compiler on `windows-2022` to compile the real installer definition against the freshly built wrappers and preserve the validation installer under `artifacts/installer-validation/`. This validates installer syntax and all referenced inputs, but does not exercise `build_release.ps1`'s release-staging copies or portable ZIP staging. The deadline script writes a per-run CSV measurement under `artifacts/performance/` and fails any CPU, Fidelity, or Ultra run below real time. The plugin validator downloads the pinned pluginval v1.0.4 archive when no executable is supplied, verifies its SHA-256, and preserves logs under `artifacts/pluginval/logs/`.

## Importer sanitizer and fuzz corpus

The standalone Clang build under `tests/fuzz` produces one bounded libFuzzer target for each supported importer grammar exercised by the harness. On Ubuntu with Clang and Ninja:

```bash
cmake -S tests/fuzz -B build/importer-fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build/importer-fuzz --parallel 2
ctest --test-dir build/importer-fuzz --output-on-failure --timeout 60
```

The 15 non-REX targets use ASan, UBSan, and libFuzzer and enforce a 4 MiB input limit. REX is excluded because its runtime is proprietary and Windows-only. CI copies each committed deterministic seed corpus into the build tree, runs 1,000 mutations with a fixed seed, and leaves the source corpus untouched; longer fuzz campaigns can invoke an individual executable with normal libFuzzer flags.

## Native coverage baseline

After building `audiocity_offline_tests`, install OpenCppCoverage or pass its executable explicitly:

```powershell
./scripts/report_test_coverage.ps1 -BuildDir build/ci-windows -Configuration Debug
./scripts/report_test_coverage.ps1 -OpenCppCoveragePath 'C:/Tools/OpenCppCoverage/OpenCppCoverage.exe'
```

The script scopes collection to `src/**`, rejects empty/malformed reports or reports without production classes, emits Cobertura XML plus a Markdown summary under `artifacts/coverage/`, and reports a baseline rather than enforcing a percentage threshold. CI installs the explicitly pinned OpenCppCoverage 0.9.9.0 package.

## UI snapshot automation

- The UI snapshot harness lives in `tests/UiScreenshotHarness.cpp` and renders deterministic offscreen PNG captures for the main editor tabs.
- The harness now also captures representative stateful coverage for recent UI work: `sample.png` renders a slice-loaded Sample view in the responsive default-width workspace+inspector mode, with the browser rail collapsed and `Output` promoted into the inspector while `Program Map` remains inline; `sample_browser_only.png` captures the default-width browser-on/inspector-off state that the Sample `Browse` toggle now swaps into at medium widths; `sample_preset_search.png` captures the searchable preset strip with deterministic mock preset names and a fixed `bass` query; `sample_wide.png` renders the default wide browser/workspace/inspector layout, which now prioritizes `Filter Envelope + Mod` over `Effects` when only one advanced right-rail card fits; `sample_wide_medium.png` locks the shorter-wide breakpoint where `Effects` is the only advanced inspector card that still fits at full height; `sample_wide_browser_only.png` captures the wide browser-on/inspector-off state; `sample_wide_inspector_only.png` captures the wide browser-off/inspector-on state; `sample_wide_cards_collapsed.png` captures the wide right rail with both advanced inspector cards collapsed down to their compact headers; `sample_wide_tall.png` renders the taller wide Sample mode that shows both `Effects` and `Filter Envelope + Mod` in the inspector; `sample_wide_focus.png` captures the wide rails-off workspace mode with both `Browse` and `Inspect` disabled; `mapping.png` renders the corresponding slice-program mapping state; and `sample_modulation.png` scrolls the Sample page to the modulation section with non-zero routing so the per-source chips, dominant-source destination summary, and the right-side inspector behavior are snapshot-tested together.
- Local export path: run `pwsh -File scripts/export_ui_snapshots.ps1` from the repo root.
- The script probes the harness with `--smoke-exit`, rebuilds the `audiocity_ui_snapshot_harness` target in the default Debug tree, exports PNGs to `build/ui-snapshots` by default, and writes `index.html`, `snapshot-summary.md`, and `snapshot-manifest.json` alongside the images.
- Committed baselines live under `tests/ui-snapshot-baselines/current`. By default the exporter compares the fresh PNGs against that directory and emits `snapshot-diff-summary.md`, `snapshot-diff-report.json`, and per-image overlay PNGs under `diffs/` when the baseline does not match.
- Baseline refresh path: run `pwsh -File scripts/export_ui_snapshots.ps1 -UpdateBaseline -BaselineDir tests/ui-snapshot-baselines/current` after intentionally accepting a UI change.
- CI path: `.github/workflows/ui-snapshots.yml` runs the same script on Windows, compares against the committed baseline set, and uploads the generated review bundle as an artifact.

## Golden tests

- Compare hashes or error thresholds.
- Fixtures cover:
  - envelopes
  - voice stealing
  - looping
  - round robin determinism
  - quality-tier resampler differences, determinism, runtime switching across CPU/Fidelity/Ultra playback modes, and an objective high-frequency spectral preservation check that requires Ultra to retain more main-tone energy with lower side-energy than Fidelity at a non-integer pitch ratio
  - preload segmentation and runtime preload changes for single-sample and imported-program playback
  - single-sample file-backed disk streaming across preload rebuilds
  - imported-program bounded disk-stream cache, processor-worker prime servicing, and note-on/lookahead hit-miss telemetry
  - SFZ import (#include/default_path/seq_length/release-trigger/seq_mode=random/velocity-crossfade playback/loop_continuous playback/one-shot playback/gain-pan-tuning playback/choke-group playback)
  - Mapping state structural round-trip, imported-program state subtree round-trip, legacy replay fallback, and imported-program derived-state summaries
  - Mapping zone create, duplicate, split, delete, chromatic remap, key-range spread, root-note derivation from key centers, and velocity fade edits, including explicit sample-asset selection for new zones
  - atomic imported-program batch mapping apply/delete rollback semantics
  - chronological editor undo-history behavior across imported-program mapping snapshots and sample/settings edits, including coalescing, undo labels, and create/duplicate/split structural zone operations
  - real-time modulation routing for mod wheel, aftertouch, velocity, and two macro controls across pitch, filter, and amp destinations, including processor parameter/state plumbing and the first in-plugin modulation surface
  - REX slice decoding, transient slice-program construction for regular samples, manual sample-slice splitting/merging, chromatic slice-program import from `.rex/.rx2`, map-to-root-note snapping, and imported-program path compatibility across generic, explicit sample-slice, and legacy state properties
  - SFZ diagnostics for unsupported opcode values
  - legacy NKI probe classification plus nearby-sample resolution, group/zone metadata enumeration, simple playable translation including sample-window, loop, gain, pan, tuning, and trigger metadata, and explicit `.nki` imported-program state tagging

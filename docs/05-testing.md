# Testing Strategy

## Offline render harness
- Runs engine without an audio device.
- Feeds timestamped MIDI into blocks.
- Writes output WAV for comparisons.

## UI snapshot automation
- The UI snapshot harness lives in `tests/UiScreenshotHarness.cpp` and renders deterministic offscreen PNG captures for the main editor tabs.
- The harness now also captures representative stateful coverage for recent UI work: `sample.png` renders a slice-loaded Sample view in the responsive default-width workspace+inspector mode, with the browser rail collapsed and `Output` promoted into the inspector while `Program Map` remains inline; `sample_preset_search.png` captures the searchable preset strip with deterministic mock preset names and a fixed `bass` query; `sample_wide.png` renders the default wide browser/workspace/inspector layout, which now prioritizes `Filter Envelope + Mod` over `Effects` when only one advanced right-rail card fits; `sample_wide_medium.png` locks the shorter-wide breakpoint where `Effects` is the only advanced inspector card that still fits at full height; `sample_wide_browser_only.png` captures the wide browser-on/inspector-off state; `sample_wide_inspector_only.png` captures the wide browser-off/inspector-on state; `sample_wide_cards_collapsed.png` captures the wide right rail with both advanced inspector cards collapsed down to their compact headers; `sample_wide_tall.png` renders the taller wide Sample mode that shows both `Effects` and `Filter Envelope + Mod` in the inspector; `sample_wide_focus.png` captures the wide rails-off workspace mode with both `Browse` and `Inspect` disabled; `mapping.png` renders the corresponding slice-program mapping state; and `sample_modulation.png` scrolls the Sample page to the modulation section with non-zero routing so the per-source chips, dominant-source destination summary, and the right-side inspector behavior are snapshot-tested together.
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

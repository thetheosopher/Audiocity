# Sampler simplification implementation — 2026-09-07

The [UX review](UX_DESIGN_REVIEW_2026-09-07.md) is implemented as a focused instrument shell. The product promise is **Turn any sound into an instrument**. No new synthesis engine, import format, performance mode or arbitrary modulation system is introduced.

## Implemented experience

| Area | Result |
| --- | --- |
| Navigation | Default Sound and on-demand Modulation; source preparation, Details, mapping and About are secondary surfaces |
| Sound | Waveform, playback/tuning, amp envelope, filter and essential effects fit at 980 × 860 and 980 × 720 logical pixels |
| Browser | Presets/Samples/Instruments scopes, search, favorites, recent items, explicit load actions, raw-sample auto/manual preview and preview level |
| Resize | Overlay browser at constrained widths; optional docking from 1400 pixels; fixed section order with no automatic inspector promotion |
| Identity/output | Current name, edited indication, Save, master volume and stereo output remain accessible; name/save destination survive editor/project restore |
| Audition | Auto/Keyboard/Pads/Hidden, MIDI/voice status and All Notes Off; pad assignment via right-click |
| Expression | Existing two macro values remain playable; routing is five sources × three destinations with explicit signed units |
| Sources | Generate/Record have preview, Use sample, cancel/return; technical format settings are disclosed separately |
| Advanced | Exact regions, quality/preload, detailed effects, diagnostics and metadata in Details; single-zone repair and SFZ/DecentSampler export in advanced mapping |
| Retired surfaces | Seven peer tabs, permanent dual audition footer, main preset administration, library creation, batch authoring and bulk metadata UI |

`PluginInstrumentWorkspace.cpp` owns the new shell layout and replacement/save interactions. Existing controls and processor paths are reused. The former layout helpers and authoring implementation remain where they support compatibility or existing tests; their retired entry points are hidden/removed from normal navigation.

## Interaction contracts

**Scope.** Single-sample Sound edits its root, region and playback mode. Imported instruments retain each zone's own settings. Sound displays an explanation and Edit zones instead of ineffective global root/mode controls. General tuning, amp, filter and effects stay at instrument scope. Non-slice multisample waveforms are reference displays; slice split/merge gestures remain available.

**Discovery.** Selecting a preset never loads it. Selecting a raw sample can preview; preview has its own level/stop control and ends when the browser closes. Loading is an explicit action. Presets and instruments have no processed preview engine. Save As destinations outside the user bank remain discoverable through recent/favorite file paths.

**Saving.** The chooser's complete path is passed to the preset writer. Save reuses that destination; factory presets use Save As. Parameter edits, zone edits, pad changes and CC assignment changes mark the sound edited. Current identity and a saved parameter baseline are UI state, excluded from playback presets. The sound's audio and mappings still use the existing serialization formats.

**Recovery.** One replacement snapshot is kept while the editor is open. It covers presets, source import, Generate/Record commits and initial slice conversion, and swaps with the current sound when restored. A cancelled/failed import does not replace that history with an attempted load. Single-sample recovery uses existing embedding, within its existing size/codec limits, including when its original file has moved. Imported recovery retains external-asset requirements. Malformed presets do not alter the current processor; missing-source restores roll back. Successful replacement may end held notes.

**Undo.** The existing mixed settings/mapping history now includes all normalized host parameter values, so amp, filter, effects and macro edits participate. Consecutive changes to the same parameter coalesce with an idle boundary. Undo/Redo issues the same host parameter notifications; IDs and ranges are unchanged. Replacement recovery is a separate command.

**Compatibility and thread ownership.** Existing importer readers, mapping models, pad data, CC mappings, presets and project assets are retained. The editor always reopens in Sound even if a legacy project stored a utility-tab index. Audition preference is stored in project/UI state and excluded from playback presets. Added sound-identity locking and undo allocations occur on editor/state paths; no lock or file I/O is added to rendering. Preview level uses a relaxed atomic read once per audio block.

## Validation

Validated locally on Windows on 2026-09-07:

| Check | Evidence |
| --- | --- |
| Debug build | `cmake --build build/ci-windows --config Debug --parallel 6` succeeded; the screenshot target was rebuilt after the final harness-only fix |
| Release products | `cmake --build build/ci-windows --config Release --target Audiocity_All --parallel 6` succeeded for Standalone and VST3 |
| Complete regression suite | `ctest --preset ci-windows`: **9/9 passed**, 36.56 seconds; engine, imports, state, presets, packaging configuration, editor interaction/screenshots, runtime and compatibility smoke checks |
| Visual comparison | **23/23 exact image matches**, with zero differing pixels against the reviewed baselines |
| Visual inspection | All 23 images inspected, including 980 × 860 default, 980 × 720 constrained and 1500 × 900 wide layouts; essential Sound controls fit without scrolling, and secondary controls remain reachable through their viewports |
| Documentation | README screenshots and user guide updated for the shipped navigation, scope, source, saving and recovery behavior |

Reviewed images are checked in under [`tests/ui-snapshot-baselines/current`](../tests/ui-snapshot-baselines/current). The test run writes fresh images and `snapshot-diff-summary.md` under `build/ci-windows/ui-snapshots`; CTest evidence is in `build/ci-windows/Testing/Temporary/LastTest.log`. The snapshot writer now truncates existing PNG files before writing, so repeat runs compare fresh renders instead of images retained at the beginning of an appended file.

The local Release products are `build/ci-windows/Audiocity_artefacts/Release/Standalone/Audiocity.exe` and `build/ci-windows/Audiocity_artefacts/Release/VST3/Audiocity.vst3`.

The screenshot harness now covers 23 scenarios: default/constrained/wide Sound; slices; routing and lower modulation controls; Details at two scroll positions; preset search, results and empty state; sample/instrument browser scopes; overlay/docking; mapping at default and constrained sizes; Generate/Record at default and constrained sizes; About; and hidden audition. Image fixtures are deterministic examples, not musician usability measurements.

`WorkspaceInteractionChecks.h` exercises the real editor and processor before screenshot export: essential-control reachability, source view stability, browser disclosure, chosen save destination and selection actions, edited state, amp Undo/Redo, preset recovery, invalid/missing presets, Generate/Record cancel/commit/recovery, slice reversal, recovery with a removed original sample file, preview-versus-load, docking, hidden audition, preference restore and editor reopening.

## Concrete limits

- Replacement history is one step and lasts for the current editor lifetime. Host projects and saved presets remain the durable recovery mechanism.
- Slice conversion accepts file-backed single samples. It does not implicitly convert generated/recorded sources or discard imported multisample mappings.
- Eight existing pad assignments remain; use MIDI/keyboard for additional slices. No new performance bank mode is added.
- Imported instruments still depend on their sample assets. Export/copy-samples or the existing relink workflow is needed when moving them.
- Preview level and browser docking are session preferences. The audition surface and sound identity persist with project state.
- Native DAW interaction, hardware MIDI/audio, screen-reader operation, Windows display scaling and trials with musicians require interactive follow-up. Offscreen renders verify logical layout and reachability, not physical-device or human task-completion performance.
- This work builds local standalone/VST3 products. Hosted CI, installer publication and any previously outstanding extended audio soak remain separate release gates.

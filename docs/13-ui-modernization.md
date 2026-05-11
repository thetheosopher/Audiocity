# UI Modernization Spec

## Status
- Owner: GitHub Copilot + repo maintainers
- Started: 2026-05-03
- Current phase: Phase 5 structural layout redesign in progress

## Goal
Modernize Audiocity into a premium sampler interface that feels fast, musical, tactile, and trustworthy for producers, sound designers, and live performers.

## Product Experience Targets
- Make the sampler feel like one instrument, not a collection of unrelated tabs.
- Keep the waveform editor visually central and increasingly direct-manipulation driven.
- Reduce visual clutter so the primary actions are obvious within one screen.
- Preserve expert depth while making common tasks fast for first-time users.
- Stay safe for long sessions in dark studios: high contrast, restrained accent use, low visual fatigue.

## Users
- Producers who want fast load-edit-play workflows.
- Beat-makers chopping and assigning slices to pads.
- Sound designers shaping envelopes, loops, and modulation.
- Live performers who need confidence, readability, and immediate feedback.

## Design Principles
1. Direct manipulation first: waveform interactions should beat numeric editing whenever possible.
2. Progressive disclosure: primary controls stay visible; advanced controls move into secondary surfaces.
3. One instrument workflow: browser, editor, shaping, and performance should feel connected.
4. Studio-safe visual language: dark neutral base, strong typography, sparse accent color.
5. Performance-aware implementation: UI changes must not compromise audio-thread safety.

## Target Information Architecture
- Header: preset navigation, macro entry points, utility actions.
- Browser sidebar: sample discovery, tags, preview, capture/generate entry points.
- Main workspace: waveform editor plus sample or mapping context.
- Shaping rail: amp, filter, modulation, FX, output.
- Performance strip: pads, keyboard, meters, transport-adjacent preview controls.
- Inspector: contextual advanced editing.

## Phase Plan

### Phase 1 - Visual Foundation
- Refresh the base palette, contrast, and panel hierarchy.
- Flatten the top-level tab chrome.
- Make waveform, envelope, filter, and meter surfaces feel more premium and readable.
- Reduce always-on clutter by hiding diagnostics behind an explicit reveal.

### Phase 2 - Sample Editor Directness
- Move trim and loop editing closer to direct waveform interaction.
- Make envelope and filter graphs progressively more interactive.
- Improve visual feedback for loop, slice, and play ranges.

### Phase 3 - Workflow Unification
- Reframe Library, Generate, Capture, and Player as connected parts of one instrument workflow.
- Introduce a persistent performance strip.
- Start collapsing long vertical control stacks into clearer grouped modules.

### Phase 4 - Advanced Interaction
- Add visual modulation routing.
- Improve slice editing, snapping, and context menus.
- Introduce a right-side inspector for advanced controls.

### Phase 5 - Structural Layout Redesign
- Move toward browser + workspace + inspector architecture.
- Add collapsible advanced controls and better resizing behavior.
- Rework preset browsing into a richer searchable surface.

## Implementation Tracker
- [x] Create modernization spec and progress log.
- [x] Phase 1.1: Refresh tab chrome and global editor palette.
- [x] Phase 1.2: Refresh waveform, graph, and metering visuals.
- [x] Phase 1.3: Hide diagnostics behind an explicit reveal instead of always-on panel space.
- [x] Phase 2.1: Reduce trim/loop dependence on knob-only editing.
- [x] Phase 2.2: Improve slice visibility and interactions.
- [x] Phase 2.3: Make amp and filter envelope graphs directly editable.
- [x] Phase 3.0: Collapse secondary Sample-page sections behind progressive disclosure.
- [x] Phase 3.1: Rework Player into a persistent performance surface.
- [x] Phase 3.2: Fold Library into a persistent or quickly accessible browser rail.
- [x] Phase 4.1: Add visual modulation routing and destination feedback.
- [ ] Phase 5.1: Restructure the full sampler layout around browser/workspace/inspector.
- [x] Validation 1.0: Land deterministic UI snapshot capture and artifact export for automated feedback.
- [x] Validation 2.0: Add committed UI snapshot baselines plus automated diff reporting in local Debug validation and CI.

## Risks And Guardrails
- Do not add audio-thread allocations, locks, file I/O, or logging.
- Keep the project compileable after each milestone.
- Favor incremental UI modernization over one-shot rewrites.
- Prefer reusable styling seams over one-off hardcoded paint changes.
- Document unfinished redesign work here as milestones rather than burying intent in code.

## Progress Log
- 2026-05-03: Spec created. Phase 1 visual foundation started.
- 2026-05-03: Phase 1.1 landed in `PluginEditor` and `DialLookAndFeel`: flatter tabs, darker neutral chrome, stronger card hierarchy, and refreshed button/knob palette.
- 2026-05-03: Phase 1.2 landed in the Sample page: waveform, envelope, filter, and output meter visuals now use the new palette and clearer overlay contrast.
- 2026-05-03: Phase 1.3 landed: diagnostics moved behind a `Tech` toggle in the Sample top bar, with `Ctrl+Alt+D` as a keyboard reveal.
- 2026-05-03: Validation passed with a CMake build after the UI slice landed.
- 2026-05-03: Phase 2.1 landed in the Sample page: waveform trim and loop editing now has an explicit summary row, direct-manipulation hinting, and a quick reset action directly under the waveform.
- 2026-05-03: Phase 3.0 landed in the Sample page: `Program Map`, `Filter Envelope + Mod`, and `Effects` now collapse behind section headers so the primary path stays visible.
- 2026-05-03: Phase 2.2 advanced: slice markers now render with stronger caps and small numeric badges for compact slice sets, improving visual scanability without changing engine behavior.
- 2026-05-03: Validation passed again after the waveform-summary, collapsible-section, and slice-visibility changes.
- 2026-05-04: Phase 2.3 landed: the amp and filter ADSR graphs now expose draggable nodes so envelope shaping can happen directly in the graph while reusing the existing dial and processor update path.
- 2026-05-04: Editor tab persistence seam corrected: the editor now honors the stored tab index on construction, and the processor tab range/capture-tab monitoring now cover all current tabs instead of the older `0..4` range.
- 2026-05-04: Screenshot automation scaffolding started in `tests/UiScreenshotHarness.cpp` and `tests/CMakeLists.txt`: the harness is designed to render deterministic offscreen PNG captures across editor tabs for UI review.
- 2026-05-04: Snapshot harness startup crash resolved: `UiScreenshotHarness.cpp` was overflowing the stack before `main` could honor `--smoke-exit` because `AudiocityAudioProcessor` lived in the `main` stack frame. The harness now keeps the processor on the heap and retains `--smoke-exit` as a cheap startup probe.
- 2026-05-04: Manual end-to-end snapshot export now passes in the Debug harness path, writing `sample`, `library`, `mapping`, `player`, `generate`, `capture`, and `about` PNGs to `build/tests/ui-snapshots` for UI review.
- 2026-05-04: Workspace VS Code tasks now use the known-good Visual Studio `cmake.exe` and `ctest.exe` paths, retry by clearing only stale default-config state instead of deleting the whole `build/` tree, and build the UI snapshot harness alongside offline tests in the Debug test path.
- 2026-05-04: Snapshot review automation now has a reusable export script plus a Windows GitHub Actions workflow that publishes the PNG set, HTML gallery, and manifest as an artifact for pull-request and manual review runs.
- 2026-05-04: Validation 2.0 landed: committed baselines now live under `tests/ui-snapshot-baselines/current`, the Debug snapshot test compares fresh renders against that baseline set, and the export workflow emits diff summaries plus overlay images when the UI changes.
- 2026-05-04: Phase 2.2 landed: slice programs now expose clearer region feedback in the waveform, hovered slice boundaries/regions render with stronger emphasis, the compact slice labels align to slice regions, and double-click now splits directly at the cursor while the summary text advertises merge/re-slice gestures.
- 2026-05-04: Phase 3.1 landed: Sample, Library, Mapping, Generate, and Capture now keep a compact performance strip with the keyboard, quick pads, and an `Open Player` affordance, while the dedicated Player tab remains the full editing surface.
- 2026-05-04: Phase 3.1 follow-up landed: the compact performance strip now scales down more gracefully at shorter editor heights and exposes a live status display with stereo output bars plus a two-digit LED-style voice counter.
- 2026-05-04: Phase 3.2 landed: Sample, Mapping, Generate, and Capture now keep a compact browser rail built from the existing Library controls, while the full Library tab retains bookmarks and tag-editing depth.
- 2026-05-04: Phase 3.2 follow-up landed: the compact browser rail now uses denser rows, keeps selection-preview behavior active across rail tabs, and surfaces preview/load affordances directly in the rail status and row text.
- 2026-05-04: Phase 4.1 started: the modulation panel now paints per-source destination feedback strips for pitch, filter, and amp routing, including live macro-value context for Macro 1 and Macro 2.
- 2026-05-05: Validation coverage follow-up landed: the UI snapshot harness now captures a slice-loaded Sample state, the corresponding Mapping state, and a scrolled `sample_modulation` view that keeps the new modulation feedback chips under deterministic screenshot coverage.
- 2026-05-05: Phase 4.1 completed: the Sample-page modulation surface now compresses route dials enough to reveal a destination-first summary card for Macro 1 and Macro 2, while the existing per-source strips continue to show pitch, filter, and amp routing directly under each source cluster.
- 2026-05-05: Phase 4.1 follow-up landed: the destination summary cards now surface the strongest active source per destination so dense modulation setups read back faster without expanding the underlying control surface.
- 2026-05-05: Phase 5.1 started: Sample layouts now use explicit responsive modes. Wider widths keep the browser, workspace, and inspector columns with `Program Map` in the right rail, while the default editor width collapses the browser rail, keeps the inspector active, and promotes `Output` controls into that rail instead of leaving every advanced section in the scroll stack.
- 2026-05-05: Phase 5.1 continued: the Sample top bar now exposes `Browse` and `Inspect` rail toggles, the wide layout keeps the compact browser rail visible alongside the workspace and inspector, and taller wide layouts promote `Effects` into the inspector next to `Sample Information` and `Program Map`.
- 2026-05-05: Snapshot coverage expanded with `sample_wide.png`, which captures the full tri-column Sample layout so browser/workspace/inspector regressions stay under deterministic review.
- 2026-05-05: Phase 5.1 continued again: taller wide Sample layouts now also promote `Filter Envelope + Mod` into the inspector below the existing right-rail cards, so the wide workspace can keep another tall advanced section out of the main scroll stack.
- 2026-05-05: Snapshot coverage expanded again with `sample_wide_tall.png` for the taller full-inspector stack and `sample_wide_focus.png` for the wide rails-off workspace mode, keeping the new rail toggles and advanced inspector promotion under deterministic review.
- 2026-05-05: Phase 5.1 continued again: shorter wide Sample layouts now explicitly prioritize `Filter Envelope + Mod` over `Effects` when there is room for only one advanced right-rail card, keeping shaping controls closer to the waveform in the default wide workspace.
- 2026-05-05: Snapshot coverage expanded again with `sample_wide_browser_only.png` and `sample_wide_inspector_only.png`, so each one-rail-off wide Sample state is now covered independently instead of only through the fully collapsed wide workspace mode.
- 2026-05-05: Phase 5.1 continued again: the advanced Sample inspector cards in the wide right rail are now individually collapsible from their own headers, so `Filter Envelope + Mod` and `Effects` can stay present as compact affordances without forcing their full control stacks open.
- 2026-05-05: Snapshot coverage expanded again with `sample_wide_medium.png`, which locks the shorter-wide breakpoint where `Effects` is still the only advanced inspector card that fits, and `sample_wide_cards_collapsed.png`, which keeps the new collapsible inspector-card state under deterministic review.
- 2026-05-05: Phase 5.1 continued again: the wide Sample inspector card states now persist through editor recreation and state restore, and the Sample top bar now turns preset management into a searchable surface with a live match summary instead of only an unfiltered combo.
- 2026-05-05: Phase 5.1 continued again: whenever the Sample inspector rail is active, `Output` now lives in that rail as a compact card instead of returning to the main scroll stack. Wider imported-program layouts keep `Program Map` and `Output` anchored in the inspector, while expanded advanced cards fall back inline unless there is enough vertical room or they are collapsed into compact rail affordances.
- 2026-05-05: Snapshot coverage expanded again with `sample_preset_search.png`, which captures the searchable preset strip with deterministic mock preset names and a fixed filter query.

## Validation Notes
- Current repo validation is strong for engine behavior, but UI visual changes do not yet have a dedicated screenshot or golden-image harness.
- Until a UI snapshot harness exists, each UI milestone must at minimum compile cleanly and be documented here.
- 2026-05-03: `Build_CMakeTools` completed successfully after the Phase 1 UI changes.
- 2026-05-03: `Build_CMakeTools` completed successfully after the Phase 2.1, Phase 3.0, and slice-visibility follow-up changes.
- 2026-05-04: `Build_CMakeTools` completed successfully after the editable-envelope and tab-state fixes.
- 2026-05-04: UI snapshot harness scaffolding now exists, with a staged rollout plan:
	- Stage 1: deterministic offscreen PNG capture for representative editor tabs and states.
	- Stage 2: publish screenshot artifacts from CI for human feedback on each UI iteration.
	- Stage 3: committed baseline images and exact-match diff reporting are now active; thresholds remain configurable in the comparison script if tiny rendering drift ever becomes acceptable.
- 2026-05-04: Direct Debug validation passed for both `--smoke-exit` and full snapshot export. `build/tests/Debug/audiocity_ui_snapshot_harness.exe --smoke-exit` now exits `0`, and `--output-dir build/tests/ui-snapshots` emits the expected PNG set.
- 2026-05-04: In this environment, CMake Tools can still be attached to `build/release-selfcontained` while the validated harness flow lives under the default Debug tree. Prefer the repo tasks or explicit Visual Studio CMake binaries when validating the snapshot harness.
- 2026-05-04: Phase 3.1 validation passed with the Debug test-target build plus a fresh snapshot export review from `scripts/export_ui_snapshots.ps1`, confirming the compact strip renders across the workflow tabs while the About page stays focused on product information.
- 2026-05-04: Stage 2 artifact publishing is now wired for CI via `.github/workflows/ui-snapshots.yml`, with `scripts/export_ui_snapshots.ps1` producing a reviewable artifact bundle (`.png`, `index.html`, `snapshot-summary.md`, `snapshot-manifest.json`).
- 2026-05-04: The local/CI snapshot flow now also produces `snapshot-diff-summary.md` and `snapshot-diff-report.json`, and fails validation when fresh snapshots diverge from `tests/ui-snapshot-baselines/current` beyond the configured thresholds.
- 2026-05-04: Phase 3.1 follow-up and Phase 3.2 both compiled cleanly via `CMake: Build Tests (Debug)`.
- 2026-05-04: Snapshot validation for the strip refinement and browser rail initially failed against the older baselines on `sample`, `library`, `mapping`, `generate`, and `capture` as expected; after visual review, `scripts/export_ui_snapshots.ps1 -UpdateBaseline` refreshed `tests/ui-snapshot-baselines/current`, and a rerun of `scripts/compare_ui_snapshots.ps1` returned `PASS` for all seven tabs.
- 2026-05-04: Phase 2.2 completion, browser-rail polish, and the first Phase 4.1 modulation-feedback slice all compiled cleanly via `CMake: Build Tests (Debug)`.
- 2026-05-04: A fresh `scripts/export_ui_snapshots.ps1 -OutputDir build/ui-snapshots-validate` run still compared `PASS` against `tests/ui-snapshot-baselines/current`, so these edits did not require a baseline refresh.
- 2026-05-05: The snapshot harness coverage was expanded with deterministic slice-program and modulation states, the Sample source label now uses the imported program name instead of a machine-specific temp path, and `scripts/compare_ui_snapshots.ps1 -ActualDir build/ui-snapshots-phase41-coverage -BaselineDir tests/ui-snapshot-baselines/current` returned `PASS` for `about`, `capture`, `generate`, `library`, `mapping`, `player`, `sample`, and `sample_modulation`.
- 2026-05-05: The Phase 4.1 completion slice compiled cleanly via `CMake: Build Tests (Debug)`, `sample_modulation` was visually reviewed after adding the macro destination summary, `scripts/export_ui_snapshots.ps1 -UpdateBaseline` refreshed only the accepted modulation snapshot baseline, and `scripts/compare_ui_snapshots.ps1 -ActualDir build/ui-snapshots-phase41-progress -BaselineDir tests/ui-snapshot-baselines/current` returned `PASS` for all eight snapshots.
- 2026-05-05: The Phase 4.1 follow-up and first Phase 5.1 Sample inspector seam both compiled cleanly via `CMake: Build Tests (Debug)`. `scripts/export_ui_snapshots.ps1 -OutputDir build/ui-snapshots-phase51-seam` produced the expected wide-layout Sample changes for visual review, and the accepted state was then refreshed into `tests/ui-snapshot-baselines/current` for ongoing exact-match snapshot validation.
- 2026-05-05: Phase 5.1 responsive follow-up compiled cleanly via `CMake: Build Tests (Debug)`. `scripts/export_ui_snapshots.ps1 -OutputDir build/ui-snapshots-phase51-both` confirmed that the default 980px editor width now collapses the Sample browser rail, keeps `Sample Information` in the inspector, moves `Output` into the inspector, and leaves `Program Map` inline for disclosure when needed.
- 2026-05-05: The next Phase 5.1 follow-up validated the user-facing Sample rail toggles plus the wide `Effects` inspector promotion. After fixing resize-time width-dependent visibility refresh for the responsive Sample layout, a direct harness export to `build/ui-snapshots-phase51-full` produced `sample`, `sample_modulation`, `sample_wide`, `library`, `mapping`, `player`, `generate`, `capture`, and `about`, and `scripts/compare_ui_snapshots.ps1 -ActualDir build/ui-snapshots-phase51-full -BaselineDir tests/ui-snapshot-baselines/current` returned `PASS` for all nine snapshots after refreshing the committed baseline set.
- 2026-05-05: The next Phase 5.1 slice compiled cleanly via `CMake: Build Tests (Debug)`. A direct harness export to `build/ui-snapshots-phase51-async` confirmed the new `sample_wide_tall` and `sample_wide_focus` states alongside the existing snapshot set, and the accepted state was refreshed into `tests/ui-snapshot-baselines/current` before rerunning exact-match comparison.
- 2026-05-05: The next Phase 5.1 follow-up compiled cleanly via `CMake: Build Tests (Debug)`. A direct harness export to `build/ui-snapshots-phase51-priority` confirmed that `sample_wide` now prioritizes `Filter Envelope + Mod` over `Effects`, while `sample_wide_browser_only` and `sample_wide_inspector_only` capture the new one-rail-off wide states; the accepted state was then refreshed into `tests/ui-snapshot-baselines/current` and exact-match comparison returned `PASS` for all 13 snapshots.
- 2026-05-05: The next Phase 5.1 follow-up compiled cleanly via `CMake: Build Tests (Debug)`. A direct harness export to `build/ui-snapshots-phase51-collapse` confirmed that `sample_wide_medium` captures the shorter-wide `Effects`-only breakpoint and `sample_wide_cards_collapsed` captures the new header-driven collapsed inspector-card state; the accepted state was then refreshed into `tests/ui-snapshot-baselines/current` and exact-match comparison returned `PASS` for all 15 snapshots.
- 2026-05-05: The next Phase 5.1 follow-up compiled cleanly via `CMake: Build Tests (Debug)`. A direct harness export to `build/ui-snapshots-phase51-search` confirmed the new searchable preset strip in the Sample top bar, including `sample_preset_search`, while the inspector-card expand/collapse state now persists through the processor-backed UI state seam; the accepted state was then refreshed into `tests/ui-snapshot-baselines/current` and exact-match comparison returned `PASS` for all 16 snapshots.
- 2026-05-05: The next Phase 5.1 follow-up compiled cleanly via `CMake: Build Tests (Debug)`. A direct harness export to `build/ui-snapshots-phase51-output-fresh` confirmed that `sample`, `sample_wide`, `sample_wide_medium`, `sample_wide_cards_collapsed`, `sample_wide_inspector_only`, and `sample_wide_tall` now reserve a compact `Output` card inside the Sample inspector rail, with collapsed advanced cards still available as right-rail affordances. The accepted state was then refreshed into `tests/ui-snapshot-baselines/current` and exact-match comparison returned `PASS` for all 16 snapshots.

# UI Modernization Spec

## Status
- Owner: GitHub Copilot + repo maintainers
- Started: 2026-05-03
- Current phase: Phase 2 - sample editor directness

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
- [ ] Phase 2.2: Improve slice visibility and interactions.
- [x] Phase 2.3: Make amp and filter envelope graphs directly editable.
- [x] Phase 3.0: Collapse secondary Sample-page sections behind progressive disclosure.
- [ ] Phase 3.1: Rework Player into a persistent performance surface.
- [ ] Phase 3.2: Fold Library into a persistent or quickly accessible browser rail.
- [ ] Phase 4.1: Add visual modulation routing and destination feedback.
- [ ] Phase 5.1: Restructure the full sampler layout around browser/workspace/inspector.
- [ ] Validation 1.0: Land deterministic UI snapshot capture and artifact export for automated feedback.

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

## Validation Notes
- Current repo validation is strong for engine behavior, but UI visual changes do not yet have a dedicated screenshot or golden-image harness.
- Until a UI snapshot harness exists, each UI milestone must at minimum compile cleanly and be documented here.
- 2026-05-03: `Build_CMakeTools` completed successfully after the Phase 1 UI changes.
- 2026-05-03: `Build_CMakeTools` completed successfully after the Phase 2.1, Phase 3.0, and slice-visibility follow-up changes.
- 2026-05-04: `Build_CMakeTools` completed successfully after the editable-envelope and tab-state fixes.
- 2026-05-04: UI snapshot harness scaffolding now exists, with a staged rollout plan:
	- Stage 1: deterministic offscreen PNG capture for representative editor tabs and states.
	- Stage 2: publish screenshot artifacts from CI for human feedback on each UI iteration.
	- Stage 3: add curated baseline images and image-diff thresholds once layouts stabilize.
- 2026-05-04: The new harness target still needs one more end-to-end validation pass in the Debug/test flow before it can be treated as a fully reliable UI regression check.
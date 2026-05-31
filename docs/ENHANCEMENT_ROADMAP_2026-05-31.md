# Audiocity — Enhancement Roadmap

```text
Review conducted: 2026-05-31
Reviewer: Claude Opus (via GitHub Copilot)
Perspectives: Software Optimization Engineering + Product Management
Codebase: Audiocity (JUCE/C++20 hybrid sampler — Standalone + VST3)
```

Companion to `CODE_REVIEW_2026-05-31.md`. This is the handoff document.
Category: Performance | Reliability | Feature | UX | Observability | Security | DX ·
Perspective: Engineering | Product | Both · Effort: XS/S/M/L/XL · Impact: Low/Medium/High/Critical

---

## Implementation Progress

Last updated: 2026-05-31

| Item | Status | Implemented changes | Validation |
| --- | --- | --- | --- |
| QW-1 | Done | Added `.github/workflows/build-and-test.yml` to build `audiocity_offline_tests` + `audiocity_preset_runtime_smoke` and run non-UI CTest coverage on PR/push/manual dispatch. | `ctest --test-dir build -C Debug --output-on-failure --exclude-regex '^audiocity_ui_snapshot_harness$'` passed 4/4 locally. |
| QW-2 | Done | Skips the stereo per-sample autopan `std::sin` loop when autopan depth is effectively off. | Offline render CTest entries passed. |
| QW-3 | Done | Hoists the saturation-drive active check so the disabled saturation path avoids per-sample calls. | Offline render CTest entries passed. |
| QW-4 | Done | Replaced repeated pending-event boundary scans with a cursor over sorted events. | Offline render CTest entries passed. |
| QW-5 | Done | Gated drag/drop `DBG` logging behind `kVerboseDragDropLogging`. | `audiocity_ui_snapshot_harness` target builds. |
| QW-6 | Done | Added rolling in-memory import diagnostics, local persistence under user app data, and a `Copy Log` action in the Tech panel. | UI harness and preset smoke targets build; non-UI CTest passed. |
| MP-4 | In progress | Added archive-relative path validation and traversal-shaped ZIP sample-reference rejection for Bitwig `.multisample` and Korg `.korgmultisample`; added an NCW converter regression covering shell metacharacters in input paths. | `audiocity_offline_tests` CTest entries passed. Remaining: fuzz/corpus harness and broader importer bounds audits. |

Known validation note: the UI snapshot CTest currently reports broad baseline mismatches across unrelated screens in this local environment; no snapshot baselines were updated as part of this implementation pass.

---

## Summary Table

| # | Item | Category | Effort | Impact | Area |
| --- | --- | --- | --- | --- | --- |
| QW-1 | Run offline test suite + smoke in CI | Reliability | S | Critical | [.github/workflows](.github/workflows) |
| QW-2 | Skip per-sample autopan when depth ≈ 0 | Performance | XS | Medium | [EngineCore.cpp](src/engine/EngineCore.cpp#L1473) |
| QW-3 | Hoist saturation drive check out of per-sample loop | Performance | XS | Low | [EngineCore.cpp](src/engine/EngineCore.cpp#L1442) |
| QW-4 | Linear cursor for `findNextPendingEventOffset` | Performance | XS | Low | [EngineCore.cpp](src/engine/EngineCore.cpp#L1518) |
| QW-5 | Gate drag-drop `DBG` logging behind one verbose flag | DX | XS | Low | [PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5327) |
| QW-6 | Persistent + copyable import diagnostics | UX | S | Medium | [PluginEditor.cpp](src/plugin/PluginEditor.cpp) |
| MP-1 | Background (async) import with progress/cancel | UX/Performance | M | High | [PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5303) |
| MP-2 | Split `PluginEditor.cpp` by tab into TUs | DX | M | High | [PluginEditor.cpp](src/plugin/PluginEditor.cpp) |
| MP-3 | DecentSampler `.dspreset` exporter | Feature | M | High | [src/engine](src/engine) |
| MP-4 | Importer fuzz harness + hardening | Security/Reliability | M | High | [src/engine](src/engine) importers |
| MP-5 | Migrate tests to a framework (Catch2) | DX/Reliability | M | Medium | [tests](tests) |
| SI-1 | macOS + AU (and CLAP) cross-platform build | Feature | XL | Critical | build/packaging |
| SI-2 | Editor architecture decomposition (MVVM-ish) | DX/Reliability | L | High | [src/plugin](src/plugin) |
| SI-3 | Opt-in local import-outcome telemetry | Observability/Product | L | High | [src/plugin](src/plugin) |
| SI-4 | Large-library browser scaling (virtualized + incremental index) | Performance/Feature | L | Medium | [LibraryFileIndex.cpp](src/plugin/LibraryFileIndex.cpp) |

---

## 1. Quick Wins (< 1 day each)

### QW-1 — Run the offline test suite in CI 🔴

- **Category:** Reliability · **Perspective:** Engineering · **Effort:** S · **Impact:** Critical
- **Area:** [.github/workflows/](.github/workflows), [tests/OfflineRenderTests.cpp](tests/OfflineRenderTests.cpp), [tests/CMakeLists.txt](tests/CMakeLists.txt)
- **Status:** Done on 2026-05-31. Added `.github/workflows/build-and-test.yml`; local non-UI CTest passed.
- **Recommendation:** Add a `build-and-test.yml` workflow on `windows-2022` that configures the `default` preset, builds `audiocity_offline_tests` and `PresetRuntimeSmoke`, and runs `ctest --test-dir build -C Debug --output-on-failure` on every PR and push to `main`. The whole offline suite already runs device-free, so no audio hardware is needed. This closes the single biggest reliability gap.

### QW-2 — Skip per-sample autopan when depth ≈ 0 🟡

- **Category:** Performance · **Perspective:** Engineering · **Effort:** XS · **Impact:** Medium
- **Area:** [EngineCore.cpp](src/engine/EngineCore.cpp#L1473)
- **Status:** Done on 2026-05-31. Autopan depth-off path now uses constant pan gains without per-sample sine work.
- **Recommendation:** When `autopanSettings_.depth <= 1e-4`, replace the per-sample `std::sin` loop with constant pan gains applied via `juce::FloatVectorOperations::multiply` on each channel. Removes a transcendental-per-sample cost from the default (autopan-off) stereo path.

### QW-3 — Hoist saturation drive check 🔵

- **Category:** Performance · **Perspective:** Engineering · **Effort:** XS · **Impact:** Low
- **Area:** [EngineCore.cpp](src/engine/EngineCore.cpp#L1442), [EngineCore.cpp](src/engine/EngineCore.cpp#L3899)
- **Status:** Done on 2026-05-31. The disabled saturation path now skips the per-sample saturation loop.
- **Recommendation:** Compute `saturationActive = drive > 1e-5` once before the mix-bus loop and skip both per-sample passes when inactive, eliminating a function call + branch per sample when saturation is off.

### QW-4 — Linear cursor for next-event scan 🔵

- **Category:** Performance · **Perspective:** Engineering · **Effort:** XS · **Impact:** Low
- **Area:** [EngineCore.cpp](src/engine/EngineCore.cpp#L1518)
- **Status:** Done on 2026-05-31. Segment boundary discovery now advances through the sorted event list with a cursor.
- **Recommendation:** Events are already sorted by offset; replace the O(events) rescan per segment with a single advancing index, making segment boundary discovery O(events) per block instead of O(events²).

### QW-5 — Gate drag-drop logging 🔵

- **Category:** DX · **Perspective:** Engineering · **Effort:** XS · **Impact:** Low
- **Area:** [PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5327)
- **Status:** Done on 2026-05-31. Drag/drop debug logging is controlled by `kVerboseDragDropLogging`.
- **Recommendation:** Collapse the cluster of `DBG("[DnD] ...")` statements behind a single `kVerboseDragDrop` constexpr flag so debug builds stay quiet by default and the path is easy to re-enable.

### QW-6 — Persistent, copyable import diagnostics 🟡

- **Category:** UX · **Perspective:** Both · **Effort:** S · **Impact:** Medium
- **Area:** [PluginEditor.cpp](src/plugin/PluginEditor.cpp), import status surfaces
- **Status:** Done on 2026-05-31. Recent import attempts are kept in memory, appended to a local log, and exposed through a `Copy Log` Tech-panel action.
- **Recommendation:** Keep the last N import attempts (path, format, outcome, reason) in a small ring buffer and expose a "Copy import log" action. Turns ephemeral failure labels into actionable bug reports and directly feeds SI-3.

---

## 2. High-Impact Medium Projects (1–2 weeks)

### MP-1 — Background import with progress + cancel 🟠

- **Category:** UX/Performance · **Perspective:** Both · **Effort:** M · **Impact:** High
- **Area:** [PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5303) (`timerCallback` drop handling, `loadFileAsInstrument`)
- **Problem:** Importing large multisamples/libraries runs synchronously on the message thread, freezing the UI with no feedback.
- **Approach:** Move parse/decode onto a `juce::ThreadPool` job; show a determinate/indeterminate progress affordance and a cancel button; marshal the finished `Program` + sample buffers back to the message thread and swap atomically into the engine. Reuse the existing snapshot-swap path so the audio thread is untouched.
- **Done when:** dropping a multi-GB library keeps the UI responsive, shows progress, and can be cancelled cleanly.

### MP-2 — Decompose `PluginEditor.cpp` by tab 🟠

- **Category:** DX · **Perspective:** Engineering · **Effort:** M · **Impact:** High
- **Area:** [PluginEditor.cpp](src/plugin/PluginEditor.cpp) (11,151 lines)
- **Problem:** One translation unit owns every tab; every UI edit triggers a full-file recompile and a high cognitive load.
- **Approach:** Extract per-tab component classes (Sample, Mapping, Player, Generate, Capture, Library, About) into their own `.h/.cpp` pairs, leaving `AudiocityAudioProcessorEditor` as a thin coordinator. No behavior change; guard with UI snapshot baselines.
- **Done when:** the editor file is < ~2k lines, snapshots are unchanged, and incremental builds of a single tab no longer recompile all UI.

### MP-3 — DecentSampler `.dspreset` exporter 🟠

- **Category:** Feature · **Perspective:** Product · **Effort:** M · **Impact:** High
- **Area:** new `src/engine/DecentSamplerExporter.{h,cpp}` paralleling [SfzExporter.cpp](src/engine/SfzExporter.cpp) and the existing [DecentSamplerImporter.cpp](src/engine/DecentSamplerImporter.cpp)
- **Problem:** The product imports ~14 formats but exports only SFZ, making it a one-way street.
- **Approach:** Map `Program`/zones/RR/velocity/loop into `.dspreset` XML; reuse the importer's schema knowledge for round-trip fidelity; add offline round-trip tests (import → export → re-import equality on key fields).
- **Done when:** an imported instrument exports to a `.dspreset` that DecentSampler loads and that re-imports into Audiocity with matching mapping.

### MP-4 — Importer fuzz harness + hardening 🟠

- **Category:** Security/Reliability · **Perspective:** Engineering · **Effort:** M · **Impact:** High
- **Area:** [src/engine/Sf2Importer.cpp](src/engine/Sf2Importer.cpp), [BinaryMultisampleImporters.cpp](src/engine/BinaryMultisampleImporters.cpp), [XmlMultisampleImporters.cpp](src/engine/XmlMultisampleImporters.cpp), ZIP-based importers
- **Status:** In progress as of 2026-05-31. Completed bounded hardening slices for ZIP sample references and NCW converter command handling: archive paths are validated as relative/no-traversal, Bitwig and Korg archive importers skip unsafe entries, offline regressions cover traversal-shaped sample refs, and NCW converter quoting is covered with a shell-metacharacter input-path regression. Remaining work includes fuzz/corpus harness coverage and broader binary/XML bounds audits.
- **Problem:** Importers parse untrusted third-party binary/XML/ZIP; this is the realistic attack surface.
- **Approach:** Add a libFuzzer/AFL target (or a corpus-driven offline harness) over each importer entry point; audit chunk/length reads for bounds checks, add ZIP path-traversal guards, and cap declared sizes before allocation. Validate the NCW external-command path uses a quoted argument vector (no shell concatenation).
- **Done when:** importers survive a malformed-input corpus without crashes/OOM and ZIP extraction rejects `../` paths.

### MP-5 — Migrate tests to Catch2 🟡

- **Category:** DX/Reliability · **Perspective:** Engineering · **Effort:** M · **Impact:** Medium
- **Area:** [tests/OfflineRenderTests.cpp](tests/OfflineRenderTests.cpp), [tests/CMakeLists.txt](tests/CMakeLists.txt)
- **Problem:** Hand-rolled `bool runX()` functions require manual registration and give coarse failure reporting.
- **Approach:** Adopt Catch2 (single-header, CMake-friendly); wrap existing assertions in `TEST_CASE`/`SECTION`; gain tagging, filtering, and richer diagnostics. Do this *after* QW-1 so CI guards the migration.
- **Done when:** tests run under `ctest` via Catch2 with no loss of coverage and clearer failure output.

---

## 3. Strategic Initiatives (multi-week)

### SI-1 — Cross-platform: macOS + Audio Unit (and CLAP) 🔴 leverage

- **Problem:** Windows-only, x64-only, VST3-only caps the addressable market; every direct competitor is cross-platform.
- **Proposed approach:** Add a macOS CMake/preset path; enable JUCE's AU wrapper alongside VST3; add `clap-juce-extensions` for a CLAP target. Stand up macOS CI (GitHub `macos-latest`), code signing, and notarization. Audit Windows-isms (paths, `\\`, registry, external-command assumptions) behind platform abstractions.
- **Key risks/open questions:** signing/notarization pipeline; ASIO is Windows-only (use CoreAudio on macOS); the NCW external-converter assumption; volume of `juce::File` path handling that assumes Windows separators.
- **Success metrics:** AU + VST3 load and pass the offline suite on macOS; installer/notarized artifacts produced in CI; first macOS user installs.

### SI-2 — Editor/processor architecture decomposition 🟠

- **Problem:** Logic, state, and view are intermixed across 11k+15k-line files, slowing every change and concentrating risk.
- **Proposed approach:** Introduce a thin view-model layer per feature area so components observe state rather than reaching into the processor; complete MP-2's file split; define clear ownership boundaries between `AudiocityAudioProcessor` (DSP/state) and UI.
- **Key risks/open questions:** avoiding churn that invalidates UI snapshot baselines; staging the refactor so each step is shippable.
- **Success metrics:** no file > ~2.5k lines; new-contributor time-to-first-PR drops; UI snapshots stable across the refactor.

### SI-3 — Opt-in, local-first import telemetry 🟠

- **Problem:** The roadmap prioritizes importers with no data on which formats users actually use or where imports fail.
- **Proposed approach:** Strictly opt-in, default-off, locally stored log of import attempts and outcomes (format, success/fail, failure reason — never file contents or paths off-device unless explicitly shared). Surface an aggregate view and a "share report" export. Builds directly on QW-6.
- **Key risks/open questions:** privacy framing and consent UX; keeping it local-first; ensuring zero impact when disabled.
- **Success metrics:** opt-in rate; a ranked list of real-world import formats/failures driving the next importer investment.

### SI-4 — Large-library browser scaling 🟡

- **Problem:** The simple file index and list rendering are unproven at 10k+ items.
- **Proposed approach:** Virtualized list rendering, incremental/background indexing with persisted cache, and lazy peak/metadata loading through `PeakPreviewCache`. Benchmark against a synthetic 50k-entry library.
- **Key risks/open questions:** index invalidation on watched-folder changes; memory ceiling for cached metadata.
- **Success metrics:** smooth scrolling and < N ms search on a 50k-item library; bounded memory.

---

## 4. Debt Retirement Candidates

- **Drag-drop `DBG` cluster** ([PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5327)) — collapse behind one flag (QW-5).
- **Repeated full-buffer display copies** — route all display reads through `PeakPreviewCache` and drop ad-hoc `copyLoadedSampleDisplayData`/`buildDisplayPeaks*` allocations on UI refresh paths ([EngineCore.h](src/engine/EngineCore.h#L189)).
- **Hand-rolled test runner registration** — retire in favor of Catch2 auto-registration (MP-5).
- **Per-segment recomputation of per-voice invariants** ([EngineCore.cpp](src/engine/EngineCore.cpp#L1777)) — cache invariants on `VoiceState` at note start.

## 5. Dependency Upgrade Path (ordered by value/risk)

1. **Add Catch2** (new, isolated test dep — lowest risk, immediate DX value).
2. **Add `clap-juce-extensions`** (additive plugin format; no impact on existing targets).
3. **Track JUCE 8.x point releases** (already current at 8.0.4; upgrade conservatively, gated by the new CI from QW-1).
4. **macOS toolchain/signing deps** (largest effort; sequence under SI-1).

No removals are recommended — the dependency surface is already minimal and healthy.

---

## Prompt Handoff

Ready-to-use prompts for an implementing model. Each references concrete files/lines from the review.

**QW-1 (CI):**
> Add `.github/workflows/build-and-test.yml` for Audiocity. On `pull_request` and `push` to `main`, on `windows-2022`: configure the CMake `default` preset, build targets `audiocity_offline_tests` and `PresetRuntimeSmoke` (see [tests/CMakeLists.txt](tests/CMakeLists.txt)), then run `ctest --test-dir build -C Debug --output-on-failure`. The offline suite in [tests/OfflineRenderTests.cpp](tests/OfflineRenderTests.cpp) runs without audio hardware. Fail the job on any test failure.

**QW-2/QW-3/QW-4 (render micro-opts):**
> In [src/engine/EngineCore.cpp](src/engine/EngineCore.cpp) `render`: (a) at line ~1473, when `autopanSettings_.depth <= 1e-4`, apply constant pan via `juce::FloatVectorOperations::multiply` and skip the per-sample `std::sin` loop; (b) at line ~1442, compute `saturationActive` once and skip both per-sample saturation passes when off; (c) at line ~1518, replace `findNextPendingEventOffset`'s O(events) rescan with a single advancing cursor over the already-sorted `pendingEvents_`. Add/extend offline determinism tests to prove identical output.

**MP-1 (async import):**
> Refactor instrument loading in [src/plugin/PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5303) so `loadFileAsInstrument` runs parse/decode on a `juce::ThreadPool`, shows progress + cancel, and marshals the resulting `Program`/sample buffers back to the message thread to swap via the existing atomic snapshot path. Do not touch the audio thread contract in [docs/02-real-time-rules.md](docs/02-real-time-rules.md).

**MP-2 (editor split):**
> Extract each tab in [src/plugin/PluginEditor.cpp](src/plugin/PluginEditor.cpp) (Sample, Mapping, Player, Generate, Capture, Library, About) into its own component `.h/.cpp`, leaving `AudiocityAudioProcessorEditor` as a coordinator. Preserve behavior; keep UI snapshot baselines under [tests/ui-snapshot-baselines](tests/ui-snapshot-baselines) unchanged.

**MP-3 (DS exporter):**
> Create `src/engine/DecentSamplerExporter.{h,cpp}` mirroring [src/engine/SfzExporter.cpp](src/engine/SfzExporter.cpp), serializing `Program`/zones/RR/velocity/loop to `.dspreset` XML using schema knowledge from [src/engine/DecentSamplerImporter.cpp](src/engine/DecentSamplerImporter.cpp). Add offline import→export→re-import round-trip tests.

**MP-4 (importer fuzzing):**
> Add a fuzz/corpus harness over importer entry points in [src/engine/](src/engine) (SF2, binary multisample, XML, ZIP). Audit length/chunk reads for bounds checks, add ZIP path-traversal guards, cap declared sizes before allocation, and confirm the NCW external command uses a quoted argument vector.

**SI-1 (cross-platform):**
> Add macOS support to Audiocity: CMake/preset path for macOS, JUCE AU wrapper + `clap-juce-extensions` alongside VST3, macOS CI with signing/notarization, and platform abstractions for Windows-specific path/registry/external-command code. Pass the offline suite on macOS.

# Audiocity — Deep Code Review

```
Review conducted: 2026-05-31
Reviewer: Claude Opus (via GitHub Copilot)
Perspectives: Software Optimization Engineering + Product Management
Codebase: Audiocity (JUCE/C++20 hybrid sampler — Standalone + VST3)
```

Severity legend: 🔴 Critical · 🟠 High · 🟡 Medium · 🔵 Low

---

## Project Context

**Purpose & domain.** Audiocity is a Windows-focused hybrid sampler built on JUCE 8.0.4 and C++20, shipped as a standalone app and a VST3 instrument plugin from a single `AudioProcessor`. Target users are music producers and sound designers who want broad multisample import coverage, transient slicing, modulation, disk streaming, and a curated factory preset bank.

**Tech stack.**
- Language: C++20. Build: CMake (3.22+) with presets (`default`, `release-selfcontained-asio`).
- Framework: JUCE 8.0.4 (audio_basics, audio_formats, dsp, gui). VST3 via JUCE wrapper.
- Vendored deps under `third_party/` (REX SDK, optional ASIO SDK).
- Packaging: Inno Setup installer + portable zip via `scripts/build_release.ps1`.
- CI: a single GitHub Actions workflow ([.github/workflows/ui-snapshots.yml](.github/workflows/ui-snapshots.yml)) that only exports/diffs UI snapshots.

**Architecture map.**
- Engine ([src/engine/](src/engine)): `EngineCore` (voice rendering, mixing, DSP, modulation, streaming), `VoicePool` (allocation/stealing), `ProgramSnapshot`/`ProgramModel` (immutable program data), and a large family of importers (SFZ, SF2, DecentSampler, Bitwig, MPC/binary, XML, REX, legacy NKI probe).
- Plugin/UI ([src/plugin/](src/plugin)): `AudiocityAudioProcessor` (parameter state, preview/capture, host glue) and `AudiocityAudioProcessorEditor` (the entire UI), plus mapping model, library index/metadata, preset JSON, peak cache.
- Tools ([tools/](tools)): `PresetAuthor` (regenerates the 64-preset factory bank) and `PresetAuditioner` (offline render QA).
- Tests ([tests/](tests)): hand-rolled offline render harness (`runXxxTest()` functions), UI snapshot harness, packaging config tests.

**State & RT model.** UI/message thread writes immutable snapshots; the audio thread reads them via `std::atomic<std::shared_ptr<const T>>` (lock-free swap). Voices, buffers, and event queues are preallocated. The documented hard rule (no allocation/locks/IO/logging on the audio thread) is broadly respected in `EngineCore::render`.

**Size & age signals.** ~30k lines of C++. Heavy concentration in three files: [src/plugin/PluginEditor.cpp](src/plugin/PluginEditor.cpp) (11,151 lines), [src/plugin/PluginProcessor.cpp](src/plugin/PluginProcessor.cpp) (4,439 lines), [src/engine/EngineCore.cpp](src/engine/EngineCore.cpp) (3,379 lines). Specs in `docs/` are thorough and current (versioned roadmap through v1.3.1). Test posture is strong on engine determinism and UI snapshots, weak on automated CI execution.

**Overall.** This is a mature, well-specified, test-conscious codebase with a disciplined real-time core. The principal risks are not correctness but **maintainability concentration** (a few enormous files), **CI gaps** (the offline test suite never runs on PRs), and **platform/market reach** (Windows + VST3 only).

---

## Phase 2 — Performance & Technical Review (Engineering)

### 2A. Algorithmic & Computational Efficiency

🟡 **Per-voice setup recomputed for every event segment.** In [src/engine/EngineCore.cpp](src/engine/EngineCore.cpp#L1777) `renderActiveVoices` recomputes a large block of per-voice constants (sample-length clamps, channel pointers, loop ranges, key-tracking `std::pow`, crossfade scales) at the top of each call. `render` ([EngineCore.cpp](src/engine/EngineCore.cpp#L1393)) calls `renderActiveVoices` once per inter-event segment, so a block carrying several MIDI events repeats this whole setup N times per active voice even though only `numSamples` changes. With dense MIDI (arpeggios, chords) this is measurable. Consider caching the invariant per-voice derived values on the `VoiceState` when the note starts and only recomputing position-dependent values per segment.

🔵 **`findNextPendingEventOffset` is O(events) per segment.** [EngineCore.cpp](src/engine/EngineCore.cpp#L1518) scans all pending events for every segment boundary → O(events²) per block. Bounded by the 1024-event array so the absolute cost is small, but pre-sorting (already done via `sortPendingEventsByOffset`) means a single linear cursor would make this O(events).

🟡 **Unconditional per-sample autopan with `std::sin`.** [EngineCore.cpp](src/engine/EngineCore.cpp#L1473) runs a per-sample loop computing `std::sin(...)` for autopan whenever `numChannels >= 2`, **even when `autopanSettings_.depth == 0`** (the default). This pays a transcendental per sample on every stereo block regardless of whether autopan is engaged. When depth ≈ 0, apply the constant pan gains with `juce::FloatVectorOperations::multiply` and skip the loop entirely. (Quick win.)

🔵 **Per-sample saturation function call when disabled.** The mix-bus saturation loop ([EngineCore.cpp](src/engine/EngineCore.cpp#L1442)) calls `processSaturationSample` for every sample of both channels; the function early-returns when `drive <= 1e-5` ([EngineCore.cpp](src/engine/EngineCore.cpp#L3899)) but still incurs a call/branch per sample. Hoist the `drive` check out of the loop and skip both passes when saturation is off. (Quick win.)

### 2B. Database & Data Layer

🔵 **No database.** State is files (`.acp` presets, JSON library metadata, SFZ). N/A by design. The library index ([src/plugin/LibraryFileIndex.cpp](src/plugin/LibraryFileIndex.cpp)) and metadata cache are the closest analog; they are small and file-backed. No pagination concerns at current scale, but very large libraries (10k+ entries) have not been exercised — see 3D.

### 2C. Concurrency, Async & I/O

🟢 **Strong lock-free core.** Program/sample/audio snapshots are swapped via `std::atomic<std::shared_ptr<const T>>` with acquire/release ordering ([EngineCore.h](src/engine/EngineCore.h#L560)), and stream priming is explicitly serviced off the audio thread (`serviceStreamPriming`). This is the right pattern and is honored consistently.

🟡 **Deferred drag-and-drop runs heavy work on the 60 Hz UI timer.** `timerCallback` ([src/plugin/PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5303)) performs synchronous instrument loading (`loadFileAsInstrument`) inside the timer when a drop is pending. Importing a large multisample blocks the message thread and stalls UI repaint. Move import parsing to a background thread and marshal the result back. (Loading is correctly deferred *out of* the OLE modal loop, but not *off* the message thread.)

🔵 **DBG logging in the hot UI timer.** `timerCallback` emits multiple `DBG(...)` calls on the drag-drop path ([PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5327)). `DBG` compiles out in Release, so this is debug-only, but the volume of stringified path logging is worth gating behind a single verbose flag.

### 2D. Memory & Resource Management

🟢 Preallocated voice/event/scratch buffers; `render` bails out if `numSamples` exceeds preallocated scratch capacity ([EngineCore.cpp](src/engine/EngineCore.cpp#L1406)) rather than allocating. Good.

🟡 **`copyLoadedSampleDisplayData` returns a full `AudioBuffer<float>` by value.** [EngineCore.h](src/engine/EngineCore.h#L189) and the various `buildDisplayPeaks*` helpers copy/allocate sizeable display buffers; these are called from UI refresh paths. For long samples this is repeated allocation/copy on the message thread during scrolling/zoom. Cache peak pyramids (the project already has `PeakPreviewCache` — ensure all display reads route through it).

### 2E. Network & API Efficiency

🔵 **No network surface.** Sole external endpoint is the "Buy Me a Coffee" link in the README. N/A. Optional NCW conversion shells out to an external command via `AUDIOCITY_NCW_CONVERTER_COMMAND` — ensure that path is validated/quoted to avoid command injection (see 2H).

### 2F. Build, Bundle & Startup

🟠 **Three God files dominate build and cognition.** [PluginEditor.cpp](src/plugin/PluginEditor.cpp) at 11k lines, [PluginProcessor.cpp](src/plugin/PluginProcessor.cpp) at 4.4k, and [EngineCore.cpp](src/engine/EngineCore.cpp) at 3.4k force full-file recompiles on any edit and are the dominant incremental-build cost. They are also the dominant code-review/onboarding cost. Splitting the editor by tab (Sample/Mapping/Player/Generate/Capture/Library) into separate translation units is the single highest-leverage DX investment.

🔵 **Static MSVC runtime for release** keeps the binary self-contained — good for distribution; just note it precludes runtime CRT security patching via the OS.

### 2G. Observability & Reliability Gaps

🔴 **The offline test suite does not run in CI.** The only workflow ([.github/workflows/ui-snapshots.yml](.github/workflows/ui-snapshots.yml)) builds and diffs UI snapshots. The extensive engine determinism / import / mapping test suite in [tests/OfflineRenderTests.cpp](tests/OfflineRenderTests.cpp) (dozens of `runXxxTest()` cases) and `PresetRuntimeSmoke` are **never executed on pull requests**. Regressions in the real-time engine, importers, and preset round-trips can land undetected. This is the most consequential reliability gap in the repo.

🟡 **No crash reporting / structured logging.** There is good *engine telemetry* (steal count, segment rebuild count, stream prime hit/miss counters in [EngineCore.h](src/engine/EngineCore.h#L207)), but no crash dumps, no log file, and no way for a user to report a failed import beyond an on-screen status string. Failed imports surface a diagnostic label but nothing is persisted.

🟡 **Custom test harness limits leverage.** Tests are `bool runX()` functions aggregated manually rather than a framework (Catch2/GoogleTest). This works but lacks per-assertion reporting, tagging, filtering, and parallel execution, and makes it easy to forget to register a new test in the runner.

### 2H. Security / Performance Intersections

🟠 **External converter command execution.** NCW support shells out to a user/environment-supplied command (`AUDIOCITY_NCW_CONVERTER_COMMAND`). Confirm arguments (file paths) are passed as a quoted argument vector, never string-concatenated into a shell line, to avoid command injection from crafted filenames. (OWASP A03 Injection.)

🟡 **Importers parse untrusted binary/XML.** SF2, EXS24, MPC/binary, Bitwig/Korg ZIP and XML importers ([src/engine/](src/engine)) consume arbitrary third-party files. These are the realistic attack surface for a sampler. Ensure: bounds-checked chunk/length reads, ZIP entry path traversal guards (no `../` extraction), and caps on declared sizes before allocating. A fuzz harness over the importers would pay for itself.

🔵 **Regex DoS** is not a concern — no heavy user-facing regex was observed in hot paths.

### 2I. Dependency Health

🟢 JUCE 8.0.4 is recent. REX SDK and ASIO SDK are vendored and optional. No abandoned or duplicate-functionality dependencies were observed. Dependency surface is intentionally minimal — appropriate for an audio plugin.

---

## Phase 3 — Product & Feature Review (Product Management)

### 3A. Feature Completeness vs. User Needs

🟢 **Import breadth is a genuine differentiator.** Direct WAV/AIFF/REX/RX2/NCW plus SFZ, SF2, DecentSampler, Bitwig, MPC, 1010music, TAL, TX16Wx, Korg, Ableton, EXS24, and a legacy NKI subset is broader than most indie samplers and rivals commercial converters. This is the product's strongest moat.

🟠 **Export is asymmetric.** The engine imports ~14 formats but exports only SFZ ([src/engine/SfzExporter.cpp](src/engine/SfzExporter.cpp)). Producers increasingly expect to round-trip into DecentSampler `.dspreset` (free, cross-platform, popular). One additional exporter would convert Audiocity from a one-way importer into a content-pipeline hub.

🟡 **Two macro controls only.** [EngineCore.h](src/engine/EngineCore.h#L99) defines `kMacroControlCount = 2`. Modern samplers expose 4–8 macros + a mod matrix. Modulation routing exists (`ModulationRoutingSettings`) but the user-facing surface is narrow relative to the engine's capability.

### 3B. UX & Developer Experience Gaps

🟠 **Synchronous import on drop blocks the UI** (see 2C). From the user's perspective, dragging a large library appears to "freeze" the app — no progress indicator, no cancel. A background import with a progress/cancel affordance is table stakes for a sampler handling multi-GB libraries.

🟡 **Import failure feedback is ephemeral.** Failed loads show a status label but no persistent, copyable diagnostic, so users cannot easily report *why* a given `.nki`/`.sf2` failed. A "copy diagnostics" action and a rolling import log would cut support friction.

🟡 **Developer experience is gated by file size.** Any UI change requires navigating an 11k-line file; new contributors face a steep ramp. (Engineering DX = product velocity.)

### 3C. Data & Analytics Gaps

🟡 **No product telemetry by design.** There is rich *engine* telemetry but zero product analytics — no opt-in signal on which formats users import, which presets they load, or where imports fail. This is privacy-friendly but leaves the roadmap flying blind on the highest-value question for this product: *which import formats actually matter to users.* A strictly opt-in, local-first usage log (especially of import attempts and outcomes) would directly inform format prioritization.

### 3D. Competitive & Domain Gaps

🟠 **Platform reach.** Windows-only, x64-only, VST3-only. Direct comparables (DecentSampler, TX16Wx, Kontakt Player, Sforzando) are cross-platform and ship AU (macOS) and often AAX. JUCE makes macOS + AU largely a build-and-sign exercise; this is the single biggest addressable-market expansion. CLAP (via `clap-juce-extensions`) is a low-cost modern-format add.

🟡 **Large-library scale unproven.** Browser/library index has not been demonstrated against 10k+ item libraries; virtualized lists and incremental indexing may be needed (the roadmap mentions watched folders/tags, but the current index is simple).

🟡 **No round-robin/velocity-layer authoring depth surfaced.** The model supports zones/RR/velocity (per docs/roadmap Epic B), but compared to commercial mapping editors there is no group-level crossfade curve editing or per-zone FX.

### 3E. Technical Investment vs. Value Misalignment

🟡 **Heavy investment in many niche importers; lighter investment in export and cross-platform.** The legacy NKI probe ([src/engine/LegacyNkiProbe.cpp](src/engine/LegacyNkiProbe.cpp), 785 lines) and the binary multisample importers represent substantial engineering for formats with a long tail of users, while export (one format) and platform reach (one OS) — both high-leverage for adoption — are comparatively underinvested. The import breadth is a moat; the imbalance is in *what surrounds it*.

🟢 **Preset/auditioner tooling is well-proportioned.** The deterministic `PresetAuthor` + `PresetAuditioner` + offline QA gate is exactly the right investment for shipping a trustworthy factory bank.

### 3F. Monetization & Growth Hooks

🔵 **Single "Buy Me a Coffee" link; no tiering or licensing.** MIT-licensed and donation-funded is a legitimate model. If monetization is ever desired, natural seams exist: a paid "Pro" import pack (protected-format detection → conversion), expanded factory banks, or a cross-platform build tier. No instrumentation exists today to measure conversion or retention (consistent with 3C).

---

## Cross-Cutting Summary

| Theme | Severity | Where |
| --- | --- | --- |
| Offline test suite not in CI | 🔴 | [.github/workflows/ui-snapshots.yml](.github/workflows/ui-snapshots.yml), [tests/OfflineRenderTests.cpp](tests/OfflineRenderTests.cpp) |
| God files (editor/processor/engine) | 🟠 | [PluginEditor.cpp](src/plugin/PluginEditor.cpp), [PluginProcessor.cpp](src/plugin/PluginProcessor.cpp), [EngineCore.cpp](src/engine/EngineCore.cpp) |
| Platform/format reach (Win+VST3 only) | 🟠 | build/packaging, product |
| Synchronous import on UI timer | 🟠 | [PluginEditor.cpp](src/plugin/PluginEditor.cpp#L5303) |
| Importer security hardening + fuzzing | 🟠 | [src/engine/](src/engine) importers |
| Export only SFZ | 🟠 | [SfzExporter.cpp](src/engine/SfzExporter.cpp) |
| Unconditional autopan/saturation per-sample work | 🟡 | [EngineCore.cpp](src/engine/EngineCore.cpp#L1442) |
| Per-voice setup recomputed per segment | 🟡 | [EngineCore.cpp](src/engine/EngineCore.cpp#L1777) |
| No product telemetry to prioritize formats | 🟡 | product |

See `ENHANCEMENT_ROADMAP_2026-05-31.md` for the prioritized, actionable plan.

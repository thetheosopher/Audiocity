# Audiocity — Prioritized Enhancement Roadmap

```text
Review conducted: 2026-09-05
Reviewer: Codex (OpenAI)
Perspectives: Software Optimization Engineering + Product Management
Codebase: Audiocity 1.3.2.0 (JUCE/C++20 hybrid sampler — Standalone + VST3)
```

> Reviewer identity note: the supplied template named a different model. The identity above records the reviewer that actually performed this review.

This document is Phase 4 of the review. Dependencies are intentional: complete QW-1/QW-2 before risky engine or UI work; MP-1 and MP-2 are release blockers; strategic expansion follows a stable Windows release.

## Summary

| Item | Category | Perspective | Effort | Impact | Area |
| --- | --- | --- | --- | --- | --- |
| QW-1 Repair CI/toolchain contract | Reliability / DX | Engineering | XS | Critical | presets, workflows, snapshot script |
| QW-2 Re-establish UI golden truth | Reliability / UX | Both | S | High | UI baselines, docs, workflow |
| QW-3 Remove duplicate CTest entry | DX | Engineering | XS | Medium | tests/CMakeLists.txt |
| QW-4 Compile the shipped products in CI | Reliability | Engineering | S | High | workflows, installer, plugin targets |
| QW-5 Make snapshot limits explicit | Reliability | Both | S | High | ProgramSnapshot, import publish |
| QW-6 Centralize supported formats | UX / DX | Both | S | High | browser, chooser, drag/drop |
| QW-7 Debounce preload changes | Performance / UX | Both | S | High | preload control, segment rebuild |
| MP-1 Audio-thread-owned control plane | Reliability / Performance | Engineering | M | Critical | APVTS, processor, EngineCore |
| MP-2 Owned cancellable import jobs | Reliability / UX | Both | M | Critical | import workers and lifetimes |
| MP-3 Scalable library index and peaks | Performance / UX | Both | M | High | scanner, cache, list model |
| MP-4 Lazy capture/preview memory and compact state | Performance / Reliability | Both | M | High | processor buffers, state codec |
| MP-5 Scalable program snapshots | Reliability / Feature | Both | M | High | ProgramSnapshot, program audio |
| MP-6 Linear event scheduler and safe overflow | Performance / Reliability | Engineering | M | High | pending MIDI events |
| MP-7 Test/build consolidation and hardening | DX / Security | Engineering | M | High | test targets, sanitizer/fuzz CI |
| MP-8 Missing-asset resolver | Reliability / UX | Both | M | High | state restore, imported programs |
| SI-1 Cross-platform and wrapper expansion | Feature / Growth | Both | XL | High | macOS, AU, VST3, later CLAP/Linux |
| SI-2 Portable instrument packaging | Feature / Reliability | Both | L | High | collect/embed/reference, manifests |
| SI-3 Modular editor/processor architecture | DX / Reliability | Engineering | L | High | large translation units, services |
| SI-4 Expressive sampler evolution | Feature | Product | XL | Medium | MPE, multi-output, time-stretch |

## 1. Quick wins — less than one day each

### QW-1 — Repair the CI/toolchain contract

- **Category:** Reliability / DX
- **Perspective:** Engineering
- **Effort:** XS
- **Impact:** Critical
- **Files/areas:** [CMakePresets.json](../CMakePresets.json#L9-L66), [.github/workflows/build-and-test.yml](../.github/workflows/build-and-test.yml#L30-L49), [.github/workflows/ui-snapshots.yml](../.github/workflows/ui-snapshots.yml#L16-L48), [scripts/export_ui_snapshots.ps1](../scripts/export_ui_snapshots.ps1#L119-L147), [README.md](../README.md#L107-L136)
- **Problem:** `windows-2022` has VS 2022, while `default` requires the VS 2026 generator. The latest public build fails in 17 seconds and the UI workflow has never run.
- **Recommendation:** Add `ci-windows-ninja` (preferred) or `ci-vs2022` with ASIO off, retain VS 2026 as a developer preset, and point both workflows/scripts at the CI preset. Print tool versions. Do not silently make VS 2026 a requirement while README says VS 2022.
- **Acceptance:** A clean GitHub-hosted `windows-2022` run configures successfully, and a clean documented VS 2022 setup follows the same supported path.

### QW-2 — Re-establish UI golden truth and release metadata

- **Category:** Reliability / UX
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Files/areas:** [tests/ui-snapshot-baselines/current](../tests/ui-snapshot-baselines/current), [tests/CMakeLists.txt](../tests/CMakeLists.txt#L372-L385), [scripts/compare_ui_snapshots.ps1](../scripts/compare_ui_snapshots.ps1), [README.md](../README.md#L37-L73), [docs/USER_GUIDE.md](USER_GUIDE.md#L113-L127)
- **Problem:** All 19 snapshots currently fail. Baselines and the guide say 128 presets while the shipped bank/current harness has 64.
- **Recommendation:** Review each actual/baseline/diff visually; correct unintended changes, then update only approved goldens. Correct the guide and regenerate public screenshots. Add a release assertion that the documented count equals the preset manifest/directory count.
- **Acceptance:** Zero unexpected diffs on two clean runs; docs and screenshots show 64; a future count mismatch fails an automated check.

### QW-3 — Remove the duplicate, path-named CTest registration

- **Category:** DX
- **Perspective:** Engineering
- **Effort:** XS
- **Impact:** Medium
- **Files/areas:** [tests/CMakeLists.txt](../tests/CMakeLists.txt#L76-L94)
- **Problem:** `c:/projects/other/audiocity` reruns `audiocity_offline_tests`, leaking a developer path and doubling that test in full CTest.
- **Recommendation:** Delete lines 84–88 and assert expected test names in a configure/CTest smoke check.
- **Acceptance:** `ctest -N` lists four uniquely named tests; offline tests run once.

### QW-4 — Compile the products users install on every relevant PR

- **Category:** Reliability
- **Perspective:** Engineering
- **Effort:** S
- **Impact:** High
- **Files/areas:** [.github/workflows/build-and-test.yml](../.github/workflows/build-and-test.yml#L3-L49), [CMakeLists.txt](../CMakeLists.txt#L61-L76), [installer](../installer), [scripts/build_release.ps1](../scripts/build_release.ps1)
- **Problem:** CI compiles test executables but not Standalone/VST3. Installer-only changes do not trigger it.
- **Recommendation:** Add `installer/**`, release scripts, and dependency docs to path filters; build `Audiocity_All` in Release with ASIO off; retain fast non-UI tests as a separate step. At minimum inspect expected executable/VST3/resource paths.
- **Acceptance:** A wrapper compile/link/resource-copy regression fails CI, and an installer-only PR receives the packaging check.

### QW-5 — Make fixed program limits visible before publish

- **Category:** Reliability
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Files/areas:** [src/engine/ProgramSnapshot.h](../src/engine/ProgramSnapshot.h#L11-L15), [src/engine/ProgramSnapshot.h](../src/engine/ProgramSnapshot.h#L93-L105), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L1175-L1226), import diagnostics in [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp)
- **Problem:** Programs above 256 assets, 128 groups, or 512 zones silently lose renderable content.
- **Recommendation:** Check `truncated` before publishing; reject with exact counts/limits, or require an explicit user-approved partial load. Keep the previous playable program active on rejection.
- **Acceptance:** Boundary tests at limit and limit+1 prove no silent partial load; UI and copyable log name the exceeded dimension.

### QW-6 — Use one authoritative supported-format registry

- **Category:** UX / DX
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Files/areas:** [src/plugin/LibraryFileIndex.cpp](../src/plugin/LibraryFileIndex.cpp#L5-L18), [src/plugin/ImportedProgramState.cpp](../src/plugin/ImportedProgramState.cpp), [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L11317-L11407), [README.md](../README.md#L96-L101)
- **Problem:** The library recognizes many formats that the main chooser and drag/drop reject.
- **Recommendation:** Define descriptors with extension, badge, description, availability predicate, import format, chooser group, and loss/legacy caveat. Generate browser checks, wildcard, drag/drop interest, UI copy, and tests from it.
- **Acceptance:** Every advertised loadable extension works consistently from chooser, drag/drop, and library; unavailable REX/NCW cases show actionable reasons.

### QW-7 — Debounce preload changes and avoid no-op rebuilds

- **Category:** Performance / UX
- **Perspective:** Both
- **Effort:** S
- **Impact:** High
- **Files/areas:** [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L5085-L5089), [src/plugin/PluginProcessor.h](../src/plugin/PluginProcessor.h#L253-L261), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L1298-L1315)
- **Problem:** Each dial tick rebuilds all loaded segment data on the message thread, even if quantization yields the current value.
- **Recommendation:** Return immediately on no-op values; show the pending value while dragging and apply once on drag end or a 250–400 ms debounce. Label the operation as applying while MP-3/MP-4 background infrastructure is not yet available.
- **Acceptance:** A full dial drag causes at most one rebuild after release; unchanged values cause zero rebuilds; playback continues on the previous snapshot.

## 2. High-impact medium projects — one to two weeks each

### MP-1 — Make the audio thread the sole owner of mutable engine/DSP state

- **Category:** Reliability / Performance
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** Critical
- **Files/areas:** [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L5090-L5117), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L938-L1057), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1136-L1195), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1217-L1266), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L2439-L2555), [docs/02-real-time-rules.md](02-real-time-rules.md)
- **Problem:** Message-thread setters and the audio thread concurrently mutate/read plain engine settings and live voices.
- **Approach:**
  1. Inventory every `processor_.set/get*` call by originating thread and classify control, structural publish, or display state.
  2. Make UI control setters update APVTS with host gestures or push a bounded command; remove their direct `engine_` writes.
  3. At block start, read all atomic values into a POD `EngineControlSnapshot`; compare with `lastAppliedControls_`; apply changed groups on the audio thread only.
  4. Route structural operations (program/sample/segment snapshots) through immutable publication and define one serialized writer.
  5. Make UI reads use APVTS or audio-to-UI snapshots, never live mutable engine fields.
- **Risks/open questions:** host state callbacks may occur on non-message threads; ADSR updates on active voices need defined smoothing; structural publishes must remain allocation-free for the reader.
- **Acceptance/success metrics:** thread-ownership document matches code; high-rate automation + UI drag + MIDI stress runs for an hour without race symptoms; no locks/allocations/I/O in `processBlock`; unchanged-block parameter apply cost drops materially in the profiling harness.

### MP-2 — Replace detached workers with owned, cancellable jobs

- **Category:** Reliability / UX
- **Perspective:** Both
- **Effort:** M
- **Impact:** Critical
- **Files/areas:** [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8075-L8131), [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8367-L8487), [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L5365-L5371), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L783-L786), importer entry points under [src/engine](../src/engine)
- **Problem:** An import thread owns a raw processor pointer after detach; cancellation only invalidates publish and permits worker accumulation.
- **Approach:** Add a processor/service-owned bounded executor or `std::jthread`; jobs own immutable inputs and report progress through a lifetime-safe queue. Use `stop_token`/shared cancellation probes in file traversal, container parsing, audio decoding, and peak generation. Join all workers before processor destruction. Keep one active import and one scan generation.
- **Risks/open questions:** JUCE decoders may not be interruptible during one large read; host teardown must not block indefinitely; UI callbacks must never retain components.
- **Acceptance/success metrics:** close/unload during a blocked import is safe; rapid import replacement never runs more than the configured job limit; cancel reaches terminal state within a documented bound; no detached threads remain in product code.

### MP-3 — Build a scalable, persistent library index with lazy peaks

- **Category:** Performance / UX
- **Perspective:** Both
- **Effort:** M
- **Impact:** High
- **Files/areas:** [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L704-L775), [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8336-L8475), [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8691-L8768), [src/plugin/PeakPreviewCache.cpp](../src/plugin/PeakPreviewCache.cpp#L53-L137), [src/plugin/LibraryFileIndex.cpp](../src/plugin/LibraryFileIndex.cpp#L51-L64)
- **Problem:** A scan reads all sample bytes for peaks, posts every 24 entries, repeatedly sorts the full list, invalidates same-size edits, and remembers only one root.
- **Approach:** Separate fast index records from rich preview records. Persist per-root canonical path/size/mtime/version records atomically. Publish coalesced deltas at 100–250 ms intervals and sort incrementally or at quiescence. Queue peaks/metadata only for visible/nearby rows with LRU memory/disk budgets. Integrate MP-2 cancellation.
- **Risks/open questions:** filesystem timestamp granularity, network/removable drives, symlink cycles, rename detection, cache migration.
- **Acceptance/success metrics:** synthetic 50k-file scan shows first results <500 ms on SSD, search interaction <50 ms, bounded worker/message-queue counts, bounded memory/cache size, and zero stale same-size-edit preview in tests.

### MP-4 — Allocate capture/preview memory lazily and replace raw-float XML state

- **Category:** Performance / Reliability
- **Perspective:** Both
- **Effort:** M
- **Impact:** High
- **Files/areas:** [src/plugin/PluginProcessor.h](../src/plugin/PluginProcessor.h#L546-L556), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1327-L1366), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1515-L1707), capture methods in [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L4180-L4550)
- **Problem:** Every instance reserves 27.5 MiB for unused arrays; committed capture/embedded audio is duplicated and XML-serialized as raw floats.
- **Approach:** Allocate a fixed-size chunk/ring pool only when capture is armed, prepared off the audio thread; publish its storage to the audio thread without resizing. Release raw capture buffers after commit. Introduce a versioned compressed binary asset chunk with checksums and hard size limits; preserve old-state reading. Track serialized state size and warn before oversized embeds.
- **Risks/open questions:** host expectations for synchronous state serialization, compression latency, backward compatibility, stereo capture policy, shared asset deduplication.
- **Acceptance/success metrics:** idle processor overhead for these features <1 MiB; 20 idle instances no longer consume >500 MiB; maximum capture round-trips; state is smaller than raw float XML; old sessions still restore.

### MP-5 — Remove silent program capacity loss with scalable immutable snapshots

- **Category:** Reliability / Feature
- **Perspective:** Both
- **Effort:** M
- **Impact:** High
- **Files/areas:** [src/engine/ProgramSnapshot.h](../src/engine/ProgramSnapshot.h#L11-L163), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L1175-L1277), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L2738-L2904), [src/plugin/ImportedProgramStore.cpp](../src/plugin/ImportedProgramStore.cpp)
- **Problem:** Fixed arrays truncate legitimate large programs, while unbounded dynamic allocation cannot occur on the audio thread.
- **Approach:** Prepare immutable vectors/packed arrays off-thread with validated maximum product budgets, publish one shared snapshot, and keep render-time scratch bounded/preallocated to the published zone/group counts or a documented voice-relevant cap. Return structured capacity diagnostics rather than a boolean flag. Preserve the QW-5 safe rejection until migration completes.
- **Risks/open questions:** worst-case zone matching complexity, snapshot memory ceiling, round-robin scratch sizing, malicious imports.
- **Acceptance/success metrics:** >512-zone real-world fixture plays all mapped zones within declared budgets; explicit rejection above security limits; no audio-thread allocations; mapping UI and renderer report the same counts.

### MP-6 — Linearize pending-event dispatch and make overflow note-safe

- **Category:** Performance / Reliability
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** High
- **Files/areas:** [src/engine/EngineCore.h](../src/engine/EngineCore.h#L613-L615), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L1535-L1559), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L2570-L2633), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L2907-L2916), MIDI ingest around [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L2370-L2420)
- **Problem:** Sorting plus per-offset full scans is quadratic; overflow can discard release/control safety events invisibly.
- **Approach:** Normalize input into one already-ordered bounded array, stable-order equal offsets, and advance a single cursor through render segments. Reserve capacity or priority lanes for note-off/all-notes-off. Count drops by type and initiate bounded panic recovery if release events cannot be retained.
- **Risks/open questions:** preserving exact same-offset semantics and deterministic goldens; MIDI messages can arrive from UI and host paths.
- **Acceptance/success metrics:** 1,024-event cost grows linearly after ingest; current offline output remains bit-stable where ordering is defined; overflow test cannot leave a held voice; counters are visible outside the RT thread.

### MP-7 — Consolidate test builds and add hostile-input/runtime hardening

- **Category:** DX / Security / Reliability
- **Perspective:** Engineering
- **Effort:** M
- **Impact:** High
- **Files/areas:** [tests/CMakeLists.txt](../tests/CMakeLists.txt#L1-L70), [tests/CMakeLists.txt](../tests/CMakeLists.txt#L188-L370), [tests/OfflineRenderTests.cpp](../tests/OfflineRenderTests.cpp), importers under [src/engine](../src/engine), [.github/workflows](../.github/workflows)
- **Problem:** Production sources compile into several executables, one 14k-line hand-registered suite limits isolation, and no sanitizer/fuzzer/coverage/plugin-host lane exists.
- **Approach:** Create shared object/static test-support libraries with explicit compile-definition variants. Split offline tests by engine/import/state/preset without requiring a framework migration. Add corpus fuzz targets or libFuzzer entry points for each importer and run ASan/UBSan on Clang Linux/macOS where feasible; keep Windows behavior tests. Add coverage and render-deadline trends, plus pluginval or equivalent VST3 smoke validation.
- **Risks/open questions:** JUCE platform differences, REX exclusion on non-Windows, reproducible UI fonts, CI duration.
- **Acceptance/success metrics:** materially lower clean/incremental CI time, individually filterable suites, sanitizer-clean corpus, coverage baseline reported (not necessarily gated initially), built VST3 passes host validation.

### MP-8 — Add a guided missing-asset resolver

- **Category:** Reliability / UX
- **Perspective:** Both
- **Effort:** M
- **Impact:** High
- **Files/areas:** [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1327-L1340), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1529-L1635), [src/plugin/ImportedProgramState.cpp](../src/plugin/ImportedProgramState.cpp), [src/plugin/ImportedProgramStore.cpp](../src/plugin/ImportedProgramStore.cpp), mapping UI in [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp)
- **Problem:** Imported sessions fail when original absolute paths move and offer no guided recovery.
- **Approach:** Store a versioned manifest with original relative path, file name, size, mtime, optional fast hash, format, and asset references. On miss, search project/session folder and known roots, then show one resolver for manual folder selection and persistent relinking. Never scan entire disks automatically. Keep the old program until resolution succeeds.
- **Risks/open questions:** host project path is not always available; duplicate filenames; privacy of persisted absolute paths; scan bounds.
- **Acceptance/success metrics:** moving a library folder and reopening can be repaired once for all matching assets; collaborators can choose a root; failure is explicit and copyable; no silent partial instrument.

## 3. Strategic initiatives — multi-week

### SI-1 — Cross-platform and wrapper expansion

- **Category:** Feature / Growth / Reliability
- **Perspective:** Both
- **Effort:** XL
- **Impact:** High
- **Files/areas:** [CMakeLists.txt](../CMakeLists.txt#L61-L99), [CMakeLists.txt](../CMakeLists.txt#L152-L188), [CMakePresets.json](../CMakePresets.json), [third_party/REXSDK_Win_1.9.2](../third_party/REXSDK_Win_1.9.2), [installer](../installer), `.github/workflows`
- **Problem statement:** Windows-only VST3/Standalone excludes macOS AU/Apple Silicon and limits market reach; platform-specific REX/ASIO assumptions are mixed into common targets.
- **Proposed approach:** Stabilize Windows first. Introduce platform capability targets; add macOS universal VST3/AU builds and tests; sign/notarize artifacts; abstract platform paths and external conversion. Evaluate CLAP/Linux after demand validation rather than coupling it to the first milestone. Keep REX optional where licensing/runtime support is unavailable.
- **Key risks/open questions:** Apple certificates/notarization, plugin IDs/state compatibility, filesystem case sensitivity, font/golden stability, REX SDK licensing/support, support burden.
- **Success metrics:** AU and VST3 pass offline plus host validation on Intel/Apple Silicon; signed/notarized install succeeds on a clean Mac; existing Windows preset/state compatibility remains; platform crash/support rate is measured.

### SI-2 — Portable instrument packaging and asset lifecycle

- **Category:** Feature / Reliability / UX
- **Perspective:** Both
- **Effort:** L
- **Impact:** High
- **Files/areas:** state code in [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1327-L1707), [src/plugin/ImportedProgramState.cpp](../src/plugin/ImportedProgramState.cpp), [src/engine/SfzExporter.cpp](../src/engine/SfzExporter.cpp), [src/engine/DecentSamplerExporter.cpp](../src/engine/DecentSamplerExporter.cpp), [docs/16-library-authoring-plan.md](16-library-authoring-plan.md)
- **Problem statement:** Reference-only imported programs do not travel, while raw-float embedded samples can make host state huge. Audiocity cannot yet be a dependable library-authoring/interchange hub.
- **Proposed approach:** Build on MP-4/MP-8 with one manifest and three explicit policies: Reference, Collect beside project, or Embed package. Deduplicate assets by hash, validate/copy atomically, include license/attribution metadata, preflight missing/lossy content, and support cleanup of unreferenced copies. Use the same package service for SFZ/DecentSampler export and future native libraries.
- **Key risks/open questions:** package format/versioning, copyright/licensing UX, very large libraries, host state limits, path traversal, hashes on network drives.
- **Success metrics:** a collected instrument opens on a clean second machine with no manual path edits; package size/deduplication is predictable; interrupted writes leave source and prior package intact; round-trip diagnostics have zero silent loss.

### SI-3 — Modular editor/processor architecture

- **Category:** DX / Reliability
- **Perspective:** Engineering
- **Effort:** L
- **Impact:** High
- **Files/areas:** [src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp), [src/plugin/PluginEditor.h](../src/plugin/PluginEditor.h), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp), extracted `Plugin*Page` files, library/import/preset services
- **Problem statement:** UI composition, jobs, import routing, state synchronization, and domain logic remain concentrated in 12k/5k-line units, obscuring thread ownership and slowing every change.
- **Proposed approach:** After goldens and MP-1/MP-2, establish feature view-models/services for Sample, Library, Mapping, Player, Generate, Capture, and About. Keep processor host/DSP responsibilities narrow. Move job orchestration, format registry, asset service, and library index behind testable non-Component APIs. Extract in behavior-preserving slices.
- **Key risks/open questions:** large refactor can hide behavior drift; JUCE component lifetimes; avoiding duplicate state sources.
- **Success metrics:** no UI/processor implementation file above an agreed threshold (for example 2.5k lines); single ownership source for each state; feature-only edits compile a narrow target; snapshot/offline/host tests stay green each slice.

### SI-4 — Evidence-led expressive sampler evolution

- **Category:** Feature
- **Perspective:** Product
- **Effort:** XL
- **Impact:** Medium
- **Files/areas:** [docs/06-roadmap.md](06-roadmap.md#L165-L185), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp), [src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L745-L747), program model/import/export layers
- **Problem statement:** Audiocity lacks per-note MPE, multiple output buses, and independent time/pitch stretching—capabilities valuable to advanced instrument builders—but implementing all would fragment the engine and UI.
- **Proposed approach:** Interview users/library authors and instrument current interactions before committing. Prototype the leading use case behind capability flags: likely multi-output for drum/mic routing, MPE for expressive instruments, or tempo-aware stretch for loops. Define state/import/export behavior and CPU budgets before UI work. Ship one coherent wedge with presets/examples.
- **Key risks/open questions:** DSP licensing/quality, host interoperability, per-voice CPU, automation/state migration, import/export semantic mismatch, unclear demand.
- **Success metrics:** validated target cohort and workflow; explicit CPU/latency budget; host matrix; adoption and repeat use of the first shipped capability; no regression to baseline polyphony.

## 4. Debt retirement candidates

| Candidate | Replace/remove | Why now |
| --- | --- | --- |
| Detached `std::thread(...).detach()` jobs | Owned bounded executor / `std::jthread` under MP-2 | Removes UAF and worker-storm class entirely. |
| Direct UI-to-`EngineCore` mutations | APVTS/command snapshot under MP-1 | Eliminates the main undefined-behavior path. |
| Per-block unconditional setter graph | Dirty, grouped control snapshot | Reduces RT work and simplifies ownership. |
| Duplicate format allowlists | QW-6 descriptor registry | Prevents user-facing capability drift. |
| Duplicate CTest registration | Delete it (QW-3) | Pure error with no value. |
| Recompiled UI shared-source lists | Shared test-support targets (MP-7) | Reduces build time and definition drift. |
| Single-root monolithic XML peak cache | Versioned per-root index (MP-3) | Fixes stale data and large-library behavior. |
| Raw-float audio in XML state | Versioned bounded binary asset codec (MP-4/SI-2) | Controls project size and memory copies. |
| Hand-maintained preset counts/screenshots | Manifest-driven release validation (QW-2) | Removes recurring docs/product drift. |
| Oversized editor/test translation units | Feature modules and split suites (SI-3/MP-7) | Improves change isolation; do only behind green tests. |

## 5. Dependency upgrade path

1. **Repair the toolchain/preset contract first.** Use a hosted-runner-supported generator, lock CI to an explicit OS image, and record compiler/CMake versions. This is prerequisite infrastructure, not a product dependency upgrade.
2. **Upgrade GitHub Actions and pin commits.** Move `actions/checkout@v4` to the current v6 line and `actions/upload-artifact@v4` to the current v7 line after checking hosted/self-hosted runner requirements; then pin reviewed full SHAs. The reviewed releases were [checkout 6.0.2](https://github.com/actions/checkout/releases/tag/v6.0.2) and [upload-artifact 7.0.1](https://github.com/actions/upload-artifact/releases/tag/v7.0.1).
3. **Upgrade JUCE 8.0.4 → 8.0.13 in an isolated PR.** Review `BREAKING_CHANGES.md`, pin the release commit, resolve CMake-policy warnings, and run offline, UI, Standalone/VST3, state-migration, installer, and host validation. JUCE 8.0.13 includes relevant compile/paint/Windows rendering improvements ([release](https://github.com/juce-framework/JUCE/releases/tag/8.0.13)).
4. **Keep CMake minimum 3.22 unless a used feature justifies raising it.** Test at both the declared minimum and CI version; fix `/Zi` versus `/Z7` duplication rather than masking warning output.
5. **Isolate REX and ASIO as optional capability targets.** Record SDK versions/licenses/checksums and ensure normal CI does not require proprietary inputs. Re-evaluate REX only against actual format usage.
6. **Add new dependencies only with a workstream.** A fuzz engine, plugin validator, stretch library, or CLAP adapter should enter through MP-7/SI-1/SI-4 with ownership, license, binary-size, and update policy. Do not add Catch2 merely to reorganize existing tests; splitting and build consolidation can precede framework migration.

## Prompt Handoff

Each prompt is intentionally scoped to one review item. Implement in dependency order and preserve unrelated user changes.

### QW-1 prompt — CI/toolchain

> In Audiocity, repair the CI generator mismatch without changing product behavior. `CMakePresets.json:9-66` requires Visual Studio 18 2026, but `.github/workflows/build-and-test.yml:30-49` and `.github/workflows/ui-snapshots.yml:16-48` run on `windows-2022`; `scripts/export_ui_snapshots.ps1:119-147` invokes the same default preset. Add a hosted-runner-compatible Ninja or VS 2022 CI configure/build preset with ASIO disabled, retain VS 2026 developer presets, switch both workflows/scripts to the CI preset, and align `README.md:107-136`. Print CMake/compiler/generator versions. Verify clean configure, selected builds, and CTest.

### QW-2 prompt — UI baseline truth

> Re-establish Audiocity's UI snapshot baseline after manual review. Build/run `audiocity_ui_snapshot_harness` from `tests/CMakeLists.txt:372-381`, compare every actual/baseline/diff using `scripts/compare_ui_snapshots.ps1`, correct unintended UI regressions, and update only intentional images under `tests/ui-snapshot-baselines/current`. Align `README.md:37-73` and `docs/USER_GUIDE.md:113-127` with the shipped 64 presets. Add a deterministic preset-count metadata/doc check. Demonstrate two clean zero-diff runs.

### QW-3 prompt — duplicate test

> Remove the accidental second CTest registration named `c:/projects/other/audiocity` at `tests/CMakeLists.txt:84-88`. Keep `audiocity_offline_tests` at lines 78-82, run `ctest -N`, and verify the offline executable runs exactly once in a full suite.

### QW-4 prompt — shipped targets in CI

> Extend `.github/workflows/build-and-test.yml:3-49` so relevant PRs compile the actual `Audiocity_All` Release targets declared at `CMakeLists.txt:61-76`, with ASIO off on hosted CI. Include `installer/**` and release/packaging scripts in path filters, run packaging and non-UI tests, and verify expected Standalone, VST3, factory-preset resource, and installer-input paths. Keep the fast test stage independently diagnosable.

### QW-5 prompt — capacity guard

> Prevent silent program truncation. `src/engine/ProgramSnapshot.h:11-15,93-105` caps 256 assets/128 groups/512 zones, while `src/engine/EngineCore.cpp:1175-1226` publishes without checking `truncated`. Add structured pre-publish validation that preserves the previous playable program and reports actual counts and limits. Add boundary tests for limit and limit+1 for every dimension; do not partially load without explicit policy.

### QW-6 prompt — format registry

> Create one authoritative import-format descriptor registry from the capabilities currently duplicated in `src/plugin/LibraryFileIndex.cpp:5-18`, `src/plugin/ImportedProgramState.cpp`, and `src/plugin/PluginEditor.cpp:11317-11407`. Generate chooser wildcards, drag/drop acceptance, browser support, badges/descriptions, availability predicates, and tests from it. Ensure every format advertised at `README.md:96-101` is consistent across entry points, with actionable REX/NCW unavailability messages.

### QW-7 prompt — preload debounce

> Make preload adjustment safe and cheap. `src/plugin/PluginEditor.cpp:5085-5089` calls `setPreloadSamples` for every dial tick; `src/engine/EngineCore.cpp:1298-1315` rebuilds loaded segments. Skip unchanged values, display pending values during drag, and apply once on drag end or after a short debounce. Add a test around `getSegmentRebuildCount()` proving a drag causes at most one rebuild and no-op updates cause none.

### MP-1 prompt — RT control plane

> Eliminate cross-thread mutation of `EngineCore`. Audit UI setters beginning at `src/plugin/PluginEditor.cpp:5090-5117` and processor setters such as `src/plugin/PluginProcessor.cpp:1217-1266,3789-3904`. Replace direct message-thread engine writes with APVTS host-notified parameters or bounded structural commands. At `PluginProcessor.cpp:938-1057,1136-1195`, load a POD control snapshot once per block, diff it against the last applied state, and apply changed groups only on the audio thread. Make UI reads use APVTS/audio-to-UI snapshots. Preserve `docs/02-real-time-rules.md`; add high-rate automation/UI/MIDI stress coverage and profile unchanged blocks.

### MP-2 prompt — worker lifetime

> Replace detached import/scan threads at `src/plugin/PluginEditor.cpp:8107-8131,8367-8487` with an owned bounded executor or `std::jthread` service. Never capture a raw processor/component across an unowned thread. Add cooperative stop checks to import/decode/traversal/peak loops, one active import/scan generation, progress states, and destructor join/cancel before `PluginProcessor.cpp:783-786` completes. Test processor destruction during a blocked import and rapid cancel/replace; prove no worker outlives the processor.

### MP-3 prompt — library scale

> Redesign library indexing around cheap persistent records and lazy previews. `PluginEditor.cpp:704-775` currently reads every sample end-to-end; `8367-8462` posts every 24 items; `8691-8768` re-filters/sorts all entries. `PeakPreviewCache.cpp:66-126` stores one root and `PluginEditor.cpp:8431-8438` validates only size. Persist per-root path/size/mtime/version records, coalesce UI deltas, lazily peak visible rows through the MP-2 worker pool, and add cache/LRU bounds. Benchmark/test 50k synthetic files, same-size edits, cancellation, root switching, and corrupted cache recovery.

### MP-4 prompt — memory/state codec

> Reduce per-instance idle memory and host-state bloat. Replace unconditional arrays at `src/plugin/PluginProcessor.h:546-556` with capture/preview storage allocated and prepared only when needed, without audio-thread allocation. Release capture working storage after commit. Replace raw-float XML properties written at `PluginProcessor.cpp:1327-1366,1515-1516` with a versioned bounded compressed binary asset codec while retaining old-state reads at `1519-1707`. Add state size limits/checksums and tests for max-duration capture, old sessions, corrupt/truncated data, and 20 idle instances.

### MP-5 prompt — scalable program snapshots

> Replace fixed silent truncation in `src/engine/ProgramSnapshot.h:11-163` with an immutable packed snapshot prepared off-thread and published through the existing snapshot cell in `src/engine/EngineCore.cpp:1175-1277`. Retain explicit product security limits, but report rejection rather than truncation. Rework render scratch/matching at `EngineCore.cpp:2738-2904` so no audio-thread allocation is introduced. Add >512-zone real-world and adversarial fixtures and assert mapping/render counts agree.

### MP-6 prompt — event scheduler

> Make pending MIDI dispatch linear and release-safe. Replace insertion sort/full rescans at `src/engine/EngineCore.cpp:1535-1559,2617-2633,2907-2916` with one stable ordered buffer and monotonic cursor. Redesign overflow at `2570-2614` so note-off/all-notes-off cannot be silently displaced; add dropped-event counters and bounded panic recovery. Preserve defined equal-offset ordering and bit-stable output. Add 1,024-event timing, overflow, stuck-note, and determinism tests.

### MP-7 prompt — test/build hardening

> Consolidate Audiocity tests without changing behavior. Turn repeated production source lists in `tests/CMakeLists.txt:1-70,188-370` into shared object/static test-support targets with explicit compile-definition variants. Split `tests/OfflineRenderTests.cpp` into filterable subsystem suites while preserving coverage. Add importer corpus/fuzz entry points, an ASan/UBSan lane on a supported platform, coverage reporting, render deadline trends, and VST3 host/plugin validation. Keep proprietary REX/ASIO optional and measure CI time before/after.

### MP-8 prompt — asset resolver

> Add guided imported-program asset recovery around `src/plugin/PluginProcessor.cpp:1327-1340,1529-1635` and `src/plugin/ImportedProgramState.cpp`. Version state with relative path, filename, size, mtime, optional hash, format, and asset manifest. Resolve within bounded project/known roots, then offer one manual root selection that repairs all unambiguous matches and persists relinks. Preserve the old program until success; never scan whole disks or silently accept partial programs. Test moved folders, duplicates, missing subsets, collaboration paths, and old states.

### SI-1 prompt — macOS/AU

> Plan and implement Audiocity's first cross-platform milestone after MP-1/MP-2 and green CI. Isolate Windows-only ASIO/REX definitions at `CMakeLists.txt:83-99,152-188`; add macOS universal VST3 and AU targets beside `CMakeLists.txt:61-76`, macOS presets/CI, signing/notarization, installer packaging, and host validation. Audit paths/case sensitivity/external converters and preserve plugin identity/state compatibility. Defer CLAP/Linux until the macOS milestone is measured and stable.

### SI-2 prompt — portable packages

> Build a portable asset/package service using state paths at `src/plugin/PluginProcessor.cpp:1327-1707`, `src/plugin/ImportedProgramState.cpp`, `src/engine/SfzExporter.cpp`, and `src/engine/DecentSamplerExporter.cpp`. Offer explicit Reference, Collect, and Embed policies; use a versioned manifest, hashes/deduplication, atomic copy/write, licensing metadata, size preflight, missing/lossy diagnostics, and cleanup. Reuse the same resolver/codec from MP-4/MP-8. Validate on a clean second machine and under interrupted writes.

### SI-3 prompt — architecture decomposition

> Decompose `src/plugin/PluginEditor.cpp` and `PluginProcessor.cpp` in behavior-preserving slices after UI goldens and thread ownership are stable. Create tested services/view-models for import jobs, format registry, library index, asset lifecycle, presets, mapping, and per-tab state; finish extracting Sample/Library/Mapping/Player components. Keep the editor a composition layer and processor a host/DSP boundary. Require green offline/UI/host tests per slice and track incremental compile time and file-size goals.

### SI-4 prompt — expressive roadmap

> Run an evidence-led discovery/prototype for one expressive sampler capability acknowledged around `docs/06-roadmap.md:165-185`. Compare multi-output (current bus is one stereo pair at `src/plugin/PluginProcessor.cpp:745-747`), per-note MPE, and independent time/pitch stretching. Interview target users, define one end-to-end workflow, state/import/export semantics, host matrix, and CPU/latency budget, then prototype behind a capability flag. Recommend one wedge with measured user value; do not implement all three simultaneously.

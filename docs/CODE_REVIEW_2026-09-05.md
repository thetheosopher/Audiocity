# Audiocity — Deep Code Review

```text
Review conducted: 2026-09-05
Reviewer: Codex (OpenAI)
Perspectives: Software Optimization Engineering + Product Management
Codebase: Audiocity 1.3.2.0 (JUCE/C++20 hybrid sampler — Standalone + VST3)
```

> Reviewer identity note: the supplied template named a different model. The identity above records the reviewer that actually performed this review.

Severity legend: 🔴 Critical · 🟠 High · 🟡 Medium · 🔵 Low

## Executive finding

Audiocity has unusually broad sampler-format support, a serious offline regression suite, useful import diagnostics, and explicit real-time design rules. The principal risk is that the implementation no longer consistently follows those rules: message-thread controls mutate `EngineCore` and its live voices while the audio thread can render the same objects. A second lifetime bug lets a detached import worker retain a raw processor pointer after the editor or processor is destroyed. These are release-blocking correctness issues.

The next tier of work is delivery integrity and scale. The public CI run for release 1.3.2.0 is failing, the UI golden suite fails all 19 images locally, programs beyond fixed snapshot limits are silently truncated, large-library scans read every audio file end-to-end, and each plugin instance reserves about 27.5 MiB for capture/preview arrays before those features are used.

## Phase 1 — Project context

### Purpose, domain, and users

Audiocity is a Windows-focused software sampler for producers, composers, sound designers, and sample-library authors. It runs as a standalone instrument and VST3 plugin, loads direct audio and a broad set of third-party multisample formats, provides slicing/mapping/modulation/effects, and exports SFZ and DecentSampler instruments. The repository ships 64 embedded factory presets and tools for preset authoring, auditioning, profiling, installers, and portable releases ([README.md](../README.md#L5-L35), [README.md](../README.md#L76-L100)).

### Technology and delivery surface

| Area | Current implementation |
| --- | --- |
| Language/runtime | C++20; small C component for the REX SDK; PowerShell release/test tooling |
| Framework | JUCE 8.0.4, obtained through `FetchContent` when not vendored |
| Build | CMake 3.22+, Visual Studio 18 2026 presets, MSVC, optional ASIO |
| Products | Windows x64 Standalone and VST3 (`FORMATS VST3 Standalone`) |
| Data | Local files, JUCE `ValueTree`/XML state, ZIP/GZIP containers, in-memory immutable program/audio snapshots; no database |
| Network/API | No product network client (`JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`); GitHub/JUCE access is build-time only |
| CI/release | Two GitHub Actions workflows, Inno Setup installer, portable ZIP script |
| Tests | Offline engine/import/preset suite, packaging checks, runtime preset smoke test, UI screenshot goldens, profiling harness |

Key build definitions are in [CMakeLists.txt](../CMakeLists.txt#L1-L76), [CMakePresets.json](../CMakePresets.json#L1-L74), and [.github/workflows](../.github/workflows). JUCE is fetched from a Git tag at [CMakeLists.txt](../CMakeLists.txt#L47-L56).

### Architecture and data flow

1. `AudiocityAudioProcessor` owns host-facing state, APVTS parameters, capture/preview buffers, library metadata, and `EngineCore` ([src/plugin/PluginProcessor.h](../src/plugin/PluginProcessor.h)).
2. `AudiocityAudioProcessorEditor` and extracted page components implement the seven-tab UI. The editor remains a 12,189-line coordinator ([src/plugin/PluginEditor.cpp](../src/plugin/PluginEditor.cpp)).
3. Importers parse external instruments into `Program` plus decoded audio. The message thread publishes fixed-capacity immutable `ProgramSnapshot` and `ProgramAudioSnapshot` objects for audio-thread reads ([src/engine/ProgramSnapshot.h](../src/engine/ProgramSnapshot.h#L11-L163), [src/engine/EngineCore.cpp](../src/engine/EngineCore.cpp#L1175-L1226)).
4. `processBlock()` consumes APVTS values, UI MIDI, host MIDI, and immutable sample/program snapshots before rendering through a fixed 64-voice pool ([src/plugin/PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1128-L1202), [src/engine/VoicePool.h](../src/engine/VoicePool.h#L10-L52)).
5. Library indexing and instrument preparation use ad-hoc detached `std::thread`s; disk-stream priming has a processor-owned worker.

The documented contract is sound: the UI should publish through a lock-free queue or double-buffered state, and the audio thread should read immutable state once per block ([docs/01-architecture.md](01-architecture.md#L7-L14), [docs/02-real-time-rules.md](02-real-time-rules.md#L1-L13)). Finding CONC-01 shows where the implementation departs from it.

### Repository shape and maturity signals

- Existing artifacts live in `docs/`, including architecture, real-time rules, test specifications, product plans, the user guide, and the prior 2026-05-31 review. This review follows that convention.
- The reviewed source/docs/test surface contains 117 relevant files and about 61,859 lines; about 58,383 are C/C++ headers or sources. The largest files are `tests/OfflineRenderTests.cpp` (14,402), `src/plugin/PluginEditor.cpp` (12,189), `PluginProcessor.cpp` (5,070), and `EngineCore.cpp` (4,126).
- The checkout contains one visible commit (`42efea0971ce6af30a5b5fb9a415472ace01d0db`, release 1.3.2.0, 2026-07-30), so repository age and commit-frequency conclusions would be unreliable.
- There are few TODO markers; incomplete behavior is expressed in roadmap documents and exporter/importer diagnostics rather than abandoned stubs.

### Verification performed

| Check | Result |
| --- | --- |
| Clean isolated CMake configure (`build/review`, Ninja, CMake 4.2.3) | Passed after JUCE was fetched. JUCE 8.0.4 emitted a CMake CMP0175 compatibility warning. |
| Compile selected test targets with VS 2026/MSVC 19.51 | `audiocity_offline_tests`, `audiocity_preset_runtime_smoke`, and `audiocity_ui_snapshot_harness` built successfully. Debug builds emitted `/Zi` overridden by `/Z7` warnings. |
| Non-UI tests | 3/3 passed: offline suite 3.15 s, packaging 0.20 s, preset runtime smoke 0.27 s. |
| Full CTest | 4/5 passed. The UI snapshot test failed after 46.05 s; the suite also ran the offline test twice under two names. |
| UI goldens | All 19 images mismatched. Ratios range from 0.00475 to 0.44783; several show intentional-looking state/layout drift, so the review does not assert that the new rendering itself is defective. The regression gate is nevertheless red and cannot protect UI changes. |
| Hosted CI | The latest public Build and Test run failed in 17 seconds. The separate UI workflow has no runs. See [release 1.3.2.0 CI run](https://github.com/thetheosopher/Audiocity/actions/runs/30594091465) and BUILD-01. |

Not performed: a release `Audiocity_All` build, installation, DAW/plugin-host validation, audio-hardware testing, ASIO testing, sanitizer/fuzzer execution, macOS/Linux builds, or measured coverage. Findings that depend on those surfaces are labeled as inferences or recommendations rather than confirmed failures.

### Strengths worth preserving

- The offline suite is broad and currently green, covering DSP, streaming, snapshots, importers, exporters, malformed corpora, preset behavior, and state helpers.
- Program/audio handoff uses immutable snapshots and an explicit hazard-pointer mechanism rather than locks on the audio thread.
- Importers already cap several ZIP/GZIP payloads, validate SF2 structures, reject archive traversal-shaped paths, and produce actionable diagnostics.
- The engine preallocates voices and block buffers, and the repository has written real-time constraints plus a profiling harness.
- Product breadth is strong for a small sampler: direct capture/generation, slicing, program editing, two export formats, and many import formats.

## Phase 2 — Performance and technical review

### 2A. Algorithmic and computational efficiency

#### PERF-01 — Full parameter graph is reapplied every audio block 🟠 High

- **Category:** Performance / Reliability · **Perspective:** Engineering
- **Evidence:** `processBlock()` calls `syncEngineFromAutomatableParameters()` on nearly every block ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1136-L1141)). That routine loads and reapplies the entire parameter set—envelopes, filter, modulation, polyphony, loop/window, effects, pan, and gain—whether values changed or not ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L938-L1057)). Envelope setters traverse all 64 voices, and filter updates invalidate every voice's coefficients ([EngineCore.cpp](../src/engine/EngineCore.cpp#L2439-L2514), [EngineCore.cpp](../src/engine/EngineCore.cpp#L3334-L3364)).
- **Impact:** Avoidable, buffer-size-dependent real-time CPU cost; smaller buffers amplify it. Reapplying polyphony and voice DSP state also increases the blast radius of CONC-01.
- **Recommendation:** Read APVTS atomics once into a block-local snapshot, compare against the last applied snapshot, and update only changed groups. Batch the two ADSR structures in one voice pass. Benchmark 32/64/128-sample blocks before and after.

#### PERF-02 — MIDI event dispatch remains quadratic at its bounded maximum 🟡 Medium

- **Category:** Performance / Reliability · **Perspective:** Engineering
- **Evidence:** Up to 1,024 events are insertion-sorted by offset (`O(E²)`) ([EngineCore.h](../src/engine/EngineCore.h#L613-L615), [EngineCore.cpp](../src/engine/EngineCore.cpp#L2617-L2633)). Each distinct render segment then scans every event again in `flushPendingEventsAtOffset()` ([EngineCore.cpp](../src/engine/EngineCore.cpp#L1542-L1559), [EngineCore.cpp](../src/engine/EngineCore.cpp#L2907-L2916)).
- **Impact:** Dense MIDI/automation bursts can create a deterministic real-time spike exactly when note density is high.
- **Recommendation:** Preserve `MidiBuffer` order or use a stable bounded sort once, then consume matching ranges with one monotonic cursor. Add a 1,024-event deadline benchmark and an output-equivalence test.

#### PERF-03 — Library indexing reads all sample audio and repeatedly rebuilds/sorts the accumulated UI list 🟠 High

- **Category:** Performance / UX · **Perspective:** Both
- **Evidence:** For every uncached audio file, the scanner divides the file into 256 windows but reads every sample across those windows ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L704-L775)). It posts a message-thread callback every 24 entries ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8367-L8462)); every callback filters and sorts the complete accumulated list ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8691-L8768)).
- **Impact:** Initial scan I/O is proportional to total audio-library bytes, not file count or visible rows. Repeated full-list work trends toward quadratic growth, so a 50k-file library can saturate storage and flood the message queue.
- **Recommendation:** Build cheap path/size/mtime metadata first, publish throttled count updates, sort once per coalesced interval/finalization, and generate peaks only for visible/near-visible rows through a bounded worker pool.

#### PERF-04 — A continuous preload dial rematerializes the loaded library 🟠 High

- **Category:** Performance / UX · **Perspective:** Both
- **Evidence:** Each `onValueChange` calls `setPreloadSamples()` and then refreshes the whole UI ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L5085-L5089)). The engine reconstructs the single sample and every program asset ([EngineCore.cpp](../src/engine/EngineCore.cpp#L1298-L1315)). For disk-backed assets, reconstruction allocates full buffers and rereads the streamed tail before rebuilding segments ([EngineCore.cpp](../src/engine/EngineCore.cpp#L3633-L3729)).
- **Impact:** Dragging one dial across a large multisample can trigger repeated whole-library allocation and disk I/O on the message thread, freezing the editor and creating transient memory peaks.
- **Recommendation:** Preview the numeric value while dragging; commit once on drag end or after a debounce. Rebuild asynchronously and atomically publish only the completed segment snapshot.

### 2B. Database and data layer

There is no database, server transaction, connection pool, or network data API to assess. The analogous persistence layer is local XML/files and immutable in-memory snapshots.

#### DATA-01 — Peak cache invalidation is stale-prone and single-root 🟡 Medium

- **Category:** Reliability / Performance · **Perspective:** Engineering
- **Evidence:** A cache hit checks only normalized path and byte size ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8422-L8438)); a same-size edit keeps stale metadata/peaks. Switching roots deletes the sole cache, and the entire map is serialized as one XML document ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8336-L8350), [PeakPreviewCache.cpp](../src/plugin/PeakPreviewCache.cpp#L66-L126)).
- **Impact:** Stale previews, repeated rescans when switching bookmarks, growing parse/write cost, and corruption blast radius across the whole index.
- **Recommendation:** Key by canonical path + size + mtime (content hash only when needed), partition by root, write atomically, and consider a compact per-entry/binary or embedded-index format once benchmarks justify it.

#### DATA-02 — Decoded NCW cache has no eviction policy 🟡 Medium

- **Category:** Resource Management · **Perspective:** Engineering
- **Evidence:** Converted WAVs are keyed by path/size/mtime and placed in a temp cache, but no age/size cleanup exists ([AudioFileSupport.cpp](../src/engine/AudioFileSupport.cpp#L33-L47), [AudioFileSupport.cpp](../src/engine/AudioFileSupport.cpp#L73-L148)).
- **Impact:** Large NCW libraries and updated source files can consume unbounded disk space with larger decoded WAV copies.
- **Recommendation:** Add a configurable cap, LRU/age cleanup, free-space checks, and a visible “clear decoded cache” action.

### 2C. Concurrency, async, and I/O

#### CONC-01 — UI and audio threads concurrently mutate/read live engine and voice state 🔴 Critical

- **Category:** Reliability / Performance · **Perspective:** Engineering
- **Evidence:** UI callbacks call processor setters directly ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L5090-L5117)). Those setters mutate `engine_` immediately before updating APVTS—for example pan, gain, delay, filter, envelope, and polyphony ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1217-L1266), [PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L3789-L3904)). At the same time the audio thread calls parameter sync and `engine_.render()` ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1136-L1195)). `EngineCore` settings and voices are ordinary non-atomic objects; setters can reset ADSRs, filters, and voices ([EngineCore.cpp](../src/engine/EngineCore.cpp#L2439-L2555)).
- **Impact:** C++ data races and undefined behavior: intermittent clicks, lost automation, corrupt voice state, or host crashes. `suspendParamSyncBlocks_` merely postpones one audio-thread write; it does not make message-thread mutation safe.
- **Recommendation:** Make APVTS/lock-free commands the only cross-thread input. UI setters should notify parameters or enqueue bounded commands, never touch live engine state. Apply the resulting block snapshot only on the audio thread. Audit all UI getters/setters, state restore, and background prepare paths against a written ownership table; add an aggressive parameter-change/render stress test.

#### CONC-02 — Detached importer can dereference a destroyed processor 🔴 Critical

- **Category:** Reliability · **Perspective:** Engineering
- **Evidence:** The import thread captures an editor `SafePointer` but also a raw `AudiocityAudioProcessor*`, calls it before checking the safe editor, and detaches ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8107-L8131)). The editor destructor only stops its timer/listener ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L5365-L5371)); the processor destructor joins only the stream-prime worker ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L783-L786)).
- **Impact:** Closing a plugin instance, unloading the plugin, or closing a DAW session during import can cause use-after-free. The failure depends on timing and may evade deterministic unit tests.
- **Recommendation:** Move import work into processor-owned `std::jthread`/JUCE thread-pool jobs, capture immutable inputs, signal stop, and join/cancel before processor destruction. Publish through a lifetime-safe callback/queue. Test destruction during a deliberately blocked import.

#### CONC-03 — “Cancel” discards results but does not stop work 🟠 High

- **Category:** Performance / Reliability / UX · **Perspective:** Both
- **Evidence:** Cancellation increments a generation and hides the UI ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8075-L8086)); importers receive no stop token. Starting another import creates another detached thread, and the library scan follows the same detached pattern ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8089-L8131), [PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L8367-L8487)).
- **Impact:** Rapid replace/cancel actions can leave several expensive decodes and full-library scans running concurrently, increasing memory, disk contention, and shutdown risk.
- **Recommendation:** Enforce single-flight jobs, cooperative cancellation checkpoints inside import/decode/peak loops, a small bounded executor, and explicit job states (`queued/running/cancelling/complete`).

### 2D. Memory and resource management

#### MEM-01 — Every plugin instance reserves about 27.5 MiB for idle capture/preview features 🟠 High

- **Category:** Performance / Resource Management · **Perspective:** Both
- **Evidence:** The processor contains a 30×48k float preview array (5.49 MiB) plus two 30×96k float capture arrays (21.97 MiB) as unconditional members ([PluginProcessor.h](../src/plugin/PluginProcessor.h#L546-L556)). Captured and embedded state vectors can duplicate audio again.
- **Impact:** Twenty unloaded instances reserve over 549 MiB before engine samples, voices, UI, or host overhead. This is costly in real DAW projects and discourages multi-instance use.
- **Recommendation:** Allocate capture storage only when capture is armed, use page/chunk pools prepared off the audio thread, shrink/release after commit, and allocate preview storage to the actual preview duration/rate.

#### MEM-02 — NKI probing reads and duplicates the entire untrusted file 🟠 High

- **Category:** Performance / Security · **Perspective:** Engineering
- **Evidence:** Even diagnostic-only probing calls `loadFileAsData()` for the entire `.nki`, then extracts every printable string into another vector before scanning ([LegacyNkiProbe.cpp](../src/engine/LegacyNkiProbe.cpp#L597-L644)). Modern monolithic files that cannot be imported still take this path.
- **Impact:** Multi-GB Kontakt containers can create extreme peak memory and long scans merely to report “unsupported”; malformed files can act as a local memory/CPU denial of service.
- **Recommendation:** Stream bounded chunks, cap bytes/strings/individual string length, detect known headers early, and stop once enough evidence is collected. Require an explicit deeper probe if the bounded pass is inconclusive.

#### MEM-03 — Embedded float state can bloat host sessions and duplicate buffers 🟠 High

- **Category:** Performance / Reliability / UX · **Perspective:** Both
- **Evidence:** Captured/generated/embedded audio is copied into `MemoryBlock` properties and serialized through XML on every state save ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1327-L1366), [PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1515-L1516)). A 30-second 96 kHz mono capture is about 11 MiB of raw float data before XML encoding and temporary copies.
- **Impact:** Large DAW project files, slow autosave/restore, memory spikes, and host watchdog risk. Conversely, external imported programs are not embedded at all (REL-02), producing inconsistent portability.
- **Recommendation:** Define a versioned binary/compressed asset container with explicit size limits and deduplication; offer “embed/collect/reference” policy instead of silently choosing opposite extremes by source type.

### 2E. Network and API efficiency

🔵 **Not applicable at runtime.** The product has no server API, remote payload, retry, or circuit-breaker surface. Keep network functionality opt-in if update checks or telemetry are added later; never put it on the audio thread.

### 2F. Build, bundle, and startup

#### BUILD-01 — Both CI paths are wired to a generator the hosted runner does not provide 🟠 High

- **Category:** Reliability / DX · **Perspective:** Engineering
- **Evidence:** `default` requires `Visual Studio 18 2026` ([CMakePresets.json](../CMakePresets.json#L9-L18)). Both workflows run on `windows-2022`; Build and Test calls that preset directly ([build-and-test.yml](../.github/workflows/build-and-test.yml#L30-L49)), while the UI script does the same ([export_ui_snapshots.ps1](../scripts/export_ui_snapshots.ps1#L119-L147)). GitHub's current `windows-2022` inventory lists Visual Studio Enterprise 2022 17.14, not VS 2026 ([official runner image](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md#visual-studio-enterprise-2022)). The latest public run is indeed a 17-second failure ([run #7](https://github.com/thetheosopher/Audiocity/actions/runs/30594091465)). The exact private step log was unavailable; the generator diagnosis is an inference from the checked-in commands and official image inventory.
- **Impact:** No trustworthy merge/release signal. The README simultaneously says VS 2022 is supported, so new contributors following the documented setup also fail ([README.md](../README.md#L107-L136)).
- **Recommendation:** Add portable Ninja and explicit VS 2022 presets; use one of them in `windows-2022` CI. Keep a separate VS 2026 developer preset. Add a configure-only matrix and display the selected generator/tool versions.

#### BUILD-02 — UI regression gate is red and has never run in its dedicated workflow 🟠 High

- **Category:** Reliability / UX · **Perspective:** Both
- **Evidence:** Local comparison reports all 19 screenshots mismatched at a zero-tolerance threshold; 17 have >10% different pixels and the worst is 44.78%. Visible differences include the baseline's “128 presets” versus the current 64 and changed performance-strip layout. The dedicated public UI workflow reports zero runs ([UI workflow](https://github.com/thetheosopher/Audiocity/actions/workflows/ui-snapshots.yml)). README screenshots point directly at these baselines ([README.md](../README.md#L37-L73)).
- **Impact:** Developers cannot distinguish intentional redesign from regressions, and public product imagery is stale.
- **Recommendation:** Manually review actual-vs-baseline pairs, fix unintended changes, approve/update intentional ones, then make the dedicated workflow run with the repaired CI preset. Never bulk-accept goldens without visual review.

#### BUILD-03 — CI omits the shippable products and installer-only changes 🟡 Medium

- **Category:** Reliability / DX · **Perspective:** Engineering
- **Evidence:** CI builds only offline and preset smoke executables, not `Audiocity_All` ([build-and-test.yml](../.github/workflows/build-and-test.yml#L43-L49)). Its path filters omit `installer/**` even though packaging tests validate installer configuration ([build-and-test.yml](../.github/workflows/build-and-test.yml#L8-L28)).
- **Impact:** Wrapper/link/resource-copy/installer defects can merge without compiling the Standalone or VST3 deliverables, and installer-only PRs can receive no check.
- **Recommendation:** Build `Audiocity_All` in Release, run packaging tests on installer/script changes, and add a plugin-host smoke validator before publishing artifacts.

#### BUILD-04 — Tests compile the same product sources repeatedly and register a duplicate test 🟡 Medium

- **Category:** DX / Build Performance · **Perspective:** Engineering
- **Evidence:** Three UI/preset executables each compile the long `AUDIOCITY_UI_SNAPSHOT_SHARED_SOURCES` list ([tests/CMakeLists.txt](../tests/CMakeLists.txt#L188-L231), [tests/CMakeLists.txt](../tests/CMakeLists.txt#L320-L370)). A second test is accidentally named `c:/projects/other/audiocity` and reruns the offline executable ([tests/CMakeLists.txt](../tests/CMakeLists.txt#L76-L94)).
- **Impact:** Large redundant builds, test-list noise, a leaked developer path, and doubled offline runtime in full CTest.
- **Recommendation:** Remove the duplicate registration and compile shared production code once as object/static test-support targets with explicit variant definitions.

#### BUILD-05 — File concentration raises incremental build and review cost 🟡 Medium

- **Category:** DX · **Perspective:** Engineering
- **Evidence:** `PluginEditor.cpp` is 12,189 lines, `OfflineRenderTests.cpp` 14,402, `PluginProcessor.cpp` 5,070, and `EngineCore.cpp` 4,126. Three UI tabs were extracted, but the editor still owns most layout, workflow, jobs, and state synchronization.
- **Impact:** Slow incremental compilation, merge conflicts, hard ownership boundaries, and review fatigue around the most failure-prone code.
- **Recommendation:** Continue behavior-preserving extraction by feature, supported by repaired goldens; split tests by subsystem and expose narrow processor/service interfaces.

### 2G. Observability and reliability

#### REL-01 — Large programs are silently truncated at 256 assets / 128 groups / 512 zones 🟠 High

- **Category:** Reliability / Data Integrity · **Perspective:** Both
- **Evidence:** `ProgramSnapshot::fromProgram()` sets `truncated` and clamps counts ([ProgramSnapshot.h](../src/engine/ProgramSnapshot.h#L11-L15), [ProgramSnapshot.h](../src/engine/ProgramSnapshot.h#L93-L105)). Production code publishes the snapshot without checking that flag ([EngineCore.cpp](../src/engine/EngineCore.cpp#L1175-L1226)); repository search finds no production read of `ProgramSnapshot::truncated`.
- **Impact:** A valid large library appears to load but some assets/groups/zones never sound. The editable `Program` can still report more zones than the render snapshot, producing user-visible inconsistency and potential destructive exports.
- **Recommendation:** Immediately reject or explicitly warn before publish. Then replace silent fixed limits with validated capacity negotiation or an immutable heap snapshot prepared off-thread. Test 257 assets, 129 groups, and 513 zones.

#### REL-02 — Imported-program session state depends on the original absolute path 🟠 High

- **Category:** Reliability / UX / Feature · **Perspective:** Both
- **Evidence:** State captures the program path and mapping state ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1327-L1340)). Restore synchronously reimports that path by format ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L1529-L1635)). Unlike embedded single samples/factory presets, there is no manifest, relocation search, or collect-assets workflow.
- **Impact:** Moving a library, opening a collaborator's project, or restoring on another machine can yield a silent/unrestored instrument with no guided recovery.
- **Recommendation:** Store relative-path/content metadata and present a missing-sample resolver. Add “Collect/Embed Assets” with size estimates and a versioned manifest; never silently omit unavailable samples.

#### REL-03 — Event overflow policy can drop safety-critical events without telemetry 🟡 Medium

- **Category:** Reliability / Observability · **Perspective:** Engineering
- **Evidence:** At 1,024 pending events, note-ons are dropped; other event types replace the latest note-on, but are dropped too if no note-on remains ([EngineCore.cpp](../src/engine/EngineCore.cpp#L2570-L2614)). There is no exposed dropped-event count or panic policy.
- **Impact:** Under pathological MIDI bursts, note-off/controller events can be lost and create stuck or incorrectly modulated voices without a diagnostic.
- **Recommendation:** Prioritize note-off/all-notes-off, reserve emergency capacity, expose per-block/total drop counters, and trigger a bounded panic if release integrity cannot be guaranteed.

#### REL-04 — Useful diagnostics exist, but release health is not measured 🟡 Medium

- **Category:** Observability · **Perspective:** Both
- **Evidence:** The UI exposes engine/import metrics and a copyable local import log, but CI has no coverage trend, sanitizer/fuzzer target, binary-size budget, render deadline budget, crash capture, or versioned support bundle.
- **Impact:** Regressions are found by users or ad-hoc local runs, and roadmap decisions lack aggregate evidence.
- **Recommendation:** Start with deterministic engineering telemetry in CI and a user-exported, privacy-preserving support bundle. Any product analytics should be explicit opt-in, path-redacted, and default-off.

### 2H. Security/performance intersections

#### SEC-01 — Untrusted import limits are inconsistent 🟠 High

- **Category:** Security / Reliability / Performance · **Perspective:** Engineering
- **Evidence:** ZIP/GZIP importers have useful payload caps, but NKI probing has none (MEM-02), several binary formats call `loadFileAsData()`, and there is no sanitizer/fuzz CI. Import occurs off-thread but can still exhaust the process.
- **Impact:** Crafted or simply huge local libraries can cause excessive allocation/CPU and crash the plugin host, whose process may contain an unsaved project.
- **Recommendation:** Define shared importer budgets (input bytes, expanded bytes, entries, zones, samples, recursion, elapsed/cancel checkpoints), return structured limit diagnostics, and run corpus fuzzing under ASan/UBSan on a supported Clang platform.

#### SEC-02 — Build dependencies use mutable major/tag references 🟡 Medium

- **Category:** Security / DX · **Perspective:** Engineering
- **Evidence:** JUCE is fetched by Git tag and Actions use floating major tags (`checkout@v4`, `upload-artifact@v4`) ([CMakeLists.txt](../CMakeLists.txt#L47-L56), [ui-snapshots.yml](../.github/workflows/ui-snapshots.yml#L21-L48)). The latest run also reports that checkout v4's Node 20 runtime is deprecated ([run #7](https://github.com/thetheosopher/Audiocity/actions/runs/30594091465)).
- **Impact:** Supply-chain inputs can drift independently of repository commits, and deprecated action runtimes create avoidable failures.
- **Recommendation:** Upgrade intentionally, then pin third-party Actions and JUCE to reviewed commit SHAs; automate update PRs rather than floating at runtime.

Positive note: archive-relative path validation, bounded archive reads, converter timeouts, and explicit error diagnostics materially reduce the importer attack surface and should be preserved.

### 2I. Dependency health

#### DEP-01 — JUCE is nine patch releases behind and warns under current CMake 🟡 Medium

- **Category:** Reliability / Performance / DX · **Perspective:** Engineering
- **Evidence:** The repository uses 8.0.4; JUCE 8.0.13 was released on 2026-05-19 and includes GUI compile-time, painting, Windows rendering, and resize improvements ([official JUCE 8.0.13 release](https://github.com/juce-framework/JUCE/releases/tag/8.0.13)). The review's CMake 4.2.3 configure emitted a JUCE-side CMP0175 warning.
- **Impact:** Missing fixes and increasing build-tool friction. A large jump also carries behavioral risk, especially for plugin state, rendering, and hosts.
- **Recommendation:** Upgrade in one isolated PR after CI is repaired; review JUCE breaking changes, pin the commit, and run full audio/UI/plugin-host validation.

#### DEP-02 — REX integration is compiled as Windows-only platform code 🟡 Medium

- **Category:** Reliability / Portability · **Perspective:** Engineering
- **Evidence:** The REX target unconditionally defines `REX_WINDOWS=1` and `REX_MAC=0`, and links Windows `Version` under MSVC ([CMakeLists.txt](../CMakeLists.txt#L83-L99)).
- **Impact:** This is a direct obstacle to cross-platform builds and may obscure SDK redistribution/runtime constraints.
- **Recommendation:** Put REX behind a platform capability target, document SDK/license/runtime provenance, and provide a clean no-REX path on unsupported platforms.

## Phase 3 — Product and feature review

### 3A. Feature completeness versus user needs

#### PROD-01 — Session portability and missing-asset recovery are the largest trust gap 🟠 High

- **Category:** Feature / Reliability / UX · **Perspective:** Both
- **Finding:** Imported instruments are reference-only while single-sample presets can embed audio (REL-02/MEM-03). Users need a predictable choice among reference, collect, and embed, plus guided relinking.
- **Domain signal:** Apple's Sampler documentation says associated audio is automatically located when an instrument opens, and Decent Sampler now includes “Collect Samples” and cleanup workflows ([Apple Sampler overview](https://support.apple.com/guide/logicpro/sampler-overview-lgsifc860d24/mac), [Decent Sampler versions](https://store.decentsamples.com/downloads/decent-sampler/versions)).
- **Recommendation:** Prioritize a portable asset manifest and resolver before adding more import formats.

#### PROD-02 — Broad advertised format support is hidden behind one discovery path 🟠 High

- **Category:** UX / Feature · **Perspective:** Product
- **Evidence:** The library index recognizes 18+ extensions ([LibraryFileIndex.cpp](../src/plugin/LibraryFileIndex.cpp#L5-L18)), but drag/drop and the primary chooser accept only WAV/AIFF/SFZ/NKI/REX ([PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L11317-L11350), [PluginEditor.cpp](../src/plugin/PluginEditor.cpp#L11399-L11407)).
- **Impact:** Users can reasonably conclude that SF2, DecentSampler, Bitwig, MPC, TAL, Ableton, Korg, EXS, and other advertised imports do not work unless they first configure and scan a library root.
- **Recommendation:** Use one format registry for chooser, drag/drop, browser, badges, diagnostics, tests, and docs; explain partial/legacy compatibility in-place.

#### PROD-03 — Export claims exceed semantic fidelity 🟡 Medium

- **Category:** Feature / Reliability · **Perspective:** Both
- **Evidence:** The DecentSampler exporter warns that velocity fades and one-shot triggers are omitted, sustain loops become continuous, and some round robins are omitted ([DecentSamplerExporter.cpp](../src/engine/DecentSamplerExporter.cpp#L317-L326), [DecentSamplerExporter.cpp](../src/engine/DecentSamplerExporter.cpp#L335-L382), [DecentSamplerExporter.cpp](../src/engine/DecentSamplerExporter.cpp#L430-L475)).
- **Impact:** “Exported successfully” can still change how an instrument plays. That undermines Audiocity's potential as an interchange/authoring tool.
- **Recommendation:** Show a preflight compatibility report with severity and affected zones; offer strict mode that refuses lossy export; close the highest-volume semantic gaps with round-trip fixtures.

#### PROD-04 — Documentation and product imagery disagree with the shipped bank 🟡 Medium

- **Category:** UX / Reliability · **Perspective:** Product
- **Evidence:** The repository contains 64 factory `.acp` files and README says 64, but the user guide says 128 ([USER_GUIDE.md](USER_GUIDE.md#L113-L127)). Committed screenshots also render 128 while the current harness renders 64.
- **Impact:** Avoidable expectation mismatch and a visible sign that release documentation is not generated from product metadata.
- **Recommendation:** Derive counts/version/format tables from one manifest and validate docs/screenshots during release builds.

### 3B. UX and developer-experience gaps

#### PROD-05 — Long operations lack truthful progress and bounded cancellation 🟠 High

- **Category:** UX / Reliability · **Perspective:** Both
- **Finding:** Background import shows preparing/publishing states, but cancel only ignores the final result (CONC-03). Library scans do not expose bytes/files remaining, cache status, or pause/resume.
- **Recommendation:** Report stage and work completed from cooperative jobs; distinguish “cancelling” from “cancelled”; allow the user to continue playing the old instrument until the new snapshot is ready.

#### PROD-06 — The preload control exposes an implementation detail with catastrophic interaction cost 🟠 High

- **Category:** UX / Performance · **Perspective:** Both
- **Finding:** A technical tuning parameter behaves like an ordinary real-time knob while rebuilding the entire library (PERF-04).
- **Recommendation:** Replace the raw sample-count dial with safe profiles (`Low memory`, `Balanced`, `Low-latency`) plus an advanced setting, estimated RAM, and one explicit apply operation.

#### PROD-07 — Accessibility is unverified across a custom, dense UI 🟡 Medium

- **Category:** UX · **Perspective:** Product
- **Evidence:** The UI is custom-drawn and keyboard-focus-heavy, but no explicit accessibility handlers or automated keyboard/focus audit were found in `src/plugin`.
- **Impact:** Screen-reader labeling, focus order, high-contrast behavior, and keyboard-only mapping workflows may be inconsistent.
- **Recommendation:** Run a Windows Narrator/Accessibility Insights audit; add component names/descriptions, focus-order tests, scalable text, and contrast checks. Treat this as an evidence-gathering task before a redesign.

### 3C. Data and analytics gaps

#### PROD-08 — Roadmap decisions lack privacy-safe outcome data 🟡 Medium

- **Category:** Observability / Product · **Perspective:** Product
- **Finding:** Local import logs are useful for support, but there is no aggregate knowledge of format usage, failure reasons, library sizes, load latency, or relink failures. For an MIT desktop plugin, silent telemetry would be inappropriate.
- **Recommendation:** First add an exportable, path-redacted support bundle and CI performance history. If maintainers need aggregate product data, make it explicit opt-in, document the schema/retention, and collect only coarse format/outcome/version/timing fields.

### 3D. Competitive and domain gaps

#### PROD-09 — Windows-only VST3/Standalone sharply limits reach 🟠 High

- **Category:** Feature / Growth · **Perspective:** Product
- **Evidence:** The build emits only VST3 and Standalone on Windows ([CMakeLists.txt](../CMakeLists.txt#L61-L76)). JUCE itself supports VST3, AU/AUv3, LV2, AAX, and cross-platform apps ([JUCE project](https://github.com/juce-framework/JUCE)); free Decent Sampler ships Windows, macOS, Linux, and iOS variants across VST/VST3/AU/AAX/Standalone ([official product page](https://www.decentsamples.com/product/decent-sampler-plugin/)).
- **Impact:** No Logic/GarageBand AU audience, no Apple Silicon/macOS users, no Linux users, and no CLAP ecosystem.
- **Recommendation:** After correctness and CI are stable, add macOS VST3/AU first, then evaluate Linux/CLAP using measured user demand. Signing/notarization and REX/ASIO isolation are part of the initiative, not afterthoughts.

#### PROD-10 — Expressive/tempo-aware and routing capabilities trail mature samplers 🟡 Medium

- **Category:** Feature · **Perspective:** Product
- **Evidence:** The current processor exposes one stereo output bus ([PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp#L745-L747)); the code has channel pressure/pitch/mod-wheel handling but no MPE model, and time-stretch remains a future roadmap item ([docs/06-roadmap.md](06-roadmap.md#L165-L185)). Kontakt exposes instrument scripting and modern tool layers, while current Decent Sampler releases advertise tempo-synced LFOs, MPE modulation, arpeggiation, and richer engines ([Kontakt KSP reference](https://www.native-instruments.com/fileadmin/ni_media/downloads/manuals/kontakt/Kontakt_8_KSP_Reference_Manual-en_260924.pdf), [Decent Sampler versions](https://store.decentsamples.com/downloads/decent-sampler/versions)).
- **Impact:** Power users cannot build multi-mic/multi-out instruments, expressive per-note patches, or tempo-independent loops inside Audiocity.
- **Recommendation:** Do not implement all of these at once. Interview library authors/users, instrument current workflows, and sequence the best validated wedge—likely multi-output or MPE before scripting.

### 3E. Technical investment versus user value

#### PROD-11 — Engineering depth is concentrated behind fragile entry and delivery surfaces 🟠 High

- **Category:** Product / DX · **Perspective:** Both
- **Finding:** Considerable complexity supports many importers, deterministic UI states, and detailed modulation, yet the main chooser hides most formats, CI is red, goldens are stale, imported sessions are not portable, and large programs can truncate.
- **Recommendation:** Pause new-format breadth until the format registry, CI, snapshot gate, capacity diagnostics, cancellation, and asset relinking make existing breadth trustworthy.

### 3F. Monetization and growth hooks

🔵 **Monetization is not currently a code requirement.** Audiocity is MIT-licensed and the README points to Buy Me a Coffee. Forced tiering or account infrastructure would add support/security burden without demonstrated demand. The credible growth hooks are broader platform availability, trustworthy portable libraries, excellent interchange, and a frictionless public release pipeline. If paid content or a marketplace is ever considered, design signed package manifests and entitlement boundaries separately from the real-time engine.

## Consolidated priority

1. **Fix CONC-01 first:** one audio-thread-owned parameter/control plane.
2. **Fix CONC-02/03:** owned, cancellable, joined background work.
3. **Repair CI and the UI golden gate:** no further architectural work is safe without dependable feedback.
4. **Prevent silent data loss:** reject/warn on snapshot limits and add asset relinking.
5. **Then address scale:** lazy capture memory, library indexing, event dispatch, and preload rebuilding.

The implementation sequence, estimates, success criteria, dependency order, and ready-to-use prompts are in [ENHANCEMENT_ROADMAP_2026-09-05.md](ENHANCEMENT_ROADMAP_2026-09-05.md). A stakeholder summary is in [REVIEW_EXECUTIVE_SUMMARY_2026-09-05.md](REVIEW_EXECUTIVE_SUMMARY_2026-09-05.md).

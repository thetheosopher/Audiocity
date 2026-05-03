# Audiocity Roadmap (Hybrid Sampler: Standalone + VST3)

> **Project goal:** Audiocity is a high‑performance sampler implemented as a JUCE/C++ engine, shipped as both a **Standalone Windows app** and a **VST3 instrument plugin**.
> **Design principle:** Spec-first + automated tests to keep behavior stable while iterating.
> **Plugin FX hosting:** **Out of scope** (DAW provides FX when running as a plugin).

---

## Milestone 0 — Scaffolding & build baseline

**Deliverables**
- JUCE project builds **Standalone** + **VST3** from the same `AudioProcessor` implementation.
- Sampler-style UI shell with placeholders for **Browser**, **Mapping**, **Editor**, **Settings**, **Diagnostics**.
- Offline render test harness skeleton (no audio device required).

**Definition of Done (DoD)**
- Standalone runs and produces audio output via JUCE device management (basic config).
- VST3 loads in a host and processes blocks without crashing.
- CI/build script can produce both artifacts.

---

## Milestone 1 — MVP (Vertical slice: sound + polyphony + simple patch)

**Goal:** Prove the engine and hybrid delivery end-to-end.

**Features**
- Sample playback from one WAV/AIFF loaded into memory.
- Pitch shifting by resampling (engine-native fast path).
- Polyphony with voice stealing.
- Amp ADSR.
- Basic filter (LPF) + filter ADSR.
- Patch save/load (minimal state).

**Tests**
- Offline render determinism: same sample + same MIDI events → repeatable output.
- Voice-stealing edge cases (max voices exceeded).

**DoD**
- Standalone: MIDI input triggers sound.
- Plugin: DAW MIDI triggers sound.

---

# Post‑MVP Releases

## Release v1 — “Sampler-grade workflows” (Mapping + Browser + SFZ import)

### Epic A — Sampler-style Browser 1.0

**Scope**
- Watched folders / scan paths + bookmarks.
- Search + filters/tags (simple tags first).
- Preview playback with waveform/scrub.
- Recent items and favorites.

---

### Epic B — Mapping Editor 1.0 (zones, layers, RR)

**Scope**
- Key range zones (`lokey/hikey`) and single-key mapping (`key`).
- Velocity layers (`lovel/hivel`).
- Optional velocity crossfades in UI (ramps).
- Round robin groups + selection modes: round robin (ordered), cycle random.

---

### Epic C — Playback modes users expect (Gate/One-shot/Loop)

**Scope**
- Gate (plays while held)
- One-shot (plays full sample)
- Loop (repeats while held or continuously depending on mode)

---

### Epic D — Looping & loop tooling (SFZ-aligned)

**Scope**
- Loop start/end markers.
- Loop modes imported from SFZ: `loop_continuous`, `loop_sustain`, `no_loop`.
- Optional: loop crossfade.

---

### Epic E — SFZ import v1 (core subset + `#include`)

**Scope**
- Preprocessor: `#include` (nested + cycle detection), `#define` expansion.
- Parser: core opcode subset.
- Import: convert to Program/Group/Zone; emit diagnostics.

---

### Epic F — Mono/legato + glide

**Scope**
- Mono/legato.
- Glide.

---

## Release v1.1 — “Scale up libraries” (performance + streaming + quality modes)

### Epic G — Disk streaming (DFD) + preload/priming

**Current validated slice**
- Preload segmentation and rebuild behavior are live for both single-sample and imported-program playback, with engine metrics exposing loaded preload versus streamed sample counts.
- Single-sample playback and imported-program sample tails can stream from disk through a bounded cache instead of requiring the full tail in memory.
- Stream priming is serviced off the audio thread, and offline tests cover preload rebuilds, single-sample file-backed streaming, imported-program cache hit/miss telemetry, and note-on/lookahead priming behavior.

### Epic H — Quality tiers (CPU vs fidelity)

**Current validated slice**
- CPU, Fidelity, and Ultra playback tiers are wired end-to-end through processor state and UI controls.
- Ultra now uses a higher-quality windowed-sinc resampler path while CPU and Fidelity remain the lighter playback modes.
- Offline tests cover audible tier differences, deterministic output per tier, runtime switching across all three tiers, and a near-Nyquist pure-tone spectral regression that requires Ultra to preserve more main-tone energy with lower side-energy than Fidelity at a non-integer pitch ratio.

### Epic I — Undo/Redo across mapping and sample edits

**Current validated slice**
- Imported-program structural mapping undo/redo uses serialized mapping snapshots.
- Editor undo/redo is now chronological across mapping changes and sample/settings edits rather than split by tab or edit domain.
- Multi-zone imported-program mapping apply/delete commits atomically at the processor publish boundary so failed batch edits roll back cleanly.
- The Mapping tab now has a true create-zone workflow that appends a new playable zone, seeded from the current selection when available and able to target a chosen imported sample asset.
- Duplicate and split structural zone operations are covered by the same unified mapping history path.

---

## Release v2 — “Sound design depth” (modulation + slicing)

### Epic J — Modulation matrix + multi-LFO/envelopes

**Current validated slice**
- `EngineCore` now has an RT-safe modulation-routing settings snapshot for mod wheel, aftertouch, velocity, and two macro controls, with sample-accurate MIDI handling for CC1 and aftertouch plus per-voice velocity routing.
- The processor exposes those routes and macro values through automatable parameters and patch-state persistence, so host automation and saved presets can drive the first modulation-routed behavior without relying on the editor timer path.
- The Player page now includes a first modulation surface with expressive-source and macro routing controls in the existing sampler control layout.
- Offline tests now cover real-time modulation routing during playback and verify audible response across pitch, filter, and amp destinations for the new source set.

### Epic K — Slicing + slice mapping

**Current validated slice**
- `.rex` and `.rx2` files now import as playable slice programs instead of flattening into a single editable sample path, while still keeping the concatenated loop loaded for waveform display and sample inspection.
- The REX decoder now preserves per-slice boundaries, and the slice-program builder maps slices chromatically from MIDI note 36 so the existing Player pads trigger the first eight slices without extra setup.
- Imported-program state persistence is now format-agnostic rather than SFZ-only, so REX slice programs restore through the same mapping-state path used by other imported programs.
- Regular sample files can now be auto-sliced into imported slice programs from transient detection, with the waveform view drawing the derived slice boundaries for single-sample imported programs.
- Sample-derived slice programs persist as an explicit imported-program format, so transient-sliced WAV/AIFF sources restore through the same generic mapping-state replay path as SFZ and REX imports.
- Offline tests cover preserved REX slice boundaries, transient slice-program construction, and imported-program path compatibility across the new generic property, explicit sample-slice format metadata, and the legacy SFZ path fallback.

### Epic L — Mapping productivity tools

**Current validated slice**
- Imported-program zone context menus now include a chromatic remap action that reassigns the selected zones to single-note keys starting at C1 while preserving the rest of each zone's settings.
- The remap action rides the existing imported-program undo/publish pipeline, so it participates in chronological editor history and republishes atomically to the engine like other mapping edits.
- Imported-program mapping tools now also include a key-range spread action that redistributes the selected zones evenly across their current combined key span while updating root notes to match the new ranges.
- Offline tests cover both model-level mapping helpers, including ordered chromatic remap behavior, even key-range spreading, and group-range recomputation.

---

## Release v3 — “Pro ecosystem” (broader SFZ + time-stretch + expression)

### Epic M — Expanded SFZ compatibility (selective)
### Epic N — Time-stretch modes (independent time/pitch)
### Epic O — Expressive MIDI (optional)

---

# Cross-cutting Epics

## X1 — Deterministic offline render harness expansion
- Add fixtures for SFZ include/default_path, RR, velocity crossfades, looping.

## X2 — Diagnostics & observability
- Non-RT logging pipeline + Diagnostics tab.

## X3 — Dependency management & licensing hygiene

**Recommended libraries by release**
- **MVP:** `nlohmann/json`, `doctest` (or Catch2)
- **v1:** `SQLite`, `dr_wav`
- **v1.1:** `libsamplerate`; optional `libsndfile`; optional `libsoxr`
- **v2:** `KISS FFT`
- **v3:** optional `sfizz` reference

---

# Appendix — SFZ behavior choices
- `#include` resolution base is **root SFZ directory**.
- `<control> default_path` reset follows ARIA-like interpretation.

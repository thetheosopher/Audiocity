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

### Epic F — Choke groups + mono/legato + glide

**Scope**
- Choke groups.
- Mono/legato.
- Glide.

---

## Release v1.1 — “Scale up libraries” (performance + streaming + quality modes)

### Epic G — Disk streaming (DFD) + preload/priming
### Epic H — Quality tiers (CPU vs fidelity)
### Epic I — Undo/Redo across mapping and sample edits

---

## Release v2 — “Sound design depth” (modulation + slicing)

### Epic J — Modulation matrix + multi-LFO/envelopes
### Epic K — Slicing + slice mapping
### Epic L — Mapping productivity tools

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

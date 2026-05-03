# Legacy NKI Import Plan

## Goal

Import older Kontakt `.nki` instruments that reference discrete sample files on disk and translate them into Audiocity imported programs.

This plan is intentionally limited to the earlier NKI era where instruments point at external WAV/AIFF samples. It does not target newer monolithic, encrypted, compressed, or database-backed Kontakt library formats.

## Scope

### In scope
- `.nki` instruments whose zone/sample definitions can be resolved to separate audio files on disk.
- Key range, root note, velocity range, tuning, offset/end, loop points, gain, pan, and one-shot or normal trigger behavior when those fields are present.
- Group-level translation where it maps cleanly to Audiocity groups.
- Import diagnostics for unsupported constructs, missing samples, and partially translated content.
- Imported-program persistence through the existing generic imported-program state path.

### Out of scope
- Monolithic sample containers and newer Kontakt packaging formats.
- Encrypted or licensed library protection schemes.
- Kontakt Script Processor behavior, effects racks, instrument buses, and host automation metadata.
- Perfect binary compatibility with every Kontakt generation before a playable subset exists.

## Constraints

- All file parsing and sample discovery must stay off the audio thread.
- Imported results should land in the existing `audiocity::engine::Program` plus sample-data path instead of inventing a parallel playback engine.
- Unsupported features should degrade with diagnostics rather than silent import failure.
- The importer must preserve Audiocity's current patch restore behavior by storing enough source metadata to re-resolve or rehydrate the imported program later.

## Proposed Architecture

### 1. Format probe and fixture capture
- Add a lightweight probe that distinguishes legacy discrete-sample NKI candidates from unsupported/newer variants before full parsing.
- Build a small fixture set from representative Kontakt 1/2 era libraries with simple chromatic, drum, and velocity-layered instruments.
- Record sample layout patterns up front: instrument-relative paths, sibling `Samples` folders, nested bank folders, and mixed WAV/AIFF libraries.

### 2. Parser plus intermediate representation
- Implement a dedicated non-RT parser for the legacy chunk structure rather than binding to Kontakt itself.
- Translate parsed content into a narrow intermediate representation: instrument metadata, groups, zones, and sample references.
- Keep the first parser milestone read-only and diagnostic-heavy so unsupported fields are surfaced before playback translation starts.

### 3. Sample resolution layer
- Resolve sample references relative to the `.nki` file first, then through common legacy library layouts.
- Normalize path separators and case handling so the same importer logic behaves predictably on Windows and other hosts.
- Report every unresolved sample explicitly and skip only the affected zones.

### 4. NKI-to-Program translation
- Map resolved zones into `audiocity::engine::Program`, reusing the imported-program sample asset path already used by SFZ and slice imports.
- Translate at least these fields in the first playable version:
  - key range
  - root note
  - velocity range
  - tune and transpose
  - sample start/end
  - loop start/end and loop enable
  - gain and pan
- Collapse unsupported Kontakt-specific behavior into warnings attached to the import diagnostic summary.

### 5. Persistence and editor integration
- Extend imported-program metadata with an explicit legacy NKI format tag once the importer exists.
- Persist the source `.nki` path plus any importer options needed for deterministic restore.
- Reuse existing mapping UI, diagnostics UI, and imported-program summaries rather than creating an NKI-specific editor surface.

## Delivery Phases

### Phase 1. Research and fixture baseline
- Collect a small verified fixture library.
- Add a document that records the observed legacy chunk variants and sample-reference patterns.
- Confirm the minimum viable feature subset against real files before writing the translator.

### Phase 2. Probe and parser skeleton
- Add file sniffing plus parser scaffolding.
- Emit metadata and diagnostics only.
- Add offline tests that assert parser success or graceful rejection for known fixtures.

### Phase 3. First playable import
- Resolve external samples.
- Translate basic zones/groups into `Program`.
- Load through the existing imported-program publish path.
- Add offline render tests for at least one chromatic and one drum-style legacy NKI fixture.

### Phase 4. Persistence and UX
- Add explicit imported-program format persistence for legacy NKI.
- Surface `NKI` badges and diagnostics in the editor using the existing imported-program status path.
- Add restore tests so saved patches reopen without reparsing surprises.

### Phase 5. Compatibility expansion
- Expand field coverage only after the basic playable path is stable.
- Add more fixture families for velocity splits, looped sustain instruments, and mixed missing-sample scenarios.
- Keep new feature additions gated by fixture-backed offline tests.

## Dependency Strategy

- Prefer an internal parser for the first implementation. The format subset needed for legacy discrete-sample instruments is small enough that a focused reader is lower-risk than bringing in a large third-party Kontakt dependency.
- Avoid dependencies that require Kontakt binaries, proprietary SDKs, or runtime redistribution constraints.
- If a helper library becomes necessary, keep it optional, isolated behind a small adapter, and documented in [docs/10-dependencies.md](docs/10-dependencies.md).

## Test Strategy

- Parser tests: fixture opens, unsupported variant rejection, and chunk-field extraction.
- Resolver tests: relative-path, sibling-folder, and missing-sample diagnostics.
- Translation tests: expected zone count, key/root mapping, loop data, and sample-asset association.
- Persistence tests: imported-program round-trip with explicit format metadata.
- Offline render tests: audible regression coverage for at least one pitched legacy instrument and one drum kit.

## Risks

- Legacy NKI has format drift across Kontakt generations, so the probe must reject unknown variants early.
- Sample-reference conventions may vary by library vendor, which makes resolver diagnostics as important as parser correctness.
- Kontakt-only behaviors such as scripts and advanced group logic will create user expectations that Audiocity cannot meet initially.

## Recommended First Slice

Implement Phase 2 before any UI work: add a probe, parse only enough metadata to enumerate groups/zones/sample references, and cover it with fixture-backed offline tests. That gives a falsifiable base for the importer without committing to a playback translation path too early.
# Multisample Import Expansion Plan

## Goal

Extend Audiocity's imported-program coverage to common third-party
multisample / instrument formats inspired by the catalogue supported by the
[ConvertWithMoss](https://www.mossgrabers.de/Software/ConvertWithMoss/ConvertWithMoss.html)
project. The intent is to read these formats into Audiocity's existing
`audiocity::engine::Program` data path with no Java dependency, no large LGPL
ports, and no encrypted-binary redistribution from third-party tooling.

ConvertWithMoss (LGPL-3.0, Java) is treated as a *reference* for which formats
are practical to support and as a public-knowledge map of where each format's
relevant chunks live. Audiocity's own importers are written from public format
specifications in this repository's permissive code path.

## Constraints

- All file parsing and sample loading stays off the audio thread.
- Imported results land in `audiocity::engine::Program` plus
  `std::vector<juce::AudioBuffer<float>>` exactly like `SfzImporter` and
  `LegacyNkiProbe`.
- Imports persist through the existing `ImportedProgramState` seam so
  saved patches restore deterministically.
- No third-party Java code or LGPL parsing libraries are bundled.
- No encrypted Kontakt `.nki` content is decrypted by Audiocity in this plan.

## Format Triage

The matrix below classifies the formats published on the ConvertWithMoss site.
Triage reflects what is reasonable to port into Audiocity's permissive C++
codebase first.

### Tier 1 - Implement now (this delivery)

| Format                       | Extension(s)            | Why first                                                                          |
|------------------------------|-------------------------|------------------------------------------------------------------------------------|
| SoundFont 2                  | `.sf2`                  | Public spec, RIFF binary, embedded 16-bit PCM samples, single self-contained file. |
| DecentSampler                | `.dspreset`             | Plain XML, references external WAVs, simple zone model.                            |

### Tier 2 - Next phases (planned)

| Format                                | Extension(s)                | Notes                                                                                                          |
|---------------------------------------|-----------------------------|----------------------------------------------------------------------------------------------------------------|
| Bitwig / Studio One Multisample       | `.multisample`              | ZIP container with `multisample.xml` plus WAV files. Reuse JUCE's ZipFile + a small XML reader.                |
| Logic EXS24                           | `.exs`                      | Documented binary chunk format; samples sit in a sibling `Sampler Files` directory.                            |
| Akai MPC Keygroups                    | `.xpm`                      | XML preset, references WAV samples next to it.                                                                 |
| Korg KMP / KSF                        | `.KMP`, `.KSF`              | Small fixed-record binary; KMP describes a multi-zone instrument that points at sibling KSF samples.           |
| Propellerhead Reason NN-XT            | `.sxt`                      | Documented binary, references external samples in the Reason refill / file system.                             |
| 1010music Bento / blackbox            | `preset.xml`                | Plain XML referencing WAV samples in the same folder.                                                          |
| TAL Sampler                           | `.talsmpl`                  | XML descriptor referencing external samples.                                                                   |
| Ableton Sampler                       | `.adv`, `.adg`              | gzip-wrapped XML.                                                                                              |
| CWITEC TX16Wx                         | `.txprog`                   | XML preset with referenced samples.                                                                            |
| Korg wavestate / modwave              | `.korgmultisample`          | ZIP + XML.                                                                                                     |
| Expert Sleepers disting EX            | `.dexpreset`                | Small text/INI-like preset.                                                                                    |
| NCW (Native Instruments compressed)   | `.ncw`                      | Compressed PCM container; needed to load full "discrete sample" Kontakt libraries that ship NCW instead of WAV. |

### Tier 3 - Probe-only or deferred (out of scope for first port)

| Format                                                        | Reason                                                                                                                          |
|---------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------|
| Native Instruments Kontakt newer monolithic / encrypted `.nki`| Requires reverse-engineered crypto and large Kontakt-specific opcode tables. Out of scope; see "Kontakt strategy" below.       |
| Akai legacy ISO / floppy images (`.img`, `.iso`, `.hfe`)      | Disk-image emulation; very specialist. Probe-only at most.                                                                      |
| Akai S5000/S6000/Z4/Z8/MPC4000 (`.akp`, `.akm`)               | Niche binary format; valuable but lower priority.                                                                               |
| Yamaha YSFC                                                   | Large, multi-instrument workstation database; defer.                                                                            |
| Waldorf Quantum / Iridium                                     | Defer.                                                                                                                          |
| Reason refill containers                                      | Container format outside scope; only loose `.sxt` files are targeted.                                                           |

## Kontakt strategy

The current legacy `.nki` work (`docs/12-legacy-nki-import-plan.md`,
`src/engine/LegacyNkiProbe.*`) targets old discrete-sample `.nki` files that
reference external WAV/AIFF samples on disk. This plan keeps that scope and
extends it as follows:

1. **Phase A (already shipped).** Probe + first playable subset for legacy
   discrete-sample `.nki`.
2. **Phase B (next).** When a `.nki` file references NCW samples (Native
   Instruments Compressed Wave) instead of WAV/AIFF, decode NCW into PCM
   through a new `NcwReader` that follows the published NCW container layout
   and either uses ZLIB (already pulled in by JUCE) for the compressed payload
   or falls back to "missing" diagnostics when the variant is unsupported.
3. **Phase C (best-effort).** Detect newer monolithic / "Kontakt 5+"
   container `.nki` files that embed a serialized Kontakt instrument tree.
   Audiocity will continue to *probe* these (already implemented) and report
   them as unsupported. Audiocity will not ship a decryption routine for
   protected / "encrypted" Kontakt content. Even though the Kontakt format is
   widely reverse-engineered, the protection scheme on locked libraries is
   tied to per-library secrets that Audiocity has no legitimate way to obtain
   from a third party. Locked content stays unsupported by design; users are
   directed to vendor-provided unlocked copies.
4. **Phase D (optional).** Implement an NKM bank reader that lists the
   instruments inside an NKM and round-trips the user back to per-instrument
   import for NKI variants we can already read.

## Architecture

Each importer follows the shape established by `SfzImporter` and
`LegacyNkiProbe`:

- `engine::<format>::importFile(juce::File)` returns a `Program` plus
  `std::vector<juce::AudioBuffer<float>>` plus a diagnostic vector.
- The importer is instantiated on a non-audio thread (typically the message
  thread that handles the file pick).
- The processor exposes `importXxxProgram(juce::File)` which clears the engine,
  publishes the program with `EngineCore::setProgram`, and stamps
  `ImportedProgramFormat::xxx` in the imported-program metadata.
- The editor's `loadFileAsInstrument` extension (and the imported-program
  restore branch in `PluginProcessor::setStateInformation`) routes by
  detected format.
- Browser indexing (`LibraryFileIndex`) treats new instrument extensions as
  instrument-class entries so the librarian's instrument badge appears.

## Tier 1 designs

### SoundFont 2 (`.sf2`)

`Sf2Importer` parses the documented SoundFont 2.04 RIFF layout
(`RIFF...sfbk` -> `LIST INFO`, `LIST sdta` (`smpl`), `LIST pdta`
(`phdr`, `pbag`, `pmod`, `pgen`, `inst`, `ibag`, `imod`, `igen`, `shdr`)).

Per-zone generator overlay rules:
- The first instrument zone with no `sampleID` generator (gen 53) is the
  instrument's *global* zone and supplies defaults to all sibling zones.
- The first preset zone with no `instrument` generator (gen 41) is the
  preset's *global* zone. Preset-level generators add to instrument-level
  generators where SF2 says they should add (key range/vel range intersect,
  others additive). Modulators are not implemented in v1; the importer warns
  when modulators are present.

Generators handled in v1:
- `0/1/4/12` start/end addr offsets (low + coarse).
- `2/3` loop start/end addr offsets.
- `17` pan.
- `43/44` key range / vel range.
- `48` initial attenuation (centibels -> dB).
- `51/52` coarse / fine tune.
- `53` sample ID (instrument zone selector).
- `54` sample modes (no loop / continuous loop / sustain loop).
- `58` overriding root key.

Sample decoding:
- 16-bit PCM samples in `sdta:smpl[start..end]`.
- 24-bit extension chunk `sm24` is detected; if present, low 8 bits are merged
  with 16-bit samples to produce 24-bit signed integers before normalising.
- Sample-rate from `shdr.sampleRate`; root-key from `shdr.originalPitch`
  unless overridden by gen 58.
- Sample loop from `shdr.startLoop/endLoop` unless overridden by gens 2/3.

Preset selection:
- v1 imports the *first* preset (lowest `bank` then `program` ordering).
  Diagnostics enumerate all detected presets so users can see what was
  available; future versions will allow preset selection in the UI.

### DecentSampler (`.dspreset`)

`DecentSamplerImporter` parses the published XML schema:

```
<DecentSampler>
  <groups>
    <group volume="..." pan="..." trigger="...">
      <sample path="..." rootNote="60" loNote="48" hiNote="60"
              loVel="0" hiVel="127" start="0" end="..."
              loopStart="..." loopEnd="..." loopEnabled="true"
              tuning="0" volume="..." pan="..." trigger="..." />
      ...
    </group>
  </groups>
</DecentSampler>
```

Mapping:
- Each group becomes an `engine::Group`; group-level `volume`, `pan`,
  `trigger` propagate as defaults.
- Each `sample` element becomes one `engine::Zone` with a `SampleAsset`
  pointing at the resolved WAV path. Resolution checks the path relative to
  the `.dspreset` first, then a sibling `Samples/` folder, then the
  `.dspreset`'s parent folder recursively up to two levels.
- `noteName="C4"` style attributes are converted to MIDI note numbers using
  the standard (C-1 = 0) mapping.
- `loopEnabled="true"` plus a present `loopStart`/`loopEnd` produce a
  continuous loop; `trigger="release"` sets `ZoneTriggerMode::release`,
  `trigger="first"`/`"legato"`/`"normal"` falls back to gate.
- `tuning` (in semitones, fractional ok) is split into `transposeSemitones`
  and `tuneCents` on the resulting zone.

Out-of-scope for v1:
- `<effects>`, `<midi>`, `<modulators>`, `<ui>` blocks are ignored with a
  diagnostic.
- Multi-`<groups>`-blocks are flattened.

## Persistence

`ImportedProgramFormat` gains `decentSampler` and `sf2`. Detection by file
extension is added to `detectImportedProgramFormat(...)`. The processor's
`setImportedProgramMetadata(...)` writes the new tag, and
`PluginProcessor::setStateInformation(...)` routes the new tags through the
matching `importXxxProgram(...)` call so saved patches reopen the same
imported program.

## Browser

`LibraryFileIndex` recognises `.sf2` and `.dspreset` as supported instrument
files (same way it already recognises `.sfz` and `.nki`). Future tiers add
their extensions in the same place.

## Tests

Each importer is exercised by an offline test that:
1. Synthesises a minimal in-memory fixture (a tiny `.sf2` for SoundFont 2;
   a tiny `.dspreset` plus a generated WAV for DecentSampler).
2. Runs the importer.
3. Asserts zone count, key/root mapping, sample-asset binding, and one
   sustain render through `EngineCore` to prove the imported program is
   playable.

Fixtures are written to the OS temp directory and deleted after the test.

## Delivery Order

1. Plan + Tier 1 importers + tests + persistence + browser hookup (this
   delivery).
2. Tier 2 wave: Bitwig multisample, Akai MPC XPM, Logic EXS24 (each as a
   single follow-up slice with its own offline test).
3. Tier 2 polish: NN-XT, Korg KMP/KSF, TAL Sampler, 1010music preset.
4. Kontakt phase B: NCW PCM decoder for unencrypted NCW samples.
5. Tier 2 wrap-up: Ableton sampler (gzip XML), CWITEC, Korg wavestate /
   modwave, disting EX.

Each follow-up slice keeps the imported-program seam intact and adds its
test-backed importer behind the same browser/processor/editor pattern.

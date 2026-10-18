# 16 — Library Authoring (create-from-scratch) plan

Status: accepted, in progress

## 1. Problem

Audiocity's mapper today operates only on **imported** instrument programs
(`importedProgramFormat=sfz|sf2|decentSampler|bitwigMultisample|mpcKeygroup|…`).
There is no way to:

1. Start with no instrument and build one from scratch.
2. Add raw `.wav/.aiff/.ncw` samples into an editable program.
3. Save the resulting program back out as a portable library that other
   samplers (and Audiocity itself, via re-import) can read.

This document records the format we standardise on, why, and the seams we
add to support author → edit → export.

## 2. Format choice: **SFZ v2**

We standardise on **SFZ** (sfzformat.com, v2 / ARIA-compatible subset) as
Audiocity's native export format.

### Candidates evaluated

| Format | Container | Spec owner | Pros | Cons |
| --- | --- | --- | --- | --- |
| **SFZ** | ASCII text + external WAV/FLAC | Open (community spec, no single owner) | Human-readable, diff-friendly, supported by Sforzando, Sfizz, TX16Wx, HISE, Bitwig, Renoise, plogue, plus dozens of OSS tools; full RR / loop / filter / envelope / xfade coverage; no DRM | Spec has dialect drift; nothing forces vendors to read every opcode |
| DecentSampler `.dspreset` | XML + external WAV | Decidedly LLC (free, undocumented edge cases) | Modern, easy XML, popular | Player-specific; UI tags are opinionated |
| SoundFont 2 `.sf2` | RIFF binary, embedded samples | E-mu / Creative (frozen) | Self-contained, very widely supported | 16-bit/24-bit PCM only, ancient modulation model, no loop_continuous semantics, binary diff is painful |
| Bitwig multisample `.multisample` | ZIP + XML + WAV | Bitwig | Open ZIP, simple | Bitwig-centric vocabulary, no choke / RR randomisation, limited filter info |
| MPC `.xpm`, EXS24, NN-XT, Korg `.kmp/.korgmultisample`, Ableton `.adv` | Various | Vendor-specific | Native to one DAW/HW | Not portable; spec officially undocumented |
| NKI (Kontakt) | Encrypted/proprietary | Native Instruments | Ubiquitous | DRM, no spec, out of scope (see `docs/12-legacy-nki-import-plan.md`) |

### Why SFZ wins

1. **No vendor lock-in.** The spec is published openly and Audiocity already
   ships a mature `SfzImporter` (`src/engine/SfzImporter.*`) and a large
   suite of import → playback regression tests (`runSfzImport*`).
2. **Round-trippable.** Every field we expose in `audiocity::engine::Zone`
   has a direct SFZ opcode (`sample`, `pitch_keycenter`, `lokey/hikey`,
   `lovel/hivel`, `xfin_*/xfout_*`, `tune`, `volume`, `pan`,
   `offset/end`, `loop_mode/loop_start/loop_end`, `group/off_by`,
   `trigger`, `seq_mode/seq_position/seq_length`). No data loss for the
   model we already store.
3. **External samples** stay as ordinary WAVs in a sibling `Samples/`
   folder, which keeps libraries diff-able in git, swappable in a DAW
   browser, and free of any custom container we have to maintain.
4. **Inter-op upside.** A library authored in Audiocity opens unchanged
   in Sforzando, Sfizz, TX16Wx, HISE, Bitwig's Sampler, Renoise, Logic
   (via plugins), etc. We become a producer in an ecosystem we already
   consume.
5. **No DRM surface.** Nothing to maintain around encryption, key
   management, or license enforcement.

### Explicitly out of scope

* Writing SF2, NKI, DS, multisample, or any vendor-specific encrypted
  container. Users who need those go through their native authoring
  tool; we focus on the open ASCII format that round-trips cleanly.
* Embedding sample audio inside the SFZ file. SFZ has no embedded-sample
  story; we always copy/reference external WAVs.

## 3. On-disk layout produced by Audiocity

```
<destination folder>/
    MyLibrary.sfz              ← ASCII, UTF-8, LF newlines
    Samples/                   ← created only when copy-samples is on
        Sample01.wav
        Sample02.wav
        …
```

* Sample paths inside the SFZ are written relative to the SFZ file
  (`sample=Samples/Sample01.wav`). When the source sample is already
  on disk and copy-samples is off, the path is written relative to the
  SFZ if possible, else absolute.
* The SFZ header includes a banner comment with the library name and
  the producing version, plus a `default_path=Samples/` directive when
  samples were copied.

## 4. Implementation seams (this milestone)

* `src/engine/SfzExporter.{h,cpp}`
  Pure non-RT function:
  ```cpp
  ExportResult exportProgramToSfz(
      const juce::File& destSfz,
      const audiocity::engine::Program& program,
      const std::vector<juce::AudioBuffer<float>>& sampleDataByAsset,
      const ExportOptions& options);
  ```
  Returns the written file plus any diagnostic warnings (e.g. asset
  missing audio and no source path).

* `src/plugin/PluginProcessor`
  Three new editor-thread methods, all using the existing
  `importedProgramStateMutex_` + `engine_.setProgram(...)` publish path:
  * `bool createEmptyImportedSfzProgram(const juce::String& libraryName);`
  * `bool addSampleAssetToImportedProgram(const juce::File& file, juce::String& errorOut);`
  * `bool saveImportedProgramAsSfz(const juce::File& destSfz, bool copySamples, juce::String& errorOut);`

  Each one obeys the real-time rules: no allocation/IO from
  `processBlock`. File reads happen on the editor thread and the
  resulting program is published via the same atomic swap the
  importers already use.

* `src/plugin/PluginEditor`
  Three buttons in the Mapping-tab header row:
  * **New Library** — prompts for a name, replaces the imported program
    with an empty one tagged `ImportedProgramFormat::sfz`.
  * **Add Sample…** — file chooser, loads audio via
    `audio_file::openReaderForFile`, appends a `SampleAsset` plus one
    seed zone covering the detected root note, re-publishes.
  * **Save Library…** — save-mode file chooser writing `.sfz`. Default
    enables copy-samples-on-save.

## 5. Embedded WAVE sampler metadata

When a raw WAVE sample is added, Audiocity reads the MIDI unity note and its
`MidiPitchFraction` correction. It imports the first `smpl` loop only when it
is an integer, forward loop with unlimited play count. The WAVE loop end is
inclusive on disk and is converted to the program model's exclusive end.
These values seed a newly created zone; they do not replace explicit settings
on an existing mapping. Finite loops, alternate/backward loops, and additional
loops are not imported and produce a diagnostic.

## 6. Embedded AIFF sampler metadata

For AIFF/AIFC samples, Audiocity reads `INST` base note and signed detune, then
resolves sustain-loop marker IDs through `MARK`. This profile imports only
forward sustain loops (mode 1). The end marker position is treated as
exclusive, matching the libsndfile compatibility profile; this is deliberate
and tested rather than inferred from WAVE's inclusive-end rule. Missing or
ambiguous marker IDs, ping-pong/release loops, and non-default note/velocity
ranges or gain produce diagnostics instead of being silently applied.

## 7. Tests

`tests/OfflineRenderTests.cpp` gains:

* `runWaveSamplerMetadataLibraryPlaybackGoldenTest` — build a RIFF/WAVE file
  with an actual `smpl` chunk, verify root/loop metadata becomes a new zone's
  default, and render the looped zone against a fixed playback-position
  expectation.
* `runWaveSamplerMetadataRejectsUnsupportedAndMalformedLoopsTest` — verify
  unsupported loop semantics produce a warning and malformed sampler payloads
  produce an error.
* `runWaveMidiPitchFractionSetsDefaultZoneTuneTest` — verify the fractional
  root correction becomes zone tuning in cents.
* `runAiffSamplerMetadataLibraryPlaybackGoldenTest` and
  `runAiffSamplerMetadataRejectsUnsupportedAndMalformedLoopsTest` — cover
  marker-ID resolution, the exclusive end convention, playback, and failures.
* `PresetRuntimeSmoke` — exercise the actual processor asset-add path with a
  WAVE `smpl` chunk and AIFF `INST`/`MARK` chunks, then save/re-import SFZ to
  verify root, tuning, and loop metadata round-trip.
* `runSfzExporterRoundTripTest` — build a 2-zone `Program` referencing
  a synthesised WAV, export, re-import via `SfzImporter`, assert zone
  count, key/velocity ranges, root note, loop mode, gain/pan/tune,
  choke/group, RR fields preserved.
* `runProcessorCreateLibraryFromScratchTest` — drive the processor
  through `createEmptyImportedSfzProgram` → `addSampleAssetToImportedProgram` ×
  2 → `saveImportedProgramAsSfz`, then re-import the saved SFZ and
  verify zone count.

## 8. Future work (not in this milestone)

* Per-zone batch sample-replace (point an existing zone at a new file).
* Velocity-layer / round-robin assistants that auto-fill ranges given a
  set of dropped samples.
* Optional `.sfz` plus `.sfzbank` packing into a zip for distribution.
* DecentSampler `.dspreset` companion export for users who need the
  Decent Sampler player specifically.

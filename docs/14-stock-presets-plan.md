# Stock Presets Plan

## Goal

Ship at least 128 stock `.acp` presets that:

- load without any external sample dependency,
- carry their pre-synthesized source sample inside the preset file,
- exercise a broad range of Audiocity's sound-design controls,
- remain portable between Standalone and VST3,
- preserve real-time safety and existing preset backward compatibility.

## Current State

- `.acp` presets are XML `ValueTree` payloads created from processor state.
- File-backed sample presets currently persist a sample path, not embedded audio.
- Imported SFZ/REX/NKI/sample-slice programs currently persist an imported-program path plus mapping edits.
- Generated waveforms and captured audio already prove that embedded sample data in patch state is viable for single-sample sources.
- The preset browser currently scans the user preset folder and, when needed, compares full preset XML text against the live processor state to infer the current selection.

That means the shortest path to 128 stock presets is not "embedded multisample instruments". It is "embedded single-sample presets with strong parameter design".

## Recommendation

Use a phased approach:

1. Add a first-class embedded single-sample preset format.
2. Ship the first 128 stock presets on that format.
3. Keep embedded imported-program or multisample preset support out of v1 unless a later content pass proves it is necessary.

This keeps the implementation aligned with the code paths that already serialize in-memory audio while avoiding a larger imported-program persistence expansion.

## Scope

### In Scope

- Embedded single-sample preset payloads.
- At least 128 factory presets.
- A deterministic authoring pipeline for generating sample material and stamping preset parameters.
- Factory-preset discovery, browsing, and loading.
- Offline render coverage and preset-portability validation.

### Out of Scope for V1

- Embedded SFZ, REX, NKI, or other multisample program payloads.
- User-facing preset store, download, or cloud sync.
- A general content manager beyond stock preset browsing.

## Technical Design

### 1. Add an Explicit Embedded Sample Source Kind

Do not overload the current generated-waveform or captured-audio legacy fields for factory content. Add a new explicit source kind and metadata fields in patch state, for example:

- `sampleSourceKind = embeddedSample`
- `embeddedSampleCodec`
- `embeddedSampleChannels`
- `embeddedSampleRate`
- `embeddedSampleFrames`
- `embeddedSampleRootMidiNote`
- `embeddedSampleLoopStart`
- `embeddedSampleLoopEnd`
- `embeddedSampleName`
- `embeddedSampleData`

Key requirement: preset loading must prefer explicit embedded sample payloads over external `samplePath` resolution so stock presets remain portable on clean machines.

### 2. Use a Compressed Audio Payload

The current generated/captured state stores raw float bytes in XML-backed state. That proves the mechanism, but it is too expensive as the default storage strategy for 128 presets because:

- XML binary values become base64 text,
- raw float payloads waste space,
- preset refresh would otherwise touch much larger files.

Recommended default:

- Store embedded sample audio as compressed FLAC bytes when possible.
- Fall back to PCM16 WAV bytes only if FLAC encoding adds unacceptable implementation cost.

Content guidance for size control:

- Prefer mono sources for most presets.
- Keep source samples short and looped.
- Reserve stereo payloads for presets where width is part of the identity.

## Factory Preset Discovery

Do not ship the first 128 presets as ordinary mutable user files by default.

Recommended approach:

- Add a factory preset directory bundled with app resources or installed content.
- Merge that directory into the preset browser alongside the user preset directory.
- Mark factory presets as read-only in the UI.
- Keep rename/delete available only for user presets.
- Let users duplicate or resave a factory preset into their own preset folder for editing.

Fallback if installer/resource lookup becomes awkward:

- Seed the user preset folder on first run, but only as a backup plan. This is easier to implement and worse for upgrades, deduplication, and write-protection.

## Preset Browser Follow-Up

Embedded presets change the cost profile of the current selection-matching logic.

Today the editor can do a full file-content comparison between each visible `.acp` file and the live preset XML to determine the selected item. That is acceptable for small path-based presets and becomes wasteful for large embedded presets.

Plan a small browser follow-up at the same time:

- Add a stable `presetId` or `presetContentHash` property to preset state.
- Use that metadata for selection matching instead of full XML string equality.
- Cache minimal preset metadata for list rendering.
- Avoid loading every preset file body during every refresh.

This is a low-risk change with a high payoff once the preset files carry audio payloads.

## Authoring Pipeline

Do not hand-author 128 presets in the UI.

Add a deterministic offline preset-authoring tool under `scripts/` or a small utility target that:

- reads a declarative manifest of preset recipes,
- synthesizes source samples offline,
- trims, normalizes, and loops the sample when needed,
- encodes the embedded sample payload,
- stamps the full Audiocity parameter set into `.acp`,
- emits factory preset metadata such as family, tags, and description.

### Suggested Manifest Fields

- preset name
- family/category
- tags
- synthesis recipe id
- sample render settings
- root note
- trim window
- loop window
- engine parameter overrides
- macro assignments
- validation note or reference render note

### Suggested Synthesis Building Blocks

- subtractive analog-style waveforms
- FM tones
- additive bell and organ spectra
- wavetable-like harmonic motion via offline resynthesis
- filtered noise and transient layers
- short attack transients paired with looped sustain bodies

Determinism matters. The authoring tool must use fixed seeds for any randomized stage so the generated presets are reproducible and diffable.

## Content Plan: 128 Presets

Use 8 families with 16 presets each. This is enough breadth to feel like a real starter library while still being manageable to review.

### 1. Bass x16

- sub bass
- rounded analog bass
- acid bass
- picked synth bass
- growl bass
- reese-style bass
- rubber bass
- plucked mono bass
- hollow digital bass
- lo-fi bass
- muted bass
- glide bass
- filter-envelope bass
- FM bass
- dirty saw bass
- punch bass

### 2. Lead x16

- clean mono lead
- wide saw lead
- square lead
- sync-style lead
- portamento lead
- vocal lead
- bright digital lead
- soft expressive lead
- distorted lead
- bell lead
- flute lead
- nasal lead
- PWM lead
- octave lead
- filter-mod lead
- retro game lead

### 3. Pad x16

- warm analog pad
- glass pad
- choir pad
- tape pad
- shimmer pad
- dark air pad
- slow brass pad
- string pad
- motion pad
- lo-fi haze pad
- resonant pad
- organ pad
- noisy texture pad
- soft FM pad
- pulse pad
- frozen pad

### 4. Pluck and Sequence x16

- harp pluck
- synth pluck
- muted pluck
- resonant pluck
- marimba synth pluck
- glass pluck
- acid pluck
- fast arp pluck
- bright key pluck
- clicky pluck
- hollow pluck
- lo-fi pluck
- metallic pluck
- transient-rich pluck
- sine pop pluck
- PWM pluck

### 5. Keys and Electric x16

- mellow electric piano
- bright electric piano
- tine-style key
- reed-style key
- toy key
- soft keyboard
- attack key
- digital key
- FM key
- organ key
- lo-fi key
- chorus key
- bell key
- vibey key
- hybrid piano key
- muted stage key

### 6. Bell, Mallet, and Percussive Tone x16

- bell
- music box
- mallet
- tubular hit
- metallophone
- kalimba-like tone
- chime
- vibraphone-like tone
- marimba-like tone
- soft struck glass
- hard struck glass
- digital bell
- detuned bell
- noisy mallet
- cinematic ping
- low metallic thunk

### 7. Ensemble x16

- synth string
- soft string
- bright string
- brass stab
- warm brass
- mellow horn
- reed ensemble
- organ ensemble
- choir vowel
- breathy ensemble
- synth brass
- trem string
- attack string
- lo-fi choir
- stacked saw ensemble
- cinematic ensemble

### 8. Texture, FX, and Utility x16

- riser
- downer
- impact tone
- reverse wash
- noise sweep
- drone
- glitch tone
- sci-fi ping
- vinyl texture key
- tape wobble tone
- alarm tone
- pulse drone
- shimmer hit
- gated texture
- unstable texture
- neutral init-plus sample showcase

## Sound-Design Conventions

Make the bank coherent instead of 128 unrelated curiosities.

Use consistent performance mapping where possible:

- Mod wheel: brightness or motion depth.
- Aftertouch: vibrato, brightness, or gain lift on expressive presets.
- Velocity: always affects level, and often filter or attack.
- Macro 1: brightness.
- Macro 2: motion.
- Macro 3: attack or texture.
- Macro 4: space, width, or drive.

Each family should include:

- a conservative bread-and-butter subset,
- a clearly expressive subset,
- a few deliberately characterful or experimental examples that show off the engine.

## Implementation Milestones

### Milestone 1. Spec and Format

- Write the embedded-preset spec.
- Add versioned state fields for embedded sample payloads.
- Preserve backward compatibility with existing `.acp` files.
- Define size budgets and preset metadata rules.

Exit criteria:

- a preset can round-trip with an embedded sample and no external file dependency,
- old path-based presets still load unchanged.

### Milestone 2. Loader and Saver Support

- Extend save/load paths for embedded sample state.
- Add source-kind precedence rules.
- Keep all new decode work on non-audio paths only.

Exit criteria:

- loading an embedded preset on a machine with no referenced sample files still succeeds,
- audio-thread code remains untouched except for already-published runtime structures.

### Milestone 3. Factory Browser and Packaging

- Add factory preset discovery.
- Distinguish factory vs user presets in the UI.
- Replace full-XML selection matching with metadata-based matching.
- Package factory preset files with Standalone and VST3 artifacts.

Exit criteria:

- the preset browser shows at least 128 stock presets,
- rename/delete protections work as intended,
- refresh remains responsive.

### Milestone 4. Authoring Tool

- Add recipe manifest support.
- Add deterministic offline synthesis and preset emission.
- Generate the initial 128 presets from recipes, not manual UI steps.

Exit criteria:

- regenerating the bank from source recipes produces stable outputs,
- preset diffs are attributable to recipe changes rather than manual drift.

### Milestone 5. Content Tuning Pass

- Tune the 128 presets for loudness consistency, macro usefulness, and category balance.
- Review loop points, release tails, velocity behavior, and modulation depth.
- Remove weak duplicates.

Exit criteria:

- every preset has a clear role,
- no family is padded by near-identical variants,
- the bank feels intentionally curated.

### Milestone 6. Validation and Release Gate

- Add serialization and load smoke tests.
- Add offline render coverage.
- Add golden or reference-render tests for representative presets.
- Add size-budget checks.
- Update user-facing docs.

Exit criteria:

- the bank is reproducible,
- the shipped preset count is at least 128,
- validation passes in CI.

## Validation Plan

Because Audiocity requires offline render and golden coverage for risky changes, validate at multiple levels.

### Automated Tests

- Embedded preset serialization round-trip.
- Embedded preset load on a clean temp environment with no external sample files.
- Preset count check for factory bank discovery.
- Non-silent render smoke for all 128 presets.
- Golden or reference render tests for at least one representative preset per family.
- Total installed bank size budget.

### Suggested Coverage Shape

- 128-preset smoke render pass.
- 8 to 16 golden presets covering the most different families and modulation styles.
- UI snapshot updates only if factory/user preset browsing changes the visible chrome.

## Risks and Mitigations

### Risk: Preset files become too large

Mitigation:

- use compressed audio payloads,
- bias toward short looped mono sources,
- enforce per-preset and total-bank size budgets.

### Risk: Preset browser slows down

Mitigation:

- replace full-file XML equality checks with preset IDs or content hashes,
- cache lightweight metadata.

### Risk: Scope creeps into embedded multisample content

Mitigation:

- explicitly keep v1 on single-sample presets,
- defer embedded imported-program content to a separate plan.

### Risk: Manual content creation becomes inconsistent

Mitigation:

- generate the bank from declarative recipes,
- use shared macro and gain conventions,
- require a content review pass before freeze.

## Recommended First Slice

Build the smallest end-to-end slice before generating the whole bank:

1. Add embedded single-sample preset state support.
2. Add one factory-preset directory and browser plumbing.
3. Generate 8 pilot presets, one from each family.
4. Add smoke and golden coverage for those pilots.
5. Scale the manifest and generation pass up to 128 once the format and UX hold up.

That sequence keeps the repo compileable after each milestone and avoids mass-producing content on top of an unproven format.
# Audiocity UX design review

September 7, 2026 · Repository revision `b331a32` · Design recommendation, not an implementation change

**Recommendation: make Audiocity a focused, expressive sampler with a synthesis workflow. Stop treating library authoring as an equal product pillar.** The strongest promise is: **“Turn any sound into an instrument.”**

The essential loop should be **find or create a sound → shape it → play it → save it**. Nearly everything worth keeping already contributes to that loop. The problem is that the interface currently gives technical configuration, content administration, and musical controls similar prominence, while splitting related actions across several pages.

## Review basis and limits

This review combines visual inspection of the committed snapshots, fresh offscreen renders from the existing September 6 Release snapshot executable, and inspection of the editor, page layouts, preset handling, browser preview, and waveform-generation code. The fresh run exported all 19 scenarios into `artifacts/ux-review-2026-09-07/snapshots`. Representative fresh Sample, wide Sample, Mapping, and tall Sample views were inspected directly. The other workflow pages were inspected through committed screenshots and their implementation.

This is an expert heuristic review, not an observed user study or live MIDI/audio test. Native app interaction was not available through the current computer-control tools. No claims about measured task times, sound quality, latency, screen-reader behavior, or user failure rates are made. The snapshot harness seeds source, modulation, and active-note states; those are not evidence of first-launch defaults. Fresh PNG hashes differ from the baselines; the visible preset total is one difference, and this review does not assert pixel equivalence. Application code and baseline images were not changed.

The user guide provides useful workflow context but contains older descriptions, including tab counts and preset portability. Current source and fresh render evidence take precedence.

## 1. Choose the product you want to be

Audiocity currently presents elements of four products:

| Product identity | Present surfaces | Recommended role |
| --- | --- | --- |
| Playable sampler / sample-based synth | Sample, envelopes, filter, effects, MIDI expression | Primary product |
| Loop chopper / pad instrument | Slice markers, zones, Player pads | Focused secondary workflow within the instrument |
| Sound acquisition tool | Generate and Capture | Ways to create the source for the same instrument |
| Library authoring / conversion tool | Mapping, New Library, batch zone operations, export, tag editing | Withdraw from the main experience; freeze expansion |

The working audience should be a producer or musician who knows what a filter, envelope, pitch, and loop do, but does not want to manage a sampler's internal data model. Approachable need not mean fewer useful synthesis parameters. It means familiar musical operations are easy to locate, hear, and undo.

Three plausible directions are available. A library workstation would require investing more heavily in mapping, organization, and export and would compound the concern that prompted this review. A full synthesizer would introduce expectations around continuous oscillator editing and modulation that the current Generate-and-commit workflow does not meet. **The expressive sampler uses the current strengths with the least new scope.**

Keep multisample playback and existing format support. Being able to open an instrument is valuable even for someone who never wants to build its velocity layers. Remove the obligation to understand those layers before playing it.

## 2. Highest-impact findings

Priority describes recommended design order, not measured defect frequency. “Observed” refers to reviewed images; “source-confirmed” refers to code paths. Consequences are expert assessments to validate with users.

### A. Musical controls lose the first screen — critical

**Observed:** At the default 980 × 860 size, the reviewed Sample state shows navigation, preset administration, waveform, information, a Program Map header, voice/pitch controls, keyboard, meters, and pads. The amplitude envelope and filter are below the visible editing area. At 1240 × 1600, the filter still sits near the bottom.

**Source-confirmed:** The main stack places sample information and mapping before Performance, then the expressive/macro modulation panel, trim/loop dials, amplitude envelope, and filter (`PluginEditor.cpp`, approximately 10050–10175).

**Why it matters:** Loading a sound and adjusting its attack, release, or brightness is the central sampler interaction. Making these controls harder to reach than file size or modulation routing reverses the product's priorities.

**Recommendation:** Guarantee that waveform, playback behavior, root/tuning, amp envelope, filter, and output fit together at the supported default size. Move detailed information to an Info disclosure. Put route editing on demand. Use freed space to make labels and graphs readable rather than adding more controls.

### B. The wide layout can be less usable than the narrow layout — critical

**Observed:** In the three-column Sample render, only part of the pitch/performance control set remains legible. Root note and several pitch controls shown in the default layout are no longer available in that row. The tall three-column render does not recover the amp graph beside its fixed-width controls.

**Source-confirmed:** The Performance and Amp rows allocate fixed-width children from the remaining center-column rectangle. Meanwhile, metadata, output, effects, and filter-modulation cards change locations according to width, height, and expansion state (`PluginEditor.cpp`, 9450 onward and 10070 onward).

**Why it matters:** Users build spatial memory. A resize should not hide a useful control, detach a filter from its envelope, or change where effects live.

**Recommendation:** Use one stable Sound surface. At smaller widths, open the browser as an overlay; at larger widths, optionally dock it. Resize the waveform and reflow whole groups before shrinking targets. Keep output in the header and each shaping section in a consistent place. Eliminate automatic card relocation based on available height.

### C. Seven peer tabs describe implementation areas rather than a musical journey — high

**Observed/source-confirmed:** Sample, Library, Mapping, Player, Generate, Capture, and About are top-level peers. Browsing also exists as a rail; playing also exists as a persistent strip.

**Recommendation:** Remove the seven-tab navigation. Use a persistent instrument shell with a default **Sound** view and an on-demand **Modulation** view. Browse opens one browser. Record and Generate open contextual source workflows. Mapping becomes an advanced command for an imported instrument. About belongs under the application menu. A larger performance view is worthwhile only if performance becomes a demonstrated primary use case.

This is a change in product hierarchy, not seven renamed tabs.

### D. Performance furniture crowds out performance controls — high

**Observed:** The default Sample footer devotes roughly 210 pixels, about a quarter of the editor height, to status, a full-range tiny keyboard, and eight pads. The Player page makes these larger but exposes little of the sound-shaping interface.

**Recommendation:** Use a single compact audition surface: keyboard for pitched material, pads for slices. Offer an explicit Keyboard/Pads/Hidden choice and remember it. A practical visible octave range with octave controls is preferable to fitting nearly the entire MIDI range into tiny keys. Keep MIDI activity, output, and All Notes Off available when the keyboard is hidden. Shrink the red voice counter into optional diagnostic status; it visually competes with more important musical feedback.

Do not add a separate live-performance product yet. If requested later, it should center on named macros, predictable patch changes, and readable triggering rather than a largely empty page of enlarged pads.

### E. Modulation exposes the routing machinery before the result — high

**Observed:** “MW Pitch,” “AT Filt,” “M1 Amp,” and related dials occupy large sections, supplemented by per-source strips and destination summaries. Two macro values sit among the controls that configure those macros.

**Recommendation:** Put the two existing macro values where they can be played. Allow musical labels, such as Brightness and Motion, when those labels accurately describe that preset's routes. Show assignment details on demand. Give the Modulation view an active-route list: **source → destination → bipolar amount**, plus Add Route. Editing a destination should reveal its contributing sources.

Do not invent a universal modulation engine as part of this work. First present the currently supported source/destination pairs more clearly. Preserve existing LFOs and automation. A macro routed to cutoff cannot honestly become a “Space” knob unless effects routing is implemented. Rename display labels without changing stored host parameter identifiers.

### F. “Library,” “program,” “sample,” and “preset” blur different objects — high

**Observed/source-confirmed:** Library is a folder browser; New Library and Save Library on Mapping create/export a playable program. Sample has a separate preset management strip. Mapping has Apply Zone, whereas synthesis controls operate directly.

**Recommendation:** Use these product terms consistently:

| User-facing term | Meaning |
| --- | --- |
| Sound / preset | The complete recalled instrument and its settings |
| Source | Audio or an imported instrument used by that sound |
| Sample | One audio asset |
| Instrument | Playable mapped content, possibly containing many samples |
| Browser | A place to find presets, samples, and imported instruments |
| Mapping | Optional advanced editing of key/velocity assignments |
| Export Instrument | A conversion/export operation, separate from Save Preset |

The internal Program/Zone vocabulary can remain. Present one clearly named current sound in the header, a separate source identity near the waveform, and explicit scope when edits affect a selected sample/zone versus the whole instrument. Do not offer apparently global loop/root controls that silently operate on only part of an imported instrument.

### G. Browsing mixes discovery with collection maintenance — medium/high

**Observed:** Search, sort, Favorite, Favorites, Recent, bookmarks, and tag editing compete in the full Library surface. Its empty state is a blank list and “No folder selected.” The preset picker has a separate search system.

**Source-confirmed:** Raw samples preview on selection; imported instruments return without preview (`previewSampleFromBrowserRow`, 9404 onward).

**Recommendation:** One browser with explicit Presets / Samples / Instruments scopes, search, recent items, and per-item favorites. A single selected-item preview with play/stop and level belongs at the bottom. Label items that require loading to audition; do not imply identical preview behavior for every supported format. Keep sample replacement distinct from loading a complete preset so it is clear whether shaping settings are retained.

Make Factory Presets immediately useful before a folder is configured. Empty user content should offer **Add Folder** and a drop target. Put rescanning, folder management, tag maintenance, and external-file details in secondary menus. Remove bulk tag editing from the first redesign.

### H. Generate and Capture feel like separate applications — medium/high

**Observed/source-confirmed:** Both use their own workspace and “Load as Sample” transfer. Generate commits a buffer and switches to Sample. The persistent keyboard can therefore address a different sound from the waveform being prepared; the distinction needs clearer presentation, even though preview state exists.

**Recommendation:** Keep these capabilities. Bring them into the source area as **Generate Waveform** and **Record Sample**. Present an explicit temporary-source state with Preview, Use Sample, and Cancel. Use Sample returns to the same Sound layout and clearly replaces the source. During recording, prioritize source routing, input level, recording state, and elapsed time; after recording, prioritize selection and Use Sample. Move bit depth, forced sample rate, and buffer length under advanced source settings.

For a first pass, retain the explicit commit action. Continuous, playable oscillator editing is a separate engine/state project, not something solved by relabeling Generate as Synth. In the VST3, explain host-routed input only when needed; in standalone, offer device setup where it can solve a missing-input problem.

### I. Saving and recovery need to earn trust — high

**Source-confirmed:** `promptSavePreset` presents a file chooser but passes only the selected base name to `savePreset`, which writes using `presetFileForName`. The selected directory is discarded. A preset-load failure offers Delete Preset / Keep Preset as its next decision (`PluginEditor.cpp`, 10920–11070).

**Recommendation:** Either save to the exact selected destination or use a simple named Save Preset dialog that explicitly saves to the user bank. Put an edited indicator beside the sound name. Define separate Save, Save As, and Export behaviors. Make load/replace recoverable, ideally with a retained previous sound; use confirmation only where recovery cannot cover a meaningful loss. A load failure should retain the current playable sound and offer details, retry, or missing-file repair, not deletion as the primary recovery action.

Existing editor undo is valuable, but should not be assumed to recover all preset loads or source replacements. Audit and test those transitions specifically. Likewise, embedded single-sample presets do not establish that every imported multi-asset program is self-contained. Explain dependencies and preserve existing relinking support until complete packaging is demonstrated.

### J. The visual language rewards density — medium

**Observed/source-confirmed:** Many identical knob rings and boxed values receive similar emphasis; labels are heavily abbreviated. `CcLearnDial` uses 10.5-point labels and 13-pixel value boxes. The waveform has a long instruction line; trim/loop frame counts appear in multiple places. Output mixes master level with fades, preload, and playback quality.

**Recommendation:** Retain the dark neutral character and cyan waveform, but establish a deliberate hierarchy: source waveform first, amp/filter second, supporting settings third. Prefer spacing over a border around every group. Use graphs plus compact numeric controls for envelopes, waveform handles for regions, and a few well-sized knobs for continuous musical controls. Put root/tuning near the source, fades near waveform boundaries, and preload/quality in Settings. Present milliseconds/seconds and musically meaningful values by default, with exact sample frames available on demand. An output level expressed in dB can be a presentation conversion while retaining the existing parameter mapping.

Use “Vibrato rate,” “Vibrato depth,” “Mod wheel,” “Pressure,” “Cutoff,” and “Resonance” where space allows; preserve precise units and familiar terms such as ADSR. Do not replace expert language with vague labels like Magic or Character unless a concrete musical behavior supports the name.

Measure contrast and verify focus order, numeric entry, reset, fine adjustment, MIDI learn, non-color selection cues, and high-DPI scaling during implementation. Tiny controls are a concern; this review does not claim a formally measured accessibility failure.

## 3. Proposed interface

The default instrument window should contain:

1. **Header:** Audiocity, Browse, current preset name and edited state, previous/next, Save, menu, master output/meter. Global sound identity stays visible during source and modulation work.
2. **Source:** source name/type, replace/import action, Record/Generate entry, waveform. Playback choices sit beside the waveform. Root note and tuning sit directly below it. Region and loop precision controls appear when selected.
3. **Sound:** amp envelope, filter with its envelope relationship, and compact effects. All basic shaping is accessible without scrolling the main page. Details expand within the relevant section.
4. **Expression:** the two current macro values with useful names; assignment is separate from performance.
5. **Audition:** one compact keyboard or slice-pad row, with MIDI activity and All Notes Off. Browser and advanced route editing are temporary surfaces.

Prefer **Pitched** and **Slices** as contextual editing tasks, not new engine playback modes. Existing **Gate / One-shot / Loop** semantics remain explicit within the relevant task. One-shot must not silently become monophonic simply because another product works that way. Imported instruments retain their original mapping; entering Slices must not implicitly destroy it. Converting to slices is an explicit, reversible operation.

For imported multisamples, source content can show an instrument summary and mapped range instead of a misleading single whole-instrument waveform. Advanced editing opens with the selected zone clearly identified. A user who only loads a piano should never have to visit that editor.

The accompanying concept illustrates this hierarchy and contextual disclosure. It is a visual prototype with example content, not a playable replacement UI or a commitment to new synthesis behavior.

## 4. What to keep, relocate, and retire

| Capability | Decision | Reason / condition |
| --- | --- | --- |
| Sample import, drag/drop, waveform editing | Keep prominent | The central sampler interaction |
| Factory/user presets, search, recent items, favorites | Keep and unify | Fast access to useful sounds |
| Amp/filter envelopes, filter, existing FX | Keep; elevate basic controls | Musical shaping is the core product |
| MIDI learn, host automation, expression, two macros | Keep; separate playing from assignment | Expert power without permanent routing clutter |
| Slicing and pad triggering | Keep as a contextual task | Strong transformation from loop to instrument |
| Imported multisample playback and existing readers | Keep compatible | Broad input support need not imply a complex UI |
| Record and generated/sketched waveforms | Keep as source workflows | Distinctive input paths feeding the same instrument |
| Basic mapping repair, root/range/gain | Advanced editor only | Useful for correcting imported material |
| New Library, exhaustive zone/RR/velocity batch authoring | Retire from main UI; freeze expansion | Optimizes for content authors rather than sound makers |
| SFZ/DecentSampler export | Advanced Export command initially | Preserve an escape route for existing authored work |
| Bulk tagging and bookmark administration | Remove from main workflow | Low musical return for visible complexity |
| Dedicated Library, Player, About tabs | Remove as peer destinations | Consolidated browser, audition strip, and app menu replace them |
| Preload, stream counters, detailed file metadata | Settings / diagnostics / Info | Troubleshooting data should appear when useful |

“Retire” does not mean strip stored data or remove readers from old sessions. First remove the surface and stop new development. After migration/recovery coverage exists, delete unused authoring UI code if the simpler product still meets actual needs. Do not build a separate librarian edition merely to preserve every past investment.

## 5. Interaction contracts worth deciding before implementation

- **Select versus load:** sample preview has a distinct state and level; loading a preset replaces a sound, while replacing a sample has an explicit policy for retaining settings. The currently loaded item remains identifiable while browsing.
- **Edit scope:** sound-level effects/envelopes versus selected-zone regions must never be ambiguous. Unsupported controls are explained or replaced by a relevant instrument view.
- **Reversibility:** define recovery for preset changes, source replacement, record/generate commit, reslicing, and zone edits. A continuous drag should be one meaningful undo action.
- **Sound while changing state:** imports display progress and cancel; failures leave a playable prior state. Decide and test held-note behavior during a successful replacement. UI relocation alone cannot guarantee uninterrupted sound.
- **Keyboard focus:** typing in search or a value must not play notes or trigger destructive shortcuts. Returning from a popup should restore a useful focus target.
- **Stored state:** UI layout choices and temporary browser preview must not accidentally become destructive sound edits. Preserve host parameter IDs, value ranges, old presets, and project restore.

## 6. Reference patterns, not a feature-shopping list

Ableton's official Simpler documentation describes Classic, One-Shot, and Slicing workflows with controls adapted to the selected task. The useful lesson is to reveal controls according to musical intent; Audiocity need not copy its engine semantics or add warping. [Ableton Live instrument reference](https://www.ableton.com/en/manual/live-instrument-reference/).

TAL-Sampler's official manual separates sample-preview enablement, play/stop, and preview volume in its browser. This supports a clearer distinction between auditioning content and changing the playable instrument. Its focused sample-based synthesis identity is a useful reference, without requiring Audiocity to copy its layering or vintage emulation. [TAL-Sampler manual](https://tal-software.com/downloads/docs/TAL-Sampler-UserManual.pdf).

These are pattern references inspected during this review, not comparative usability tests or purchase recommendations.

## 7. Delivery order and acceptance criteria

**First: fix the existing priorities and trust problems.** Put amp/filter before routing and information; replace the large permanent keyboard-plus-pads footer with one compact surface; remove Rename/Delete/Tech from the preset bar; fix clipped controls and preset destination handling. These changes can deliver value before the complete navigation redesign.

**Second: consolidate the shell.** Introduce the stable Sound surface, one browser, persistent sound identity, and contextual source workflows. Move authoring and export behind an advanced entry. Keep existing state and engine paths wherever practical. Audit parameter scope for imported instruments before presenting a unified editor.

**Third: refine expression and direct editing.** Expose two named macro values, active routes, clear waveform edit modes, and local numeric precision. Improve source replacement recovery and missing-asset handling. Retire unused authoring UI after compatibility checks.

**Fourth: validate the product with musicians.** Use five to eight people who understand music tools but do not know Audiocity. This is a formative sample for finding problems, not a statistically representative benchmark. Include a keyboard player, a loop/pad user, and someone working in a DAW plugin window.

| Test task | Proposed acceptance target, not a measured result |
| --- | --- |
| First usable sound | Find and audition a factory sound without setting up a sample folder; target under 30 seconds after audio is ready |
| Turn a sample into a playable patch | Load/drop, set root, adjust amp and cutoff, and save without leaving Sound or scrolling the main editing surface |
| Edit a loop | Discover loop handles, set a smooth boundary, hear it, and undo without consulting a manual |
| Slice a phrase | Find slicing from the waveform, trigger slices on pads, edit a boundary, and undo without encountering RR/velocity-layer fields |
| Import an instrument | Load and play a multisample; identify what sound-level controls affect; no mandatory mapping visit |
| Build from generated/recorded audio | Clearly identify preview versus current sound, then commit or cancel without losing work |
| Recover an experiment | Restore the previous sound after preset/source replacement, including a failed load |
| Resize and reopen | Critical controls remain visible at the minimum supported size; no meaningful controls silently move between unrelated sections |

Start layout evaluation at the actual 980 × 860 default, then test a shorter laptop/DAW-friendly target such as 1000 × 720 if supported, wide windows, and Windows 125/150/200% display scaling. Large 1600-pixel-tall snapshots must not be the only views where basic synthesis fits.

Record time, wrong turns, requests for help, and whether people can explain what is currently sounding. Ask which visible controls they ignored. Compare the current interface and prototype using equivalent tasks and alternate the order. The deciding outcome is fewer interruptions to making a sound, not merely fewer controls on screen.

For engineering validation, add focused coverage for preset destination, state recovery, host parameter compatibility, and layout reachability. Existing screenshot baselines are valuable but can faithfully preserve a poor layout. Require human review of musical task completion before accepting new baselines.

## Decision

Make the next milestone a **sampler simplification release**. Pause new import formats, library-authoring expansion, additional synthesis engines, and new performance modes while the core workflow is consolidated. Preserve existing sound compatibility and the musical depth already present. The largest opportunity is to make that depth easy to reach from one coherent instrument.

## Implementation follow-through

The [sampler simplification implementation](SAMPLER_SIMPLIFICATION_2026-09-07.md) records the resulting shell, interaction contracts, compatibility boundaries, local validation and remaining limits. The [user guide](USER_GUIDE.md) and current screenshot baselines describe the implemented experience; this review remains the original design brief.

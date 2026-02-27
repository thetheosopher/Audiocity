# Sampler-style UI — Audiocity

## Panels
1. **Browser**: search, tags, favorites, watched folders, preview playback + waveform.
2. **Mapping**: keyboard grid + velocity lanes + RR groups + zone list.
3. **Editor**: sample trim, start/end, loop points, fades, reverse (v1 baseline).
4. **Settings**: audio device, MIDI devices, buffer size, quality tier.
5. **Diagnostics**: SFZ import warnings/errors, missing samples, perf counters.

## UX invariants
- Drag/drop samples (and SFZ) into mapping.
- Import never crashes; unknown SFZ opcodes are ignored but reported.

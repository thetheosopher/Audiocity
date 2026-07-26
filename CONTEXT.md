# Domain glossary — Audiocity

Names used in code, docs, and architecture discussion. Prefer these words; if a new concept
earns a name, add it here rather than inventing a synonym at the call site.

## Instrument content

- **Program** — a playable instrument: a set of zones plus the sample assets they reference.
  Owned as `audiocity::engine::Program`.
- **Zone** — one mapped region of a program: key range, velocity range, root note, sample
  window, loop points, gain, pan, round-robin and trigger behaviour.
- **Sample asset** — one decoded audio source a zone can point at, plus its metadata
  (source path, length, channels, sample rate, root note).
- **Sample data** — the decoded audio buffers for a program's sample assets, indexed by
  asset. Kept alongside the program rather than inside it.
- **Mapping** — the zone layout of a program: what the user edits on the Mapping page.
- **Slice** — a zone derived from a transient or REX marker rather than from a mapped key
  range.

## Imported instruments

- **Imported program** — the program currently loaded from an external instrument file
  (SFZ, SF2, NKI, DecentSampler, and the multisample formats). Distinct from the single
  sample loaded on the Sample page.
- **Imported program format** — which external format an imported program came from.
- **Imported program store** — the module that owns the imported program, its sample data,
  its derived state, and the publish to the engine. The only writer of that state.
- **Derived state** — values recomputed from the program after every change: map summary,
  zone rows, zone count. Never edited directly.
- **Edit outcome** — what a mutation reports back: whether it succeeded, the index it
  produced (a new zone, for example), the diagnostic label to record, and any sample data
  to append.

## Playback

- **Program sink** — the destination a program is published to once it becomes current.
  The engine adapter silences sounding voices before adopting it.
- **Snapshot** — an immutable copy of program metadata or program audio that the audio
  thread reads without locking.
- **Voice** — one sounding note, drawn from a fixed-size pool.
- **Publish** — make a program current for playback.

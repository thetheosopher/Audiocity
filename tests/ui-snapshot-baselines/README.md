# UI Snapshot Baselines

This directory stores the committed reference PNGs for the deterministic UI snapshot harness.

## Comparison tolerance
`scripts/compare_ui_snapshots.ps1` tolerates text anti-aliasing drift between machines and GPU/font configurations: at most 250 differing pixels, a differing-pixel ratio of at most 0.0003, and a per-channel delta of at most 96 per image. Layout or colour changes exceed these limits and still fail. Override with `-MaxDifferentPixels`, `-MaxDifferentRatio` and `-MaxChannelDelta`.

## Refresh workflow
- Review the newly exported snapshots locally first.
- If the UI change is intentional, refresh the committed baseline set with:

```powershell
pwsh -File scripts/export_ui_snapshots.ps1 -UpdateBaseline -BaselineDir tests/ui-snapshot-baselines/current
```

- Commit the updated PNGs plus `snapshot-manifest.json` from `current/` with the UI change that required them.
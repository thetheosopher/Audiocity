# UI Snapshot Baselines

This directory stores the committed reference PNGs for the deterministic UI snapshot harness.

## Refresh workflow
- Review the newly exported snapshots locally first.
- If the UI change is intentional, refresh the committed baseline set with:

```powershell
pwsh -File scripts/export_ui_snapshots.ps1 -UpdateBaseline -BaselineDir tests/ui-snapshot-baselines/current
```

- Commit the updated PNGs plus `snapshot-manifest.json` from `current/` with the UI change that required them.
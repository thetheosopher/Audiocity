Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host '==> Configure (preset: ci-windows)'
cmake --preset ci-windows

Write-Host '==> Build plugin targets (Audiocity_All)'
cmake --build --preset ci-windows --target Audiocity_All

Write-Host '==> Build offline tests (audiocity_offline_tests)'
cmake --build --preset ci-windows --target audiocity_offline_tests audiocity_preset_runtime_smoke

Write-Host '==> Run tests (Debug config)'
ctest --preset ci-windows --exclude-regex '^audiocity_ui_snapshot_harness$'

Write-Host '==> Bootstrap complete'

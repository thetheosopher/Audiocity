Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host '==> Configure (preset: default)'
cmake --preset default

Write-Host '==> Build plugin targets (Audiocity_All)'
cmake --build --preset default --target Audiocity_All

Write-Host '==> Build offline tests (audiocity_offline_tests)'
cmake --build --preset default --target audiocity_offline_tests

Write-Host '==> Run tests (Debug config)'
ctest --test-dir build -C Debug --output-on-failure

Write-Host '==> Bootstrap complete'

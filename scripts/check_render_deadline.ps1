param(
    [string]$BuildDir = 'build/ci-windows',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [ValidateRange(0.1, 60.0)]
    [double]$SecondsPerTier = 2.0,
    [ValidateRange(0.1, 1000.0)]
    [double]$MinimumRealtimeFactor = 1.0,
    [string]$OutputPath = 'artifacts/performance/render-deadline.csv'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}
$resolvedOutputPath = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    [System.IO.Path]::GetFullPath($OutputPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
}
$outputDirectory = Split-Path -Parent $resolvedOutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$profileExecutable = Join-Path $resolvedBuildDir "tests\$Configuration\audiocity_engine_profile.exe"
if (-not (Test-Path -LiteralPath $profileExecutable -PathType Leaf)) {
    throw "Engine profile executable was not found: $profileExecutable"
}

$rows = @()
foreach ($quality in @('cpu', 'fidelity', 'ultra')) {
    $line = (& $profileExecutable --quality $quality --seconds $SecondsPerTier --block-size 128 --voices 24 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Engine profile failed for $quality with exit code $LASTEXITCODE"
    }
    Write-Host $line
    if ($line -notmatch 'realtimeFactor=([0-9]+(?:\.[0-9]+)?)x') {
        throw "Could not parse realtime factor from: $line"
    }

    $factor = [double]$Matches[1]
    $rows += [pscustomobject]@{
        timestampUtc = [DateTimeOffset]::UtcNow.ToString('o')
        configuration = $Configuration
        quality = $quality
        blockSize = 128
        voices = 24
        realtimeFactor = $factor
        minimumRealtimeFactor = $MinimumRealtimeFactor
    }
    if ($factor -lt $MinimumRealtimeFactor) {
        throw "Render deadline missed for ${quality}: ${factor}x < ${MinimumRealtimeFactor}x"
    }
}

$rows | Export-Csv -LiteralPath $resolvedOutputPath -NoTypeInformation
Write-Host "Render-deadline measurement written to $resolvedOutputPath"

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ''
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value '### Render deadline'
    foreach ($row in $rows) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY `
            -Value "- $($row.quality): $($row.realtimeFactor)x real time at 24 voices / 128 samples"
    }
}

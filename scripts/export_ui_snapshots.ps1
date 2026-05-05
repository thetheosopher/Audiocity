param(
    [string]$OutputDir = '',
    [string]$BaselineDir = '',
    [switch]$UpdateBaseline,
    [int]$MaxDifferentPixels = 0,
    [double]$MaxDifferentRatio = 0.0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)

    Write-Host "==> $Message"
}

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Get-Sha256Hash {
    param([string]$Path)

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)

    try {
        $hashBytes = $sha256.ComputeHash($stream)
    }
    finally {
        $stream.Dispose()
        $sha256.Dispose()
    }

    return ([System.BitConverter]::ToString($hashBytes)).Replace('-', '').ToLowerInvariant()
}

function Invoke-External {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Reset-Directory {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Path $Path | Out-Null
}

function Resolve-ToolPath {
    param(
        [string]$CommandName,
        [string[]]$Candidates
    )

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "$CommandName was not found. Install Visual Studio 2022 with CMake tools or add $CommandName to PATH."
}

function Resolve-WorkspacePath {
    param(
        [string]$PathValue,
        [string]$WorkspaceRoot
    )

    if ($PathValue -eq '') {
        return ''
    }

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $PathValue))
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build'
$defaultBaselineDir = Join-Path $repoRoot 'tests/ui-snapshot-baselines/current'
$resolvedOutputDir = if ($OutputDir -ne '') {
    Resolve-WorkspacePath -PathValue $OutputDir -WorkspaceRoot $repoRoot
} else {
    Join-Path $buildRoot 'ui-snapshots'
}
$resolvedBaselineDir = if ($BaselineDir -ne '') {
    Resolve-WorkspacePath -PathValue $BaselineDir -WorkspaceRoot $repoRoot
} elseif ($UpdateBaseline -or (Test-Path -LiteralPath $defaultBaselineDir)) {
    $defaultBaselineDir
} else {
    ''
}
$compareScript = Join-Path $PSScriptRoot 'compare_ui_snapshots.ps1'

$cmake = Resolve-ToolPath 'cmake.exe' @(
    'C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    'C:/Program Files/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
)

Write-Step 'Configure default Debug tree'
& $cmake --preset default
if ($LASTEXITCODE -ne 0) {
    Write-Step 'Retry configure after clearing stale default-config state'
    foreach ($path in @(
        (Join-Path $buildRoot 'CMakeCache.txt'),
        (Join-Path $buildRoot 'CMakeFiles')
    )) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }

    Invoke-External $cmake @('--preset', 'default')
}

Write-Step 'Build UI snapshot harness'
Invoke-External $cmake @('--build', '--preset', 'default', '--config', 'Debug', '--target', 'audiocity_ui_snapshot_harness')

$harnessExe = Join-Path $buildRoot 'tests/Debug/audiocity_ui_snapshot_harness.exe'
if (-not (Test-Path -LiteralPath $harnessExe)) {
    throw "Snapshot harness executable not found at '$harnessExe'."
}

Write-Step 'Probe harness startup'
Invoke-External $harnessExe @('--smoke-exit')

Write-Step 'Export UI snapshots'
Reset-Directory $resolvedOutputDir
Invoke-External $harnessExe @('--output-dir', $resolvedOutputDir)

$pngFiles = Get-ChildItem -LiteralPath $resolvedOutputDir -Filter '*.png' | Sort-Object Name
if ($pngFiles.Count -eq 0) {
    throw "No PNG snapshots were generated in '$resolvedOutputDir'."
}

$manifest = foreach ($file in $pngFiles) {
    [ordered]@{
        fileName = $file.Name
        relativePath = $file.Name
        sizeBytes = $file.Length
        sha256 = Get-Sha256Hash $file.FullName
    }
}

$manifestPath = Join-Path $resolvedOutputDir 'snapshot-manifest.json'
$summaryPath = Join-Path $resolvedOutputDir 'snapshot-summary.md'
$galleryPath = Join-Path $resolvedOutputDir 'index.html'

$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$summaryLines = @(
    '# UI Snapshot Export',
    '',
    "- Output directory: $resolvedOutputDir",
    "- Harness: $harnessExe",
    "- Snapshot count: $($pngFiles.Count)",
    '- Open `index.html` from the artifact bundle for a quick visual gallery.',
    '',
    '| File | Bytes | SHA256 |',
    '| --- | ---: | --- |'
)

foreach ($entry in $manifest) {
    $summaryLines += "| $($entry.fileName) | $($entry.sizeBytes) | $($entry.sha256) |"
}

Set-Content -LiteralPath $summaryPath -Value $summaryLines -Encoding utf8

$galleryCards = foreach ($entry in $manifest) {
@"
        <article class="card">
            <h2>$($entry.fileName)</h2>
            <img src="$($entry.relativePath)" alt="$($entry.fileName)" loading="lazy" />
            <p>SHA256: <code>$($entry.sha256)</code></p>
        </article>
"@
}

$galleryHtml = @"
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Audiocity UI Snapshots</title>
    <style>
        body {
            margin: 0;
            font-family: "Segoe UI", sans-serif;
            background: #101316;
            color: #f2f4f5;
        }

        main {
            max-width: 1600px;
            margin: 0 auto;
            padding: 24px;
        }

        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
            gap: 20px;
        }

        .card {
            background: #171b20;
            border: 1px solid #26303a;
            border-radius: 16px;
            padding: 16px;
            box-shadow: 0 18px 40px rgba(0, 0, 0, 0.24);
        }

        h1, h2 {
            margin-top: 0;
        }

        img {
            width: 100%;
            height: auto;
            display: block;
            border-radius: 10px;
            border: 1px solid #2f3944;
            background: #0b0e11;
        }

        code {
            word-break: break-all;
        }
    </style>
</head>
<body>
    <main>
        <h1>Audiocity UI Snapshots</h1>
        <p>Generated by <code>scripts/export_ui_snapshots.ps1</code>.</p>
        <section class="grid">
$($galleryCards -join "`n")
        </section>
    </main>
</body>
</html>
"@

Set-Content -LiteralPath $galleryPath -Value $galleryHtml -Encoding utf8

$baselineManifestPath = ''
$diffSummaryPath = ''

if ($UpdateBaseline) {
    if ($resolvedBaselineDir -eq '') {
        throw 'UpdateBaseline requires a baseline directory.'
    }

    Write-Step 'Update UI snapshot baselines'
    Reset-Directory $resolvedBaselineDir
    foreach ($file in $pngFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $resolvedBaselineDir $file.Name) -Force
    }

    $baselineManifestPath = Join-Path $resolvedBaselineDir 'snapshot-manifest.json'
    $manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $baselineManifestPath -Encoding utf8
} elseif ($resolvedBaselineDir -ne '') {
    if (-not (Test-Path -LiteralPath $resolvedBaselineDir)) {
        throw "Baseline snapshot directory not found at '$resolvedBaselineDir'."
    }

    $powershell = Resolve-ToolPath 'powershell.exe' @(
        (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe')
    )

    Write-Step 'Compare snapshots against baseline'
    Invoke-External $powershell @(
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-File',
        $compareScript,
        '-ActualDir',
        $resolvedOutputDir,
        '-BaselineDir',
        $resolvedBaselineDir,
        '-ReportDir',
        $resolvedOutputDir,
        '-MaxDifferentPixels',
        "$MaxDifferentPixels",
        '-MaxDifferentRatio',
        "$MaxDifferentRatio"
    )

    $diffSummaryPath = Join-Path $resolvedOutputDir 'snapshot-diff-summary.md'
}

Write-Host ''
Write-Host 'UI snapshot export summary'
Write-Host '--------------------------'
Write-Host "Output directory: $resolvedOutputDir"
Write-Host "Summary: $summaryPath"
Write-Host "Gallery: $galleryPath"
Write-Host "Manifest: $manifestPath"
if ($baselineManifestPath -ne '') {
    Write-Host "Baseline manifest: $baselineManifestPath"
}
if ($resolvedBaselineDir -ne '' -and -not $UpdateBaseline) {
    Write-Host "Baseline directory: $resolvedBaselineDir"
}
if ($diffSummaryPath -ne '') {
    Write-Host "Diff summary: $diffSummaryPath"
}
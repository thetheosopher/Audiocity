param(
    [string]$BuildDir = 'build/ci-windows',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Debug',
    [string]$OpenCppCoveragePath = '',
    [string]$OutputDirectory = 'artifacts/coverage'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}
$resolvedOutputDirectory = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}
New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null

$testExecutable = Join-Path $resolvedBuildDir "tests\$Configuration\audiocity_offline_tests.exe"
if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw "Offline test executable was not found: $testExecutable"
}

if ([string]::IsNullOrWhiteSpace($OpenCppCoveragePath)) {
    $command = Get-Command OpenCppCoverage.exe -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        $defaultTool = 'C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe'
        if (-not (Test-Path -LiteralPath $defaultTool -PathType Leaf)) {
            throw 'OpenCppCoverage.exe was not found. Install it or pass -OpenCppCoveragePath.'
        }
        $OpenCppCoveragePath = $defaultTool
    } else {
        $OpenCppCoveragePath = $command.Source
    }
}

$coverageXml = Join-Path $resolvedOutputDirectory 'audiocity-coverage.xml'
$sourceRoot = Join-Path $repoRoot 'src'
& $OpenCppCoveragePath `
    --quiet `
    "--sources=$sourceRoot" `
    '--modules=audiocity_offline_tests*' `
    "--export_type=cobertura:$coverageXml" `
    "--working_dir=$repoRoot" `
    -- `
    $testExecutable

if ($LASTEXITCODE -ne 0) {
    throw "Coverage run failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $coverageXml -PathType Leaf)) {
    throw "Coverage tool did not create $coverageXml"
}

[xml]$coverage = Get-Content -LiteralPath $coverageXml -Raw
$coverageRoot = $coverage.DocumentElement
if ($null -eq $coverageRoot -or $coverageRoot.Name -ne 'coverage') {
    throw "Coverage report does not contain a Cobertura <coverage> root: $coverageXml"
}

$lineRate = 0.0
$branchRate = 0.0
$linesValid = 0
$linesCovered = 0
$invariantCulture = [System.Globalization.CultureInfo]::InvariantCulture
$numberStyle = [System.Globalization.NumberStyles]::Float
if (-not [double]::TryParse([string]$coverageRoot.'line-rate', $numberStyle, $invariantCulture, [ref]$lineRate) -or
    -not [double]::TryParse([string]$coverageRoot.'branch-rate', $numberStyle, $invariantCulture, [ref]$branchRate) -or
    -not [int]::TryParse([string]$coverageRoot.'lines-valid', [ref]$linesValid) -or
    -not [int]::TryParse([string]$coverageRoot.'lines-covered', [ref]$linesCovered)) {
    throw "Coverage report is missing valid Cobertura summary metrics: $coverageXml"
}
if ($linesValid -le 0 -or $linesCovered -lt 0 -or $linesCovered -gt $linesValid -or
    -not [double]::IsFinite($lineRate) -or $lineRate -lt 0.0 -or $lineRate -gt 1.0 -or
    -not [double]::IsFinite($branchRate) -or $branchRate -lt 0.0 -or $branchRate -gt 1.0) {
    throw "Coverage report contains empty or invalid summary metrics: $coverageXml"
}

$sourceNodes = @($coverage.SelectNodes('/coverage/sources/source'))
$classNodes = @($coverage.SelectNodes('/coverage/packages/package/classes/class'))
$sourceRootWithSeparator = $sourceRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
$hasExpectedSourceRoot = $false
$reportedSourceRoots = New-Object System.Collections.Generic.List[string]
foreach ($sourceNode in $sourceNodes) {
    if ([string]::IsNullOrWhiteSpace($sourceNode.InnerText)) {
        continue
    }
    $reportedSource = [System.IO.Path]::GetFullPath($sourceNode.InnerText.Trim())
    $reportedSourceRoots.Add($reportedSource)
    $reportedSourceWithSeparator = $reportedSource.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    if ($sourceRootWithSeparator.StartsWith($reportedSourceWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        $hasExpectedSourceRoot = $true
    }
}

$hasProductionClass = $false
foreach ($classNode in $classNodes) {
    $classFileName = [string]$classNode.filename
    if ([string]::IsNullOrWhiteSpace($classFileName)) {
        continue
    }
    $classCandidates = if ([System.IO.Path]::IsPathRooted($classFileName)) {
        @([System.IO.Path]::GetFullPath($classFileName))
    } else {
        @($reportedSourceRoots | ForEach-Object { [System.IO.Path]::GetFullPath((Join-Path $_ $classFileName)) })
        @([System.IO.Path]::GetFullPath((Join-Path $repoRoot $classFileName)))
        @([System.IO.Path]::GetFullPath((Join-Path $sourceRoot $classFileName)))
    }
    foreach ($classPath in $classCandidates) {
        if ($classPath.StartsWith($sourceRootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase) -and
            (Test-Path -LiteralPath $classPath -PathType Leaf)) {
            $hasProductionClass = $true
            break
        }
    }
    if ($hasProductionClass) {
        break
    }
}

if (-not $hasExpectedSourceRoot -or $classNodes.Count -eq 0 -or -not $hasProductionClass) {
    throw "Coverage report does not contain instrumented classes from the expected src tree: $sourceRoot"
}

$summaryPath = Join-Path $resolvedOutputDirectory 'summary.md'
$summary = @(
    '# Audiocity native coverage baseline'
    ''
    "- Configuration: $Configuration"
    "- Source scope: ``src/**``"
    "- Lines: $linesCovered / $linesValid ($([math]::Round($lineRate * 100.0, 2))%)"
    "- Branch rate: $([math]::Round($branchRate * 100.0, 2))%"
    "- Generated: $([DateTimeOffset]::UtcNow.ToString('u'))"
)
$summary | Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | ForEach-Object { Write-Host $_ }

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
    $summary | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8
}

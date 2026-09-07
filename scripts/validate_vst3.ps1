param(
    [string]$BuildDir = 'build/ci-windows',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [string]$PluginvalPath = '',
    [ValidateRange(1, 10)]
    [int]$StrictnessLevel = 5
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}
$vst3Bundle = Join-Path $resolvedBuildDir "Audiocity_artefacts\$Configuration\VST3\Audiocity.vst3"
if (-not (Test-Path -LiteralPath $vst3Bundle -PathType Container)) {
    throw "Audiocity VST3 bundle was not found: $vst3Bundle"
}

$validationRoot = Join-Path $repoRoot 'artifacts\pluginval'
New-Item -ItemType Directory -Path $validationRoot -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($PluginvalPath)) {
    $pluginvalVersion = 'v1.0.4'
    $expectedSha256 = 'c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab'
    $archivePath = Join-Path $validationRoot "pluginval_$pluginvalVersion.zip"
    $expandedPath = Join-Path $validationRoot $pluginvalVersion
    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        $uri = "https://github.com/Tracktion/pluginval/releases/download/$pluginvalVersion/pluginval_Windows.zip"
        Invoke-WebRequest -Uri $uri -OutFile $archivePath
    }

    $actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $expectedSha256) {
        throw "pluginval archive checksum mismatch: expected $expectedSha256, got $actualSha256"
    }

    if (-not (Test-Path -LiteralPath $expandedPath -PathType Container)) {
        Expand-Archive -LiteralPath $archivePath -DestinationPath $expandedPath
    }
    $pluginval = Get-ChildItem -LiteralPath $expandedPath -Filter 'pluginval.exe' -File -Recurse |
        Select-Object -First 1
    if ($null -eq $pluginval) {
        throw "pluginval.exe was not found after extracting $archivePath"
    }
    $PluginvalPath = $pluginval.FullName
}

$resolvedPluginval = (Resolve-Path -LiteralPath $PluginvalPath).Path
$logDirectory = Join-Path $validationRoot 'logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

Write-Host "Validating $vst3Bundle with pluginval strictness $StrictnessLevel"
$pluginvalArguments = @(
    '--strictness-level', [string]$StrictnessLevel,
    '--skip-gui-tests',
    '--output-dir', ('"' + $logDirectory + '"'),
    '--validate', ('"' + $vst3Bundle + '"')
)
$pluginvalProcess = Start-Process `
    -FilePath $resolvedPluginval `
    -ArgumentList $pluginvalArguments `
    -WindowStyle Hidden `
    -Wait `
    -PassThru

if ($pluginvalProcess.ExitCode -ne 0) {
    throw "pluginval rejected Audiocity with exit code $($pluginvalProcess.ExitCode). Logs: $logDirectory"
}

Write-Host "pluginval accepted Audiocity. Logs: $logDirectory"

param(
    [switch]$EnableAsio,
    [switch]$DisableAsio,
    [switch]$SkipTests,
    [switch]$SkipInstaller,
    [switch]$SkipPortableZip,
    [string]$InnoSetupCompiler = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step {
    param([string]$Message)

    Write-Host "==> $Message"
}

function Get-AppVersion {
    param([string]$RepoRoot)

    $cmakeListsPath = Join-Path $RepoRoot 'CMakeLists.txt'
    $cmakeListsContent = Get-Content -LiteralPath $cmakeListsPath -Raw
    $match = [regex]::Match($cmakeListsContent, 'project\s*\(\s*Audiocity\s+VERSION\s+([0-9]+(?:\.[0-9]+){1,3})',
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)

    if (-not $match.Success) {
        throw "Could not determine Audiocity version from '$cmakeListsPath'."
    }

    return $match.Groups[1].Value
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

function Invoke-CMakeConfigureWithRetry {
    param(
        [string]$Preset,
        [string]$BuildRoot
    )

    & 'cmake' '--preset' $Preset
    if ($LASTEXITCODE -eq 0) {
        return
    }

    Write-Step "Retrying configure after deleting stale $Preset files"
    foreach ($path in @(
        (Join-Path $BuildRoot 'CMakeCache.txt'),
        (Join-Path $BuildRoot 'CMakeFiles'),
        (Join-Path $BuildRoot '_deps\juce-build'),
        (Join-Path $BuildRoot '_deps\juce-subbuild')
    )) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }

    & 'cmake' '--preset' $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: cmake --preset $Preset"
    }
}

function Ensure-Directory {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

function Reset-Directory {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Path $Path | Out-Null
}

function Copy-DirectoryContent {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    Ensure-Directory $DestinationDir
    Copy-Item -Path (Join-Path $SourceDir '*') -Destination $DestinationDir -Recurse -Force
}

function Resolve-InnoCompiler {
    param([string]$PreferredPath)

    if ($PreferredPath -ne '') {
        if (-not (Test-Path -LiteralPath $PreferredPath)) {
            throw "Inno Setup compiler not found at '$PreferredPath'."
        }

        return (Resolve-Path -LiteralPath $PreferredPath).Path
    }

    $candidates = New-Object System.Collections.Generic.List[string]

    $isccCommand = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($null -ne $isccCommand) {
        $candidates.Add($isccCommand.Source)
    }

    if ($null -ne ${env:ProgramFiles(x86)}) {
        $candidates.Add((Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'))
    }

    if ($null -ne $env:ProgramFiles) {
        $candidates.Add((Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'))
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "ISCC.exe was not found. Install Inno Setup 6 or pass -InnoSetupCompiler <path-to-ISCC.exe>."
}

if ($SkipInstaller -and $SkipPortableZip) {
    throw 'At least one artifact must be enabled. Remove -SkipInstaller or -SkipPortableZip.'
}

if ($EnableAsio -and $DisableAsio) {
    throw 'Use only one of -EnableAsio or -DisableAsio.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$appVersion = Get-AppVersion -RepoRoot $repoRoot
$outputRoot = Join-Path $repoRoot 'output'
$stageRoot = Join-Path $outputRoot 'release-stage'
$installerStageRoot = Join-Path $stageRoot 'installer'
$portableStageRoot = Join-Path $stageRoot 'portable'
$portablePackageName = "Audiocity-$appVersion-windows-x64-portable"
$portablePackageRoot = Join-Path $portableStageRoot $portablePackageName
$installerArtifact = Join-Path $outputRoot "Audiocity-$appVersion-windows-x64-setup.exe"
$portableArtifact = Join-Path $outputRoot "Audiocity-$appVersion-windows-x64-portable.zip"
$licensePath = Join-Path $repoRoot 'LICENSE'
$portableInstructionsPath = Join-Path $repoRoot 'installer\PortableInstall.txt'
$innoScriptPath = Join-Path $repoRoot 'installer\AudiocityInstaller.iss'
$releaseWithAsio = $EnableAsio -or -not $DisableAsio
$configurePreset = if ($releaseWithAsio) { 'release-selfcontained-asio' } else { 'release-selfcontained' }
$buildPreset = $configurePreset
$defaultBuildRoot = Join-Path $repoRoot 'build'
$releaseBuildRoot = Join-Path $repoRoot (Join-Path 'build' $buildPreset)
$standaloneSourceDir = Join-Path $releaseBuildRoot 'Audiocity_artefacts\Release\Standalone'
$vst3SourceDir = Join-Path $releaseBuildRoot 'Audiocity_artefacts\Release\VST3\Audiocity.vst3'

Ensure-Directory $outputRoot

if (-not $SkipTests) {
    Write-Step 'Configure debug test build'
    Invoke-CMakeConfigureWithRetry -Preset 'default' -BuildRoot $defaultBuildRoot

    Write-Step 'Build debug test executables'
    Invoke-External 'cmake' @(
        '--build', '--preset', 'default', '--config', 'Debug', '--target',
        'audiocity_offline_tests',
        'audiocity_ui_snapshot_harness',
        'audiocity_preset_runtime_smoke'
    )

    Write-Step 'Run test suite'
    Invoke-External 'ctest' @('--test-dir', (Join-Path $repoRoot 'build'), '-C', 'Debug', '--output-on-failure')
}

Write-Step "Configure $configurePreset"
Invoke-CMakeConfigureWithRetry -Preset $configurePreset -BuildRoot $releaseBuildRoot

Write-Step 'Build self-contained release binaries'
Invoke-External 'cmake' @('--build', '--preset', $buildPreset)

if (-not (Test-Path -LiteralPath $standaloneSourceDir)) {
    throw "Standalone release output not found at '$standaloneSourceDir'."
}

if (-not (Test-Path -LiteralPath $vst3SourceDir)) {
    throw "VST3 release output not found at '$vst3SourceDir'."
}

Write-Step 'Stage release files'
Reset-Directory $stageRoot
Ensure-Directory $installerStageRoot
Ensure-Directory $portableStageRoot

$installerStandaloneStage = Join-Path $installerStageRoot 'standalone'
$installerVst3Stage = Join-Path $installerStageRoot 'VST3'
$portableVst3Stage = Join-Path $portablePackageRoot 'VST3'

Copy-DirectoryContent $standaloneSourceDir $installerStandaloneStage
Ensure-Directory $installerVst3Stage
Copy-Item -LiteralPath $vst3SourceDir -Destination (Join-Path $installerVst3Stage 'Audiocity.vst3') -Recurse -Force

Copy-DirectoryContent $standaloneSourceDir $portablePackageRoot
Ensure-Directory $portableVst3Stage
Copy-Item -LiteralPath $vst3SourceDir -Destination (Join-Path $portableVst3Stage 'Audiocity.vst3') -Recurse -Force
Copy-Item -LiteralPath $licensePath -Destination (Join-Path $portablePackageRoot 'LICENSE') -Force
Copy-Item -LiteralPath $portableInstructionsPath -Destination (Join-Path $portablePackageRoot 'PortableInstall.txt') -Force

# Stage factory presets so the installer + portable bundle ship the stock bank
$factoryPresetsSource = Join-Path $repoRoot 'assets\factory_presets'
if (Test-Path -LiteralPath $factoryPresetsSource) {
    foreach ($factoryPresetDestination in @(
        (Join-Path $installerStandaloneStage 'FactoryPresets'),
        (Join-Path $installerVst3Stage 'Audiocity.vst3\Contents\Resources\FactoryPresets'),
        (Join-Path $portablePackageRoot 'FactoryPresets'),
        (Join-Path $portableVst3Stage 'Audiocity.vst3\Contents\Resources\FactoryPresets')
    )) {
        Reset-Directory $factoryPresetDestination
        Copy-Item -Path (Join-Path $factoryPresetsSource '*') -Destination $factoryPresetDestination -Recurse -Force
    }
}
else {
    Write-Warning "Factory presets not found at '$factoryPresetsSource'; bank will not be bundled."
}

if (-not $SkipInstaller) {
    $resolvedInnoCompiler = Resolve-InnoCompiler $InnoSetupCompiler

    if (Test-Path -LiteralPath $installerArtifact) {
        Remove-Item -LiteralPath $installerArtifact -Force
    }

    Write-Step 'Build Inno Setup installer'
    Invoke-External $resolvedInnoCompiler @(
        "/DMyAppVersion=$appVersion",
        "/DSourceRoot=$installerStageRoot",
        "/DOutputDir=$outputRoot",
        "/DOutputBaseFilename=Audiocity-$appVersion-windows-x64-setup",
        $innoScriptPath
    )

    if (-not (Test-Path -LiteralPath $installerArtifact)) {
        throw "Installer artifact was not created at '$installerArtifact'."
    }
}

if (-not $SkipPortableZip) {
    if (Test-Path -LiteralPath $portableArtifact) {
        Remove-Item -LiteralPath $portableArtifact -Force
    }

    Write-Step 'Build portable zip'
    Compress-Archive -Path $portablePackageRoot -DestinationPath $portableArtifact -CompressionLevel Optimal

    if (-not (Test-Path -LiteralPath $portableArtifact)) {
        throw "Portable artifact was not created at '$portableArtifact'."
    }
}

Write-Host ''
Write-Host 'Release artifact summary'
Write-Host '------------------------'
if (-not $SkipInstaller) {
    Write-Host "Installer: $installerArtifact"
}
if (-not $SkipPortableZip) {
    Write-Host "Portable zip: $portableArtifact"
}

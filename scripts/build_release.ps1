param(
    [switch]$EnableAsio,
    [switch]$SkipTests,
    [switch]$SkipInstaller,
    [switch]$SkipPortableZip,
    [string]$InnoSetupCompiler = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$appVersion = '1.0.0'

function Write-Step {
    param([string]$Message)

    Write-Host "==> $Message"
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

$repoRoot = Split-Path -Parent $PSScriptRoot
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
$configurePreset = if ($EnableAsio) { 'release-selfcontained-asio' } else { 'release-selfcontained' }
$buildPreset = $configurePreset
$releaseBuildRoot = Join-Path $repoRoot (Join-Path 'build' $buildPreset)
$standaloneSourceDir = Join-Path $releaseBuildRoot 'Audiocity_artefacts\Release\Standalone'
$vst3SourceDir = Join-Path $releaseBuildRoot 'Audiocity_artefacts\Release\VST3\Audiocity.vst3'

Ensure-Directory $outputRoot

if (-not $SkipTests) {
    Write-Step 'Configure debug test build'
    Invoke-External 'cmake' @('--preset', 'default')

    Write-Step 'Build offline tests'
    Invoke-External 'cmake' @('--build', '--preset', 'default', '--config', 'Debug', '--target', 'audiocity_offline_tests')

    Write-Step 'Run test suite'
    Invoke-External 'ctest' @('--test-dir', (Join-Path $repoRoot 'build'), '-C', 'Debug', '--output-on-failure')
}

Write-Step "Configure $configurePreset"
Invoke-External 'cmake' @('--preset', $configurePreset)

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

if (-not $SkipInstaller) {
    $resolvedInnoCompiler = Resolve-InnoCompiler $InnoSetupCompiler

    if (Test-Path -LiteralPath $installerArtifact) {
        Remove-Item -LiteralPath $installerArtifact -Force
    }

    Write-Step 'Build Inno Setup installer'
    Invoke-External $resolvedInnoCompiler @(
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

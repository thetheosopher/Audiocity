param(
    [string]$BuildDir = 'build/ci-windows',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [switch]$CompileInstaller,
    [string]$InnoSetupCompiler = '',
    [string]$InstallerValidationOutputDir = 'artifacts/installer-validation'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-PresetManifest {
    param([Parameter(Mandatory)][string]$Root)

    $rootItem = Get-Item -LiteralPath $Root -Force
    if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Factory-preset bank roots must not be links or reparse points: $($rootItem.FullName)"
    }

    $resolvedRoot = $rootItem.FullName
    $rootPrefix = $resolvedRoot.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $manifest = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::Ordinal)

    $items = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -Force)
    foreach ($item in $items) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Factory-preset banks must not contain links or reparse points: $($item.FullName)"
        }

        if ($item.PSIsContainer -or $item.Extension -ine '.acp') {
            continue
        }

        if (-not $item.FullName.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Factory preset escaped its expected root: $($item.FullName)"
        }

        $relativePath = $item.FullName.Substring($rootPrefix.Length).Replace('\', '/')
        if (($relativePath.Length -eq 0) -or
            [System.IO.Path]::IsPathRooted($relativePath) -or
            ($relativePath.Split('/') -contains '..')) {
            throw "Factory preset has an invalid relative path: $($item.FullName)"
        }

        if ($manifest.ContainsKey($relativePath)) {
            throw "Factory-preset bank contains a duplicate relative path: $relativePath"
        }

        $manifest.Add($relativePath, (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash)
    }

    return $manifest
}

function Assert-PresetBankMatches {
    param(
        [Parameter(Mandatory)]$Reference,
        [Parameter(Mandatory)]$Candidate,
        [Parameter(Mandatory)][string]$Label
    )

    $differences = [System.Collections.Generic.List[string]]::new()
    foreach ($relativePath in $Reference.Keys) {
        if (-not $Candidate.ContainsKey($relativePath)) {
            $differences.Add("missing '$relativePath'")
        }
        elseif (-not [System.StringComparer]::OrdinalIgnoreCase.Equals(
            $Reference[$relativePath], $Candidate[$relativePath])) {
            $differences.Add("content differs for '$relativePath'")
        }
    }

    foreach ($relativePath in $Candidate.Keys) {
        if (-not $Reference.ContainsKey($relativePath)) {
            $differences.Add("unexpected '$relativePath'")
        }
    }

    if ($differences.Count -gt 0) {
        $preview = ($differences | Select-Object -First 8) -join '; '
        if ($differences.Count -gt 8) {
            $preview += "; and $($differences.Count - 8) more"
        }
        throw "$Label factory-preset copy mismatch: $preview."
    }
}

function Resolve-InnoSetupCompiler {
    param([string]$PreferredPath)

    if ($PreferredPath -ne '') {
        if (-not (Test-Path -LiteralPath $PreferredPath -PathType Leaf)) {
            throw "Inno Setup compiler not found at '$PreferredPath'."
        }
        return (Resolve-Path -LiteralPath $PreferredPath).Path
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
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
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'ISCC.exe was not found. Install Inno Setup 6 or pass -InnoSetupCompiler <path-to-ISCC.exe>.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}

$artifactRoot = Join-Path $resolvedBuildDir "Audiocity_artefacts\$Configuration"
$standaloneExe = Join-Path $artifactRoot 'Standalone\Audiocity.exe'
$standalonePresets = Join-Path $artifactRoot 'Standalone\FactoryPresets'
$vst3Bundle = Join-Path $artifactRoot 'VST3\Audiocity.vst3'
$vst3Binary = Join-Path $vst3Bundle 'Contents\x86_64-win\Audiocity.vst3'
$vst3Presets = Join-Path $vst3Bundle 'Contents\Resources\FactoryPresets'
$installerDefinition = Join-Path $repoRoot 'installer\AudiocityInstaller.iss'
$portableInstructions = Join-Path $repoRoot 'installer\PortableInstall.txt'
$license = Join-Path $repoRoot 'LICENSE'
$installerIcon = Join-Path $repoRoot 'assets\icons\audiocity_icon_multi.ico'
$sourcePresetRoot = Join-Path $repoRoot 'assets\factory_presets'

$requiredFiles = @(
    $standaloneExe,
    $vst3Binary,
    $installerDefinition,
    $portableInstructions,
    $license,
    $installerIcon
)

foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required Release or installer input file was not found: $requiredFile"
    }
}

foreach ($requiredDirectory in @($standalonePresets, $vst3Bundle, $vst3Presets, $sourcePresetRoot)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required Release or installer input directory was not found: $requiredDirectory"
    }
}

$sourcePresets = Get-PresetManifest -Root $sourcePresetRoot
if ($sourcePresets.Count -eq 0) {
    throw 'The source factory-preset bank is empty.'
}

Assert-PresetBankMatches -Reference $sourcePresets `
    -Candidate (Get-PresetManifest -Root $standalonePresets) -Label 'Standalone'
Assert-PresetBankMatches -Reference $sourcePresets `
    -Candidate (Get-PresetManifest -Root $vst3Presets) -Label 'VST3'

$installerValidationArtifact = $null
if ($CompileInstaller) {
    $compiler = Resolve-InnoSetupCompiler -PreferredPath $InnoSetupCompiler
    $resolvedInstallerOutputDir = if ([System.IO.Path]::IsPathRooted($InstallerValidationOutputDir)) {
        [System.IO.Path]::GetFullPath($InstallerValidationOutputDir)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $InstallerValidationOutputDir))
    }
    if (-not (Test-Path -LiteralPath $resolvedInstallerOutputDir -PathType Container)) {
        New-Item -ItemType Directory -Path $resolvedInstallerOutputDir -Force | Out-Null
    }

    $installerValidationBaseName = 'Audiocity-installer-input-validation'
    $installerValidationArtifact = Join-Path $resolvedInstallerOutputDir "$installerValidationBaseName.exe"
    if (Test-Path -LiteralPath $installerValidationArtifact -PathType Leaf) {
        Remove-Item -LiteralPath $installerValidationArtifact -Force
    }

    $compilerArguments = @(
        "/DSourceRoot=$artifactRoot",
        "/DOutputDir=$resolvedInstallerOutputDir",
        "/DOutputBaseFilename=$installerValidationBaseName",
        $installerDefinition
    )
    & $compiler @compilerArguments
    if ($LASTEXITCODE -ne 0) {
        throw "ISCC.exe failed installer-input validation with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $installerValidationArtifact -PathType Leaf)) {
        throw "ISCC.exe did not create the expected validation installer: $installerValidationArtifact"
    }
}

Write-Host "Verified Audiocity $Configuration artifacts:"
Write-Host "  Standalone: $standaloneExe"
Write-Host "  VST3:       $vst3Bundle"
Write-Host "  Presets:    $($sourcePresets.Count) in both product resource locations"
Write-Host "  Installer:  $installerDefinition"
if ($null -ne $installerValidationArtifact) {
    Write-Host "  Compiled:   $installerValidationArtifact"
}

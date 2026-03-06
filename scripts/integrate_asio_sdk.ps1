param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,

    [string]$DestinationDir = (Join-Path $PSScriptRoot "..\third_party\asiosdk"),

    [switch]$CleanDestination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedSource = (Resolve-Path $SourceDir).Path
$resolvedDestination = [System.IO.Path]::GetFullPath($DestinationDir)

if (-not (Test-Path $resolvedSource -PathType Container)) {
    throw "SourceDir does not exist or is not a directory: $resolvedSource"
}

$sourceCommon = Join-Path $resolvedSource "common\iasiodrv.h"
$sourceAltCommon = Join-Path $resolvedSource "ASIOSDK\common\iasiodrv.h"

if (-not (Test-Path $sourceCommon) -and -not (Test-Path $sourceAltCommon)) {
    throw "Could not find required ASIO header 'common/iasiodrv.h' under: $resolvedSource"
}

if (Test-Path $resolvedDestination -PathType Container) {
    if ($CleanDestination) {
        Get-ChildItem $resolvedDestination -Force | Remove-Item -Recurse -Force
    }
} else {
    New-Item -ItemType Directory -Path $resolvedDestination | Out-Null
}

Write-Host "==> Copy ASIO SDK to $resolvedDestination"
Copy-Item -Path (Join-Path $resolvedSource '*') -Destination $resolvedDestination -Recurse -Force

$destinationCommon = Join-Path $resolvedDestination "common\iasiodrv.h"
$destinationAltCommon = Join-Path $resolvedDestination "ASIOSDK\common\iasiodrv.h"

if (-not (Test-Path $destinationCommon) -and -not (Test-Path $destinationAltCommon)) {
    throw "Integration failed: required header missing in destination."
}

Write-Host "==> ASIO SDK integration complete"
Write-Host "Now configure with: cmake --preset package-release-bootstrapper-asio"

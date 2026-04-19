param(
    [switch]$WhatIf,
    [switch]$IncludeOutput
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$targets = @(
    'build',
    'installer/bin',
    'installer/obj',
    'Testing/Temporary'
)

if ($IncludeOutput) {
    $targets += 'output'
}

$removed = New-Object System.Collections.Generic.List[string]
$skipped = New-Object System.Collections.Generic.List[string]

foreach ($relativePath in $targets) {
    $fullPath = Join-Path $root $relativePath
    if (-not (Test-Path $fullPath)) {
        $skipped.Add("$relativePath (missing)")
        continue
    }

    if ($relativePath -eq 'output') {
        Get-ChildItem -Path $fullPath -Force | ForEach-Object {
            if ($_.PSIsContainer) {
                if ($WhatIf) {
                    Write-Host "[WhatIf] Remove directory: output/$($_.Name)"
                }
                else {
                    Remove-Item -LiteralPath $_.FullName -Recurse -Force
                    $removed.Add("output/$($_.Name)")
                }
            }
            else {
                $ext = $_.Extension.ToLowerInvariant()
                if ($ext -eq '.exe' -or $ext -eq '.msi' -or $ext -eq '.zip') {
                    $skipped.Add("output/$($_.Name) (kept package)")
                    return
                }

                if ($WhatIf) {
                    Write-Host "[WhatIf] Remove file: output/$($_.Name)"
                }
                else {
                    Remove-Item -LiteralPath $_.FullName -Force
                    $removed.Add("output/$($_.Name)")
                }
            }
        }

        continue
    }

    if ($WhatIf) {
        Write-Host "[WhatIf] Remove: $relativePath"
    }
    else {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
        $removed.Add($relativePath)
    }
}

Write-Host ''
Write-Host 'Cleanup summary'
Write-Host '---------------'

if ($removed.Count -gt 0) {
    Write-Host 'Removed:'
    $removed | ForEach-Object { Write-Host "  - $_" }
}
else {
    Write-Host 'Removed: none'
}

if ($skipped.Count -gt 0) {
    Write-Host 'Skipped/kept:'
    $skipped | ForEach-Object { Write-Host "  - $_" }
}
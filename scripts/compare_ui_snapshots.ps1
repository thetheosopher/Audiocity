param(
    [Parameter(Mandatory = $true)]
    [string]$ActualDir,

    [Parameter(Mandatory = $true)]
    [string]$BaselineDir,

    [string]$ReportDir = '',
    [int]$MaxDifferentPixels = 0,
    [double]$MaxDifferentRatio = 0.0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

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

function Remove-DirectoryIfExists {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Convert-ToArgbBitmap {
    param([System.Drawing.Bitmap]$SourceBitmap)

    $convertedBitmap = New-Object System.Drawing.Bitmap(
        $SourceBitmap.Width,
        $SourceBitmap.Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($convertedBitmap)

    try {
        $graphics.DrawImage($SourceBitmap, 0, 0, $SourceBitmap.Width, $SourceBitmap.Height)
    }
    finally {
        $graphics.Dispose()
    }

    return $convertedBitmap
}

function Save-DiffBitmap {
    param(
        [byte[]]$DiffBytes,
        [int]$Width,
        [int]$Height,
        [string]$DiffPath
    )

    $bitmap = $null
    $bitmapData = $null

    try {
        $bitmap = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $rectangle = New-Object System.Drawing.Rectangle(0, 0, $Width, $Height)
        $bitmapData = $bitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bitmap.PixelFormat)
        [Runtime.InteropServices.Marshal]::Copy($DiffBytes, 0, $bitmapData.Scan0, $DiffBytes.Length)
        $bitmap.UnlockBits($bitmapData)
        $bitmapData = $null
        $bitmap.Save($DiffPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        if ($null -ne $bitmapData) {
            $bitmap.UnlockBits($bitmapData)
        }

        if ($null -ne $bitmap) {
            $bitmap.Dispose()
        }
    }
}

function Compare-ImageFiles {
    param(
        [string]$BaselinePath,
        [string]$ActualPath,
        [string]$DiffPath,
        [int]$AllowedDifferentPixels,
        [double]$AllowedDifferentRatio
    )

    $baselineHash = Get-Sha256Hash $BaselinePath
    $actualHash = Get-Sha256Hash $ActualPath
    $fileName = [System.IO.Path]::GetFileName($BaselinePath)
    $result = [ordered]@{
        fileName = $fileName
        status = 'match'
        note = ''
        width = 0
        height = 0
        differentPixels = 0
        differentPixelRatio = 0.0
        maxChannelDelta = 0
        baselineSha256 = $baselineHash
        actualSha256 = $actualHash
        diffPath = ''
    }

    if ($baselineHash -eq $actualHash) {
        return [pscustomobject]$result
    }

    $baselineBitmap = $null
    $actualBitmap = $null
    $baselineArgbBitmap = $null
    $actualArgbBitmap = $null
    $baselineBitmapData = $null
    $actualBitmapData = $null

    try {
        $baselineBitmap = New-Object System.Drawing.Bitmap($BaselinePath)
        $actualBitmap = New-Object System.Drawing.Bitmap($ActualPath)
        $baselineArgbBitmap = Convert-ToArgbBitmap $baselineBitmap
        $actualArgbBitmap = Convert-ToArgbBitmap $actualBitmap

        $result.width = $baselineArgbBitmap.Width
        $result.height = $baselineArgbBitmap.Height

        if ($baselineArgbBitmap.Width -ne $actualArgbBitmap.Width -or $baselineArgbBitmap.Height -ne $actualArgbBitmap.Height) {
            $result.status = 'dimension-mismatch'
            $result.note = "Baseline is $($baselineArgbBitmap.Width)x$($baselineArgbBitmap.Height); actual is $($actualArgbBitmap.Width)x$($actualArgbBitmap.Height)."
            return [pscustomobject]$result
        }

        $rectangle = New-Object System.Drawing.Rectangle(0, 0, $baselineArgbBitmap.Width, $baselineArgbBitmap.Height)
        $baselineBitmapData = $baselineArgbBitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $baselineArgbBitmap.PixelFormat)
        $actualBitmapData = $actualArgbBitmap.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $actualArgbBitmap.PixelFormat)

        $byteCount = [Math]::Abs($baselineBitmapData.Stride) * $baselineArgbBitmap.Height
        $baselineBytes = New-Object byte[] $byteCount
        $actualBytes = New-Object byte[] $byteCount
        [Runtime.InteropServices.Marshal]::Copy($baselineBitmapData.Scan0, $baselineBytes, 0, $byteCount)
        [Runtime.InteropServices.Marshal]::Copy($actualBitmapData.Scan0, $actualBytes, 0, $byteCount)

        $differentPixels = 0
        $maxChannelDelta = 0
        $diffBytes = $null
        for ($index = 0; $index -lt $byteCount; $index += 4) {
            $blueDelta = [Math]::Abs([int]$baselineBytes[$index] - [int]$actualBytes[$index])
            $greenDelta = [Math]::Abs([int]$baselineBytes[$index + 1] - [int]$actualBytes[$index + 1])
            $redDelta = [Math]::Abs([int]$baselineBytes[$index + 2] - [int]$actualBytes[$index + 2])
            $alphaDelta = [Math]::Abs([int]$baselineBytes[$index + 3] - [int]$actualBytes[$index + 3])
            $pixelDelta = [Math]::Max([Math]::Max($blueDelta, $greenDelta), [Math]::Max($redDelta, $alphaDelta))
            if ($pixelDelta -gt 0) {
                ++$differentPixels
                if ($pixelDelta -gt $maxChannelDelta) {
                    $maxChannelDelta = $pixelDelta
                }

                if ($null -eq $diffBytes) {
                    $diffBytes = New-Object byte[] $byteCount
                }

                $diffBytes[$index] = 255
                $diffBytes[$index + 1] = 0
                $diffBytes[$index + 2] = 255
                $diffBytes[$index + 3] = 255
            }
        }

        $totalPixels = [double]($baselineArgbBitmap.Width * $baselineArgbBitmap.Height)
        $differentRatio = if ($totalPixels -gt 0) {
            [Math]::Round($differentPixels / $totalPixels, 8)
        } else {
            0.0
        }

        $result.differentPixels = $differentPixels
        $result.differentPixelRatio = $differentRatio
        $result.maxChannelDelta = $maxChannelDelta

        if ($differentPixels -eq 0) {
            $result.status = 'metadata-diff'
            $result.note = 'PNG bytes differ but pixels are identical.'
            return [pscustomobject]$result
        }

        $withinPixelThreshold = $differentPixels -le $AllowedDifferentPixels
        $withinRatioThreshold = $differentRatio -le $AllowedDifferentRatio
        if ($withinPixelThreshold -and $withinRatioThreshold) {
            $result.status = 'within-threshold'
            $result.note = 'Pixel difference stayed within the configured threshold.'
            return [pscustomobject]$result
        }

        $result.status = 'mismatch'
        $result.note = 'Pixel difference exceeded the configured threshold.'
        Save-DiffBitmap -DiffBytes $diffBytes -Width $baselineArgbBitmap.Width -Height $baselineArgbBitmap.Height -DiffPath $DiffPath
        $result.diffPath = "diffs/$fileName"
        return [pscustomobject]$result
    }
    finally {
        if ($null -ne $baselineBitmapData) {
            $baselineArgbBitmap.UnlockBits($baselineBitmapData)
        }

        if ($null -ne $actualBitmapData) {
            $actualArgbBitmap.UnlockBits($actualBitmapData)
        }

        foreach ($bitmap in @($baselineArgbBitmap, $actualArgbBitmap, $baselineBitmap, $actualBitmap)) {
            if ($null -ne $bitmap) {
                $bitmap.Dispose()
            }
        }
    }
}

$resolvedActualDir = [System.IO.Path]::GetFullPath($ActualDir)
$resolvedBaselineDir = [System.IO.Path]::GetFullPath($BaselineDir)
$resolvedReportDir = if ($ReportDir -ne '') {
    [System.IO.Path]::GetFullPath($ReportDir)
} else {
    $resolvedActualDir
}

if (-not (Test-Path -LiteralPath $resolvedActualDir)) {
    throw "Actual snapshot directory not found: '$resolvedActualDir'."
}

if (-not (Test-Path -LiteralPath $resolvedBaselineDir)) {
    throw "Baseline snapshot directory not found: '$resolvedBaselineDir'."
}

Ensure-Directory $resolvedReportDir
$diffDir = Join-Path $resolvedReportDir 'diffs'
Remove-DirectoryIfExists $diffDir

$actualFiles = Get-ChildItem -LiteralPath $resolvedActualDir -Filter '*.png' | Sort-Object Name
$baselineFiles = Get-ChildItem -LiteralPath $resolvedBaselineDir -Filter '*.png' | Sort-Object Name

if ($baselineFiles.Count -eq 0) {
    throw "Baseline snapshot directory '$resolvedBaselineDir' does not contain any PNG files."
}

$actualByName = @{}
foreach ($file in $actualFiles) {
    $actualByName[$file.Name] = $file.FullName
}

$baselineByName = @{}
foreach ($file in $baselineFiles) {
    $baselineByName[$file.Name] = $file.FullName
}

$results = New-Object System.Collections.Generic.List[object]

foreach ($baselineFile in $baselineFiles) {
    if (-not $actualByName.ContainsKey($baselineFile.Name)) {
        $results.Add([pscustomobject]@{
            fileName = $baselineFile.Name
            status = 'missing'
            note = 'Actual snapshot is missing.'
            width = 0
            height = 0
            differentPixels = -1
            differentPixelRatio = 1.0
            maxChannelDelta = 0
            baselineSha256 = Get-Sha256Hash $baselineFile.FullName
            actualSha256 = ''
            diffPath = ''
        }) | Out-Null
        continue
    }

    if (-not (Test-Path -LiteralPath $diffDir)) {
        Ensure-Directory $diffDir
    }

    $diffPath = Join-Path $diffDir $baselineFile.Name
    $results.Add((Compare-ImageFiles -BaselinePath $baselineFile.FullName -ActualPath $actualByName[$baselineFile.Name] -DiffPath $diffPath -AllowedDifferentPixels $MaxDifferentPixels -AllowedDifferentRatio $MaxDifferentRatio)) | Out-Null
}

foreach ($actualFile in $actualFiles) {
    if ($baselineByName.ContainsKey($actualFile.Name)) {
        continue
    }

    $results.Add([pscustomobject]@{
        fileName = $actualFile.Name
        status = 'unexpected'
        note = 'Actual snapshot has no matching baseline.'
        width = 0
        height = 0
        differentPixels = -1
        differentPixelRatio = 1.0
        maxChannelDelta = 0
        baselineSha256 = ''
        actualSha256 = Get-Sha256Hash $actualFile.FullName
        diffPath = ''
    }) | Out-Null
}

$failedStatuses = @('dimension-mismatch', 'mismatch', 'missing', 'unexpected')
$failedResults = @($results | Where-Object { $failedStatuses -contains $_.status })
$summaryPath = Join-Path $resolvedReportDir 'snapshot-diff-summary.md'
$reportPath = Join-Path $resolvedReportDir 'snapshot-diff-report.json'

if (-not (Test-Path -LiteralPath $diffDir)) {
    Ensure-Directory $diffDir
}

$report = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    result = if ($failedResults.Count -eq 0) { 'pass' } else { 'fail' }
    actualDir = $resolvedActualDir
    baselineDir = $resolvedBaselineDir
    maxDifferentPixels = $MaxDifferentPixels
    maxDifferentRatio = $MaxDifferentRatio
    fileCount = $results.Count
    files = $results
}

$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding utf8

$summaryLines = @(
    '# UI Snapshot Diff',
    '',
    "- Result: $($report.result.ToUpperInvariant())",
    "- Actual directory: $resolvedActualDir",
    "- Baseline directory: $resolvedBaselineDir",
    "- Max different pixels: $MaxDifferentPixels",
    "- Max different ratio: $MaxDifferentRatio",
    '',
    '| File | Status | Diff Pixels | Diff Ratio | Max Delta | Notes |',
    '| --- | --- | ---: | ---: | ---: | --- |'
)

foreach ($entry in ($results | Sort-Object fileName)) {
    $summaryLines += "| $($entry.fileName) | $($entry.status) | $($entry.differentPixels) | $($entry.differentPixelRatio) | $($entry.maxChannelDelta) | $($entry.note) |"
}

Set-Content -LiteralPath $summaryPath -Value $summaryLines -Encoding utf8

if ($failedResults.Count -gt 0) {
    throw "UI snapshot baseline comparison failed. See '$summaryPath' for details."
}
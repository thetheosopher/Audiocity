param(
    [ValidateSet('cpu', 'fidelity', 'ultra')]
    [string]$Quality = 'fidelity',
    [double]$Seconds = 4.0,
    [int]$Voices = 24,
    [int]$BlockSize = 128,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',
    [string]$OutputDir = '',
    [int]$Top = 20,
    [switch]$NoLoopCrossfade,
    [switch]$SkipBuild,
    [switch]$Overwrite
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

function Reset-Directory {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Path $Path | Out-Null
}

function Remove-PathIfExists {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
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

function Resolve-WorkspacePath {
    param(
        [string]$PathValue,
        [string]$WorkspaceRoot
    )

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $WorkspaceRoot $PathValue))
}

function Resolve-ToolPath {
    param(
        [string]$CommandName,
        [string[]]$Candidates = @(),
        [string[]]$WildcardCandidates = @()
    )

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    foreach ($pattern in $WildcardCandidates) {
        $matches = Get-ChildItem -Path $pattern -File -ErrorAction SilentlyContinue | Sort-Object FullName -Descending
        if ($matches.Count -gt 0) {
            return $matches[0].FullName
        }
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "$CommandName was not found. Install the required Visual Studio and Windows Performance Toolkit components or add $CommandName to PATH."
}

function Invoke-CMakeConfigureWithRetry {
    param(
        [string]$CMakePath,
        [string]$Preset,
        [string]$BuildRoot
    )

    & $CMakePath --preset $Preset
    if ($LASTEXITCODE -eq 0) {
        return
    }

    Write-Step "Retrying configure after clearing stale $Preset state"
    foreach ($path in @(
        (Join-Path $BuildRoot 'CMakeCache.txt'),
        (Join-Path $BuildRoot 'CMakeFiles')
    )) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }

    & $CMakePath --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $CMakePath --preset $Preset"
    }
}

function Convert-HexToUInt64 {
    param([string]$Value)

    $trimmed = $Value.Trim()
    if ($trimmed.StartsWith('0x', [System.StringComparison]::OrdinalIgnoreCase)) {
        $trimmed = $trimmed.Substring(2)
    }

    return [System.Convert]::ToUInt64($trimmed, 16)
}

function Get-PreferredImageBase {
    param(
        [string]$DumpbinPath,
        [string]$ExecutablePath
    )

    $imageBaseLine = & $DumpbinPath /headers $ExecutablePath | Select-String 'image base' | Select-Object -First 1
    if ($null -eq $imageBaseLine) {
        throw "Could not read the PE image base from '$ExecutablePath'."
    }

    $match = [regex]::Match($imageBaseLine.Line, '([0-9A-Fa-f]+)\s+image base')
    if (-not $match.Success) {
        throw "Could not parse the PE image base from '$($imageBaseLine.Line)'."
    }

    return [System.Convert]::ToUInt64($match.Groups[1].Value, 16)
}

function Format-TableText {
    param([object[]]$Rows)

    if (@($Rows).Count -eq 0) {
        return '<none>'
    }

    return (($Rows | Format-Table -AutoSize | Out-String -Width 240).TrimEnd())
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot 'build'
$profileRunsRoot = Join-Path $buildRoot 'tests/profile-runs'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'

$resolvedOutputDir = if ($OutputDir -ne '') {
    Resolve-WorkspacePath -PathValue $OutputDir -WorkspaceRoot $repoRoot
} else {
    Join-Path $profileRunsRoot "engine-profile-$timestamp"
}

if ($OutputDir -ne '' -and (Test-Path -LiteralPath $resolvedOutputDir) -and -not $Overwrite) {
    throw "Output directory '$resolvedOutputDir' already exists. Pass -Overwrite to reuse it."
}

Ensure-Directory $profileRunsRoot
Reset-Directory $resolvedOutputDir

$cmake = Resolve-ToolPath 'cmake.exe' @(
    'C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    'C:/Program Files/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
) @(
    'C:/Program Files/Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
)

$vsDiagnostics = Resolve-ToolPath 'VSDiagnostics.exe' @() @(
    'C:/Program Files/Microsoft Visual Studio/*/*/Team Tools/DiagnosticsHub/Collector/VSDiagnostics.exe'
)

$cpuUsageConfig = Resolve-ToolPath 'CpuUsageBase.json' @() @(
    'C:/Program Files/Microsoft Visual Studio/*/*/Team Tools/DiagnosticsHub/Collector/AgentConfigs/CpuUsageBase.json'
)

$wpaExporter = Resolve-ToolPath 'wpaexporter.exe' @(
    'C:/Program Files (x86)/Windows Kits/10/Windows Performance Toolkit/wpaexporter.exe'
)

$wpaProfile = Resolve-ToolPath 'WpaRuleMatchExporter.wpaProfile' @(
    'C:/Program Files (x86)/Windows Kits/10/Windows Performance Toolkit/Catalog/WpaRuleMatchExporter.wpaProfile'
)

$dumpbin = Resolve-ToolPath 'dumpbin.exe' @() @(
    'C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/HostX64/x64/dumpbin.exe'
)

$llvmSymbolizer = Resolve-ToolPath 'llvm-symbolizer.exe' @() @(
    'C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/HostX64/x64/llvm-symbolizer.exe'
)

if (-not $SkipBuild) {
    Write-Step 'Configure default build tree'
    Invoke-CMakeConfigureWithRetry -CMakePath $cmake -Preset 'default' -BuildRoot $buildRoot

    Write-Step "Build audiocity_engine_profile ($Configuration)"
    Invoke-External $cmake @('--build', '--preset', 'default', '--config', $Configuration, '--target', 'audiocity_engine_profile')
}

$harnessExe = Join-Path $buildRoot "tests/$Configuration/audiocity_engine_profile.exe"
if (-not (Test-Path -LiteralPath $harnessExe)) {
    throw "Profiling harness executable not found at '$harnessExe'."
}

$harnessPdb = [System.IO.Path]::ChangeExtension($harnessExe, '.pdb')
if (-not (Test-Path -LiteralPath $harnessPdb)) {
    throw "Profiling harness PDB not found at '$harnessPdb'. Build the harness in RelWithDebInfo before profiling."
}

$presetsDir = Join-Path $env:LOCALAPPDATA 'Windows Performance Analyzer'
$presetsFile = Join-Path $presetsDir 'MyPresets.wpaPresets'
Ensure-Directory $presetsDir
if (-not (Test-Path -LiteralPath $presetsFile)) {
    New-Item -ItemType File -Path $presetsFile | Out-Null
}

$symbolCacheDir = Join-Path $resolvedOutputDir 'symcache'
Ensure-Directory $symbolCacheDir
$env:_NT_SYMBOL_PATH = "$(Split-Path -Parent $harnessExe);$(Join-Path $buildRoot $Configuration);srv*$symbolCacheDir*https://msdl.microsoft.com/download/symbols"
$env:_NT_SYMCACHE_PATH = $symbolCacheDir

$harnessStdout = Join-Path $resolvedOutputDir 'harness-output.txt'
$harnessStderr = Join-Path $resolvedOutputDir 'harness-error.txt'
$diagsessionPath = Join-Path $resolvedOutputDir 'capture.diagsession'
$expandedSessionDir = Join-Path $resolvedOutputDir 'capture'
$wpaExportDir = Join-Path $resolvedOutputDir 'wpaexport'
$wpaExportLog = Join-Path $resolvedOutputDir 'wpaexporter.log'
$moduleSummaryCsv = Join-Path $resolvedOutputDir 'module-summary.csv'
$functionSummaryCsv = Join-Path $resolvedOutputDir 'engine-hotspots.csv'
$addressSummaryCsv = Join-Path $resolvedOutputDir 'engine-addresses.csv'
$summaryPath = Join-Path $resolvedOutputDir 'summary.md'

Remove-PathIfExists $harnessStdout
Remove-PathIfExists $harnessStderr
Remove-PathIfExists $diagsessionPath
Remove-PathIfExists $expandedSessionDir
Remove-PathIfExists $wpaExportLog
Reset-Directory $wpaExportDir

$secondsText = $Seconds.ToString('0.###', [System.Globalization.CultureInfo]::InvariantCulture)
$harnessArgs = @('--seconds', $secondsText, '--quality', $Quality, '--voices', "$Voices", '--block-size', "$BlockSize")
if ($NoLoopCrossfade) {
    $harnessArgs += '--no-loop-crossfade'
}

$sessionId = [System.Guid]::NewGuid().ToString()

Write-Step 'Capture CPU sample trace'
$proc = Start-Process -FilePath $harnessExe -ArgumentList $harnessArgs -PassThru -RedirectStandardOutput $harnessStdout -RedirectStandardError $harnessStderr
$sessionStarted = $false
try {
    & $vsDiagnostics start $sessionId "/attach:$($proc.Id)" "/loadConfig:$cpuUsageConfig" "/scratchLocation:$resolvedOutputDir" '/package:dir'
    if ($LASTEXITCODE -ne 0) {
        throw "VSDiagnostics start failed with exit code $LASTEXITCODE."
    }

    $sessionStarted = $true

    $proc.WaitForExit()

    & $vsDiagnostics stop $sessionId "/output:$diagsessionPath"
    if ($LASTEXITCODE -ne 0) {
        throw "VSDiagnostics stop failed with exit code $LASTEXITCODE."
    }

    $sessionStarted = $false
}
finally {
    if ($sessionStarted) {
        & $vsDiagnostics stop $sessionId *> $null
    }

    if (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) {
        Stop-Process -Id $proc.Id -Force
    }
}

if (-not (Test-Path -LiteralPath $diagsessionPath)) {
    throw "Diagnostics session was not created at '$diagsessionPath'."
}

Write-Step 'Expand diagnostics session'
Invoke-External $vsDiagnostics @('expandDiagSession', $diagsessionPath)

$etlFile = Get-ChildItem -LiteralPath $expandedSessionDir -Recurse -Filter 'sc.user_aux.etl' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $etlFile) {
    throw "Expanded diagnostics session did not contain sc.user_aux.etl under '$expandedSessionDir'."
}

Write-Step 'Export WPA CSV tables'
& $wpaExporter -profile $wpaProfile -i $etlFile.FullName -outputfolder $wpaExportDir -outputformat CSV -prefix profile_ *> $wpaExportLog
$wpaExportExitCode = $LASTEXITCODE

$cpuCsv = Get-ChildItem -LiteralPath $wpaExportDir -Filter '*CPU_Usage_(Sampled)_RuleEngine.csv' | Select-Object -First 1
$imagesCsv = Get-ChildItem -LiteralPath $wpaExportDir -Filter '*Images_Summary_Table_RuleEngine.csv' | Select-Object -First 1
$processesCsv = Get-ChildItem -LiteralPath $wpaExportDir -Filter '*Processes_RuleEngine.csv' | Select-Object -First 1

if ($null -eq $cpuCsv -or $null -eq $imagesCsv -or $null -eq $processesCsv) {
    throw "WPA export did not produce the expected CPU/process/image CSV files in '$wpaExportDir'."
}

if ($wpaExportExitCode -ne 0) {
    Write-Step "WPA exporter reported exit code $wpaExportExitCode; continuing because the required CSVs were produced"
}

$processes = Import-Csv -LiteralPath $processesCsv.FullName
$processRow = $processes | Where-Object { [int]$_.'Process ID' -eq $proc.Id } | Select-Object -First 1
if ($null -eq $processRow) {
    throw "Could not find process ID $($proc.Id) in '$($processesCsv.FullName)'."
}

$processDisplayName = $processRow.Process
$cpuHeaders = @(
    'Process',
    'Process Name',
    'CPU',
    'Display Name',
    'Module',
    'Stack1',
    'DPC/ISR',
    'Thread ID',
    'Weight (ms)',
    'Address',
    'Function',
    'TimeStamp (s)',
    'Priority',
    'Rank',
    'Stack2',
    'Is PGO''ed',
    'PGO Counts'
)
$cpuRows = Get-Content -LiteralPath $cpuCsv.FullName |
    Select-Object -Skip 1 |
    ConvertFrom-Csv -Header $cpuHeaders |
    Where-Object { $_.Process -eq $processDisplayName }
if (@($cpuRows).Count -eq 0) {
    throw "No sampled CPU rows were found for '$processDisplayName'."
}

$images = Import-Csv -LiteralPath $imagesCsv.FullName
$harnessImageRow = $images |
    Where-Object { $_.Process -eq $processDisplayName -and $_.'Image Name' -eq (Split-Path -Leaf $harnessExe) } |
    Select-Object -First 1

if ($null -eq $harnessImageRow) {
    throw "Could not find the harness image row for '$processDisplayName' in '$($imagesCsv.FullName)'."
}

$runtimeBase = Convert-HexToUInt64 $harnessImageRow.'Start Address'
$preferredBase = Get-PreferredImageBase -DumpbinPath $dumpbin -ExecutablePath $harnessExe
$processTotalWeight = (($cpuRows | ForEach-Object { [double]$_.'Weight (ms)' }) | Measure-Object -Sum).Sum

$moduleSummary = $cpuRows |
    Group-Object Module |
    ForEach-Object {
        $weight = (($_.Group | ForEach-Object { [double]$_.'Weight (ms)' }) | Measure-Object -Sum).Sum
        [pscustomobject]@{
            Module = $_.Name
            WeightMs = [math]::Round($weight, 2)
            PercentOfProcess = if ($processTotalWeight -gt 0.0) { [math]::Round(($weight / $processTotalWeight) * 100.0, 1) } else { 0.0 }
            SampleCount = $_.Count
        }
    } |
    Sort-Object WeightMs -Descending

$harnessModuleName = Split-Path -Leaf $harnessExe
$harnessRows = $cpuRows | Where-Object { $_.Module -eq $harnessModuleName -and $_.Address -match '^0x[0-9A-Fa-f]+$' }
$moduleTotalWeight = (($harnessRows | ForEach-Object { [double]$_.'Weight (ms)' }) | Measure-Object -Sum).Sum

$addressSummary = @(
    $harnessRows |
        Group-Object Address |
        ForEach-Object {
            $weight = (($_.Group | ForEach-Object { [double]$_.'Weight (ms)' }) | Measure-Object -Sum).Sum
            $runtimeAddress = Convert-HexToUInt64 $_.Name
            $preferredAddress = $preferredBase + ($runtimeAddress - $runtimeBase)
            $symbol = ('0x{0:X}' -f $preferredAddress) | & $llvmSymbolizer --obj=$harnessExe | Select-Object -First 1
            if ([string]::IsNullOrWhiteSpace($symbol)) {
                $symbol = '?'
            }

            [pscustomobject]@{
                RuntimeAddress = $_.Name
                PreferredAddress = ('0x{0:X}' -f $preferredAddress)
                Function = $symbol
                WeightMs = [math]::Round($weight, 3)
                SampleCount = $_.Count
            }
        } |
        Sort-Object WeightMs -Descending
)

$functionSummary = @(
    $addressSummary |
        Group-Object Function |
        ForEach-Object {
            $weight = (($_.Group | ForEach-Object { [double]$_.WeightMs }) | Measure-Object -Sum).Sum
            $samples = (($_.Group | ForEach-Object { [int]$_.SampleCount }) | Measure-Object -Sum).Sum
            [pscustomobject]@{
                Function = $_.Name
                WeightMs = [math]::Round($weight, 2)
                PercentOfModule = if ($moduleTotalWeight -gt 0.0) { [math]::Round(($weight / $moduleTotalWeight) * 100.0, 1) } else { 0.0 }
                SampleCount = [int]$samples
            }
        } |
        Sort-Object WeightMs -Descending
)

$topModules = @($moduleSummary | Select-Object -First $Top)
$topFunctions = @($functionSummary | Select-Object -First $Top)

$moduleSummary | Export-Csv -LiteralPath $moduleSummaryCsv -NoTypeInformation
$functionSummary | Export-Csv -LiteralPath $functionSummaryCsv -NoTypeInformation
$addressSummary | Export-Csv -LiteralPath $addressSummaryCsv -NoTypeInformation

$harnessOutputLine = ''
if (Test-Path -LiteralPath $harnessStdout) {
    $harnessOutputLine = Get-Content -LiteralPath $harnessStdout | Where-Object { $_.Trim() -ne '' } | Select-Object -Last 1
}

$summaryLines = @(
    '# Engine Profile Capture',
    '',
    "- Output directory: $resolvedOutputDir",
    "- Harness: $harnessExe",
    "- PDB: $harnessPdb",
    "- Configuration: $Configuration",
    "- Harness arguments: $($harnessArgs -join ' ')",
    "- Harness process: $processDisplayName",
    "- Diagnostics session: $diagsessionPath",
    "- Expanded ETL: $($etlFile.FullName)",
    "- WPA export: $wpaExportDir",
    "- WPA exporter exit code: $wpaExportExitCode",
    "- Preferred image base: $('0x{0:X}' -f $preferredBase)",
    "- Runtime image base: $($harnessImageRow.'Start Address')",
    "- Harness output: $harnessOutputLine",
    '',
    '## Top Modules',
    '',
    '```text',
    (Format-TableText $topModules),
    '```',
    '',
    '## Top Engine Functions',
    '',
    '```text',
    (Format-TableText $topFunctions),
    '```',
    '',
    '## Exported Files',
    '',
    "- $moduleSummaryCsv",
    "- $functionSummaryCsv",
    "- $addressSummaryCsv",
    "- $wpaExportLog",
    "- $harnessStdout",
    "- $harnessStderr"
)

Set-Content -LiteralPath $summaryPath -Value $summaryLines -Encoding utf8

Write-Step 'Harness throughput'
if ($harnessOutputLine -ne '') {
    Write-Host $harnessOutputLine
}

Write-Step 'Top modules'
Write-Host (Format-TableText $topModules)

Write-Step 'Top engine functions'
Write-Host (Format-TableText $topFunctions)

Write-Step "Artifacts written to $resolvedOutputDir"
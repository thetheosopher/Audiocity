param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerRoot
)

if (-not (Test-Path -Path $InstallerRoot)) {
    Write-Host "Installer root not found. Skipping MSI table validation: $InstallerRoot"
    exit 0
}

$msi = Get-ChildItem -Path $InstallerRoot -Recurse -Filter "AudiocityInstaller.msi" -File -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $msi) {
    Write-Host "AudiocityInstaller.msi not found under $InstallerRoot. Skipping MSI table validation."
    exit 0
}

$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase($msi.FullName, 0)

$shortcutRows = @{}
$shortcutView = $database.OpenView('SELECT `Shortcut`, `Directory_`, `Component_`, `Target` FROM `Shortcut`')
$shortcutView.Execute()
$shortcutRecord = $shortcutView.Fetch()
while ($shortcutRecord) {
    $shortcutId = $shortcutRecord.StringData(1)
    $shortcutRows[$shortcutId] = @{
        Directory = $shortcutRecord.StringData(2)
        Component = $shortcutRecord.StringData(3)
        Target = $shortcutRecord.StringData(4)
    }
    $shortcutRecord = $shortcutView.Fetch()
}

$componentRows = @{}
$componentView = $database.OpenView('SELECT `Component`, `Condition` FROM `Component`')
$componentView.Execute()
$componentRecord = $componentView.Fetch()
while ($componentRecord) {
    $componentRows[$componentRecord.StringData(1)] = $componentRecord.StringData(2)
    $componentRecord = $componentView.Fetch()
}

$expectedShortcuts = @{
    "DesktopShortcut" = @{ Directory = "CommonDesktopFolder"; Component = "cmpDesktopShortcut"; Target = "[INSTALLFOLDER]Audiocity.exe" }
    "StartMenuShortcut" = @{ Directory = "AudiocityStartMenuFolder"; Component = "cmpStartMenuShortcut"; Target = "[INSTALLFOLDER]Audiocity.exe" }
}

$expectedComponentConditions = @{
    "cmpDesktopShortcut" = ""
    "cmpStartMenuShortcut" = ""
}

$errors = New-Object System.Collections.Generic.List[string]

foreach ($shortcutId in $expectedShortcuts.Keys) {
    if (-not $shortcutRows.ContainsKey($shortcutId)) {
        $errors.Add("Missing shortcut row: $shortcutId")
        continue
    }

    $actual = $shortcutRows[$shortcutId]
    $expected = $expectedShortcuts[$shortcutId]

    if ($actual.Directory -ne $expected.Directory) {
        $errors.Add("Shortcut $shortcutId has Directory_='$($actual.Directory)' expected '$($expected.Directory)'")
    }

    if ($actual.Component -ne $expected.Component) {
        $errors.Add("Shortcut $shortcutId has Component_='$($actual.Component)' expected '$($expected.Component)'")
    }

    if ($actual.Target -ne $expected.Target) {
        $errors.Add("Shortcut $shortcutId has Target='$($actual.Target)' expected '$($expected.Target)'")
    }
}

foreach ($componentId in $expectedComponentConditions.Keys) {
    if (-not $componentRows.ContainsKey($componentId)) {
        $errors.Add("Missing component row: $componentId")
        continue
    }

    $actualCondition = $componentRows[$componentId]
    $expectedCondition = $expectedComponentConditions[$componentId]

    if (($actualCondition ?? "") -ne $expectedCondition) {
        $errors.Add("Component $componentId has Condition='$actualCondition' expected '$expectedCondition'")
    }
}

if ($errors.Count -gt 0) {
    foreach ($message in $errors) {
        Write-Error $message
    }
    exit 1
}

Write-Host "Validated MSI shortcut/component table data in $($msi.FullName)"
exit 0

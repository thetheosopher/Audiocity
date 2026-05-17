$exe = 'C:/Projects/Applications/Audiocity/build/Debug/audiocity_preset_auditioner.exe'
$log = 'C:/Projects/Applications/Audiocity/audit_run.log'
& $exe *> $log
"ExitCode=$LASTEXITCODE"
Write-Host "--- last 60 lines ---"
Get-Content $log -Tail 60

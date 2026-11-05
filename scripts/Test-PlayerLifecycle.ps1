param([switch]$Asan)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
# Reuse the project's compiler setup and refresh the production plugin as well.
& "$PSScriptRoot/Build-Mp3Plugin.ps1"
$stage = Join-Path $nodivuRoot ('.local/player-lifecycle-' + $(if ($Asan) { 'asan' } else { 'normal' }))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Push-Location $stage
try {
    $compilerOptions = @('/nologo','/utf-8','/std:c++17','/EHsc','/W4','/WX','/MD')
    if ($Asan) { $compilerOptions += @('/Zi','/Od','/fsanitize=address') } else { $compilerOptions += '/O2' }
    & cl.exe @compilerOptions "$nodivuRoot/plugins/mp3-player/tests/lifecycle.cpp" "$nodivuRoot/plugins/mp3-player/player/player.cpp" "$nodivuRoot/plugins/mp3-player/player/decoder.cpp" /Fe:lifecycle.exe /link mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib /INCREMENTAL:NO
    if ($LASTEXITCODE) { throw 'Lifecycle test build failed' }
    $testProcess = Start-Process -FilePath "$stage/lifecycle.exe" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$stage/result.log" -RedirectStandardError "$stage/error.log"
    if (-not $testProcess.WaitForExit(30000)) {
        Stop-Process -Id $testProcess.Id -Force
        throw 'Lifecycle test exceeded 30s (FAIL); test process terminated.'
    }
    Get-Content "$stage/result.log"
    Get-Content "$stage/error.log"
    if ($testProcess.ExitCode -ne 0) { throw "Lifecycle test failed: $($testProcess.ExitCode)" }
} finally { Pop-Location }

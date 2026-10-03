param([switch]$Audio,[switch]$Plugin,[switch]$Mp3,[switch]$Worker,[switch]$Routing,[switch]$Viewport,[switch]$Project,[switch]$Capture,[switch]$Demo,[switch]$StartupRecovery,[switch]$Monitor)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
$nodivuExe = Join-Path $nodivuRoot '.local/electron/nodivu-electron.exe'
$nodivuArgs = @('--smoke-test')
if ($Audio -or $Plugin) { $nodivuArgs += '--test-audio' }
if ($Plugin) { $nodivuArgs += '--test-plugin' }
if ($Mp3) { $nodivuArgs += @('--test-audio','--test-mp3') }
if ($Worker) { $nodivuArgs += @('--test-audio','--test-worker') }
if ($Routing) { $nodivuArgs += @('--test-audio','--test-routing') }
if ($Capture) { $nodivuArgs += '--test-capture' }
if ($Monitor) { $nodivuArgs += '--test-monitor' }
if ($StartupRecovery) { $nodivuArgs += '--test-startup-recovery' }
if ($Demo) { $nodivuArgs += '--frontend-demo' }
if ($Project) { $nodivuArgs += '--test-project' }
if ($Viewport) { $nodivuArgs += '--test-viewport' }
$nodivuProcess = Start-Process -FilePath $nodivuExe -ArgumentList $nodivuArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$nodivuRoot/.local/electron-test.stdout.log" -RedirectStandardError "$nodivuRoot/.local/electron-test.stderr.log"
# Keep a process handle before exit; Windows PowerShell can otherwise return a
# null ExitCode after a fast child has already disappeared.
$null = $nodivuProcess.Handle
if (-not $nodivuProcess.WaitForExit(45000)) { $nodivuProcess.Kill(); throw 'Teste Electron excedeu 45 segundos.' }
$nodivuProcess.WaitForExit()
$nodivuExitCode = $nodivuProcess.ExitCode
Get-Content "$nodivuRoot/.local/electron-test.stdout.log" -Encoding UTF8
Get-Content "$nodivuRoot/.local/electron-test.stderr.log" -Encoding UTF8
$nodivuProcess.Dispose()
if ($null -eq $nodivuExitCode -or $nodivuExitCode -ne 0) { throw "Teste Electron falhou: $nodivuExitCode" }

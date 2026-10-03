param(
    [ValidateSet(10,15,20)][int]$CaptureTargetMs = 20,
    [switch]$MinimumCapturePeriod
)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
$nodivuExe = Join-Path $nodivuRoot '.local/electron/nodivu-electron.exe'
if (-not (Test-Path -LiteralPath $nodivuExe)) { throw 'Execute scripts/Build-Electron.ps1 primeiro.' }
$previousTarget = $env:NODIVU_CAPTURE_TARGET_MS
$previousPeriod = $env:NODIVU_CAPTURE_PERIOD
try {
    $env:NODIVU_CAPTURE_TARGET_MS = [string]$CaptureTargetMs
    $env:NODIVU_CAPTURE_PERIOD = if ($MinimumCapturePeriod) { 'minimum' } else { 'default' }
    # Aplicativo interativo: perfil é herdado pelo processo filho, sem configuração global.
    Start-Process -FilePath $nodivuExe -WorkingDirectory (Split-Path $nodivuExe -Parent)
} finally {
    $env:NODIVU_CAPTURE_TARGET_MS = $previousTarget
    $env:NODIVU_CAPTURE_PERIOD = $previousPeriod
}

param([switch]$Wait)
#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
$nodivuExecutable = Join-Path $nodivuRoot '.local/qt-build/release/nodivu.exe'
if (!(Test-Path -LiteralPath $nodivuExecutable)) { throw 'Compile com scripts/Build-Desktop.ps1 primeiro.' }
$nodivuOriginalPath = $env:PATH
try {
    $env:PATH = "$nodivuRoot/.tools/Qt/bin;$env:PATH"
    $nodivuProcess = Start-Process -FilePath $nodivuExecutable -ArgumentList @('--repository', ('"' + $nodivuRoot + '"')) -PassThru -Wait:$Wait -WindowStyle Hidden
    if ($Wait -and $nodivuProcess.ExitCode -ne 0) { throw "Nodivu encerrou com código $($nodivuProcess.ExitCode)." }
} finally { $env:PATH = $nodivuOriginalPath }

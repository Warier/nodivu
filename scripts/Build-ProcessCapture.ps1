$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -notin @('x64', 'amd64')) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Ferramentas MSVC x64 não encontradas.' }
    & (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$stage = Join-Path $nodivuRoot '.local/process-capture'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Push-Location $stage
try {
    & cl.exe /nologo /utf-8 /std:c++17 /EHsc /O2 /W4 /WX /MD `
        "$nodivuRoot/plugins/windows-audio/capture/process_capture.cpp" `
        "$nodivuRoot/plugins/windows-audio/tests/isolation.cpp" `
        /Fe:process-capture-test.exe /link mmdevapi.lib ole32.lib uuid.lib
    if ($LASTEXITCODE) { throw 'Build do ensaio de captura por processo falhou.' }
} finally { Pop-Location }

$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -notin @('x64','amd64')) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'MSVC x64 não encontrado.' }
    & (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$stage = Join-Path $nodivuRoot '.local/windows-audio-plugin'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Push-Location $stage
try {
    & cl.exe /nologo /utf-8 /std:c++17 /EHsc /O2 /W4 /WX /LD /MT "/I$nodivuRoot/third_party/clap-1.2.2/include" `
        "$nodivuRoot/plugins/windows-audio/adapter/clap_plugin.cpp" `
        "$nodivuRoot/plugins/windows-audio/capture/process_capture.cpp" `
        "$nodivuRoot/plugins/windows-audio/capture/targets.cpp" `
        "$nodivuRoot/plugins/windows-audio/capture/stream.cpp" `
        /link /OUT:nodivu-windows-audio.clap mmdevapi.lib ole32.lib uuid.lib
    if ($LASTEXITCODE) { throw 'Build do plugin de aplicativos falhou.' }
    foreach ($folder in @('.local/plugins/windows-audio','.local/electron/plugins/windows-audio')) {
        $destination = Join-Path $nodivuRoot $folder
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -LiteralPath "$stage/nodivu-windows-audio.clap" -Destination $destination
        Copy-Item -LiteralPath "$nodivuRoot/plugins/windows-audio/plugin.json" -Destination $destination
    }
} finally { Pop-Location }

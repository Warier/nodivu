$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
# Test-ClapFixture chama este script várias vezes: não acumular PATH/INCLUDE do DevShell.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -notin @('x64', 'amd64')) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Ferramentas MSVC x64 não encontradas.' }
    & (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -notin @('x64', 'amd64')) {
        throw 'Inicialização do ambiente MSVC x64 falhou.'
    }
}

$stage = Join-Path $nodivuRoot '.local/soundboard-plugin'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Push-Location $stage
try {
    & cl.exe /nologo /utf-8 /std:c++17 /EHsc /O2 /W4 /LD /MT "/I$nodivuRoot/third_party/clap-1.2.2/include" "$nodivuRoot/plugins/soundboard/adapter/clap_plugin.cpp" "$nodivuRoot/plugins/mp3-player/player/player.cpp" "$nodivuRoot/plugins/mp3-player/player/decoder.cpp" /link /OUT:nodivu-soundboard.clap mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib
    if ($LASTEXITCODE) { throw 'Build Soundboard falhou.' }
    foreach ($folder in @('.local/plugins/soundboard','.local/electron/plugins/soundboard')) {
        $destination=Join-Path $nodivuRoot $folder
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -LiteralPath "$stage/nodivu-soundboard.clap" -Destination $destination
        Copy-Item -LiteralPath "$nodivuRoot/plugins/soundboard/plugin.json" -Destination $destination
        foreach ($asset in @('nodivu-package.json','README.md')) {
            Copy-Item -LiteralPath "$nodivuRoot/plugins/soundboard/$asset" -Destination "$destination/$asset"
        }
        Copy-Item -LiteralPath "$nodivuRoot/LICENSE" -Destination "$destination/LICENSE"
        Copy-Item -LiteralPath "$nodivuRoot/third_party/clap-1.2.2/LICENSE" -Destination "$destination/CLAP-LICENSE"
    }
} finally { Pop-Location }

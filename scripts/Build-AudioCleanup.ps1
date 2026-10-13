$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -notin @('x64', 'amd64')) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Ferramentas MSVC x64 não encontradas.' }
    & (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
$stage = Join-Path $nodivuRoot '.local/audio-cleanup'
$rnnoise = Join-Path $nodivuRoot 'third_party/rnnoise-0.2'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Push-Location $stage
try {
    $sources = @('denoise','rnn','pitch','kiss_fft','celt_lpc','nnet','nnet_default','parse_lpcnet_weights','rnnoise_data','rnnoise_tables')
    $files = $sources | ForEach-Object { "$rnnoise/src/$_.c" }
    # Portable C/SSE2 baseline, no mandatory AVX instruction or external model.
    & cl.exe /nologo /utf-8 /std:c11 /O2 /W3 /wd4244 /wd4305 /MT /c /D_CRT_SECURE_NO_WARNINGS /D__SSE2__ "/I$rnnoise/include" "/I$rnnoise/src" @files
    if ($LASTEXITCODE) { throw 'Build RNNoise falhou.' }
    $objects = $sources | ForEach-Object { "$stage/$_.obj" }
    foreach ($name in @('voice-cleanup','peak-limiter')) {
        $limiter = if ($name -eq 'peak-limiter') { 1 } else { 0 }
        & cl.exe /nologo /utf-8 /std:c++17 /EHsc /O2 /W4 /WX /LD /MT "/DNODIVU_LIMITER=$limiter" "/I$rnnoise/include" "/I$nodivuRoot/third_party/clap-1.2.2/include" "$nodivuRoot/plugins/audio-cleanup/clap_plugin.cpp" @objects /link "/OUT:$name.clap"
        if ($LASTEXITCODE) { throw "Build $name falhou." }
        $destination=Join-Path $nodivuRoot ".local/plugins/$name"
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -LiteralPath "$stage/$name.clap" -Destination $destination
        foreach ($asset in @('plugin.json','nodivu-package.json','README.md')) {
            Copy-Item -LiteralPath "$nodivuRoot/plugins/audio-cleanup/$name/$asset" -Destination "$destination/$asset"
        }
        Copy-Item -LiteralPath "$nodivuRoot/LICENSE" -Destination "$destination/LICENSE"
        Copy-Item -LiteralPath "$rnnoise/COPYING" -Destination "$destination/RNNOISE-LICENSE"
        Copy-Item -LiteralPath "$nodivuRoot/third_party/clap-1.2.2/LICENSE" -Destination "$destination/CLAP-LICENSE"
    }
} finally { Pop-Location }

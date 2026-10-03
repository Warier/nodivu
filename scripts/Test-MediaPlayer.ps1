param([Parameter(Mandatory=$true)][string]$Ffmpeg)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
$stage = Join-Path $nodivuRoot '.local/media-test'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
# Only synthetic fixtures; ffmpeg is test tooling, not shipped with the plugin.
foreach ($format in @('wav','mp3','m4a')) {
    & $Ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=3' -ac 2 "$stage/short.$format"
    if ($LASTEXITCODE) { throw "Fixture $format failed" }
}
foreach ($seconds in @(600,601)) {
    & $Ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'anullsrc=r=48000:cl=mono' -t $seconds "$stage/$seconds.wav"
    if ($LASTEXITCODE) { throw "Fixture $seconds failed" }
}
& "$PSScriptRoot/Build-Mp3Plugin.ps1"
Push-Location $stage
try {
    & cl.exe /nologo /utf-8 /std:c++17 /EHsc /O2 /W4 /MD "$nodivuRoot/plugins/mp3-player/tests/transport.cpp" "$nodivuRoot/plugins/mp3-player/player/player.cpp" "$nodivuRoot/plugins/mp3-player/player/decoder.cpp" /Fe:transport-test.exe /link mfplat.lib mfreadwrite.lib mfuuid.lib ole32.lib
    if ($LASTEXITCODE) { throw 'Player test build failed' }
    & "$stage/transport-test.exe" "$stage/short.wav" "$stage/short.mp3" "$stage/short.m4a" "$stage/600.wav" "$stage/601.wav"
    if ($LASTEXITCODE) { throw 'Player transport test failed' }
} finally { Pop-Location }

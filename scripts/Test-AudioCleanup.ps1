$ErrorActionPreference='Stop'
$nodivuRoot=Split-Path $PSScriptRoot -Parent
& "$PSScriptRoot/Build-AudioCleanup.ps1"
$stage=Join-Path $nodivuRoot '.local/audio-cleanup'
$objects=@('denoise','rnn','pitch','kiss_fft','celt_lpc','nnet','nnet_default','parse_lpcnet_weights','rnnoise_data','rnnoise_tables') | ForEach-Object { "$stage/$_.obj" }
Push-Location $stage
try {
    & cl.exe /nologo /utf-8 /std:c++17 /EHsc /O2 /W4 /WX /MT "/I$nodivuRoot/third_party/rnnoise-0.2/include" "$nodivuRoot/plugins/audio-cleanup/test.cpp" @objects /link /OUT:cleanup-test.exe
    if($LASTEXITCODE){throw 'Compilação do ensaio DSP falhou.'}
    & "$stage/cleanup-test.exe"
    if($LASTEXITCODE){throw 'Ensaio DSP falhou.'}
} finally {Pop-Location}

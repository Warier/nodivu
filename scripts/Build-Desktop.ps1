param([switch]$FrontendOnly, [switch]$Test, [switch]$Clean)
#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
Push-Location $nodivuRoot
try {
    if (!$FrontendOnly) {
        if (!(Get-Command cargo -ErrorAction SilentlyContinue)) {
            $nodivuLocalCargo = Join-Path $nodivuRoot '.tools/rust/cargo/bin/cargo.exe'
            if (!(Test-Path -LiteralPath $nodivuLocalCargo)) { throw 'Instale Rust/rustup e adicione cargo ao PATH antes do build completo.' }
            $env:CARGO_HOME = Join-Path $nodivuRoot '.tools/rust/cargo'
            $env:RUSTUP_HOME = Join-Path $nodivuRoot '.tools/rust/rustup'
            $env:PATH = "$(Split-Path $nodivuLocalCargo -Parent);$env:PATH"
        }
        cargo rustc -p nodivu-app-backend --release --locked -- -C target-feature=+crt-static
        if ($LASTEXITCODE) { throw 'Falha ao compilar o backend existente.' }
        & "$PSScriptRoot/Build-Mp3Plugin.ps1"
        & "$PSScriptRoot/Build-WindowsAudioPlugin.ps1"
        & "$PSScriptRoot/Build-AudioCleanup.ps1"
    }
    & "$PSScriptRoot/qt/Build-Desktop.ps1" -Test:$Test -Clean:$Clean
} finally { Pop-Location }

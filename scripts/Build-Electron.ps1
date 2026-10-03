$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitOperatingSystem) {
    throw 'O aplicativo completo requer Windows x64. Para editar a interface use npm run dev:ui.'
}
foreach ($command in @('cargo', 'rustc', 'node', 'npm.cmd')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Instale $command e abra um novo terminal; consulte README.md." }
}
node -e "const [a,b]=process.versions.node.split('.').map(Number);if(a<22||(a===22&&b<12)){console.error('Node >=22.12 necessario');process.exit(1)}"
if ($LASTEXITCODE) { throw 'Versao do Node incompativel.' }
if ($env:CARGO_TARGET_DIR -or $env:CARGO_BUILD_TARGET) {
    throw 'Execute em um terminal sem CARGO_TARGET_DIR/CARGO_BUILD_TARGET; o app usa target/release.'
}
$nodivuOldCache = $env:electron_config_cache
$env:electron_config_cache = Join-Path $nodivuRoot '.tools/electron-cache'
Push-Location $nodivuRoot
try {
    # Also initializes the installed MSVC x64 environment before Rust linking.
    & "$PSScriptRoot/Build-Mp3Plugin.ps1"
    & "$PSScriptRoot/Build-WindowsAudioPlugin.ps1"
    # Static C runtime: no separate VC redistributable for this backend.
    cargo rustc -p nodivu-app-backend --release --locked -- -C target-feature=+crt-static
    if ($LASTEXITCODE) { throw 'Build Rust falhou.' }
    Set-Location "$nodivuRoot/apps/nodivu-electron"
    npm.cmd ci --no-fund --cache "$nodivuRoot/.tools/npm-cache"
    if ($LASTEXITCODE) { throw 'Instalação Electron falhou.' }
    npm.cmd test
    if ($LASTEXITCODE) { throw 'Teste IPC falhou.' }
    npm.cmd run package
    if ($LASTEXITCODE) { throw 'Preparação local Electron falhou.' }
} finally { Pop-Location; $env:electron_config_cache = $nodivuOldCache }

$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
& "$PSScriptRoot/Build-ClapFixture.ps1" -Scale 0.5
foreach ($relative in @('.local/plugins','.local/electron/plugins')) {
    $destination = Join-Path $nodivuRoot $relative
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -LiteralPath "$nodivuRoot/.local/clap-fixture/nodivu-fixture.clap" -Destination "$destination/nodivu-fixture.clap"
    Copy-Item -LiteralPath "$nodivuRoot/examples/clap-gain/plugin.json" -Destination "$destination/plugin.json"
    foreach ($asset in @('nodivu-package.json','README.md')) {
        Copy-Item -LiteralPath "$nodivuRoot/examples/clap-gain/$asset" -Destination "$destination/$asset"
    }
    Copy-Item -LiteralPath "$nodivuRoot/LICENSE" -Destination "$destination/LICENSE"
    Copy-Item -LiteralPath "$nodivuRoot/third_party/clap-1.2.2/LICENSE" -Destination "$destination/CLAP-LICENSE"
}
'Plugin de demonstração preparado. Reinicie o aplicativo para carregar.'

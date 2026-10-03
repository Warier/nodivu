$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
& "$PSScriptRoot/Build-ClapFixture.ps1"
foreach ($nodivuDestination in @('.local/plugins/consumer', '.local/electron/plugins/consumer')) {
    $nodivuTarget = Join-Path $nodivuRoot $nodivuDestination
    New-Item -ItemType Directory -Force -Path $nodivuTarget | Out-Null
    Copy-Item -LiteralPath "$nodivuRoot/.local/clap-fixture/nodivu-fixture.clap" -Destination "$nodivuTarget/nodivu-consumer.clap" -Force
    Copy-Item -LiteralPath "$nodivuRoot/examples/clap-consumer/plugin.json" -Destination "$nodivuTarget/plugin.json" -Force
}
Write-Output 'Plugin consumidor instalado; reinicie o aplicativo. Host não recompilado por este script.'

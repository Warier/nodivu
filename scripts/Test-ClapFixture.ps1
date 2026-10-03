$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
Push-Location $nodivuRoot
try {
    cargo build -p nodivu-clap-host --example clap_probe --release --locked
    if ($LASTEXITCODE) { throw 'Build do harness falhou.' }
    $probe = Join-Path $nodivuRoot 'target/release/examples/clap_probe.exe'
    $fixture = Join-Path $nodivuRoot '.local/clap-fixture/nodivu-fixture.clap'
    $hostHash = (Get-FileHash -LiteralPath $probe).Hash
    $reports = @()
    foreach ($scale in @('0.5', '0.25')) {
        & "$PSScriptRoot/Build-ClapFixture.ps1" -Scale $scale
        $report = & $probe $fixture $scale
        if ($LASTEXITCODE) { throw "Harness falhou com escala $scale." }
        $report | Set-Content -LiteralPath ".local/clap-probe-$scale.json" -Encoding utf8
        $result = $report -join "`n" | ConvertFrom-Json
        if ($result.verification.status -ne 'PASS') { throw 'Verificação CLAP não passou.' }
        if ((Get-FileHash -LiteralPath $probe).Hash -ne $hostHash) { throw 'Host mudou durante substituição de plugin.' }
        $reports += [pscustomobject]@{ scale = $scale; host_sha256 = $hostHash; plugin_sha256 = (Get-FileHash -LiteralPath $fixture).Hash; status = $result.verification.status }
    }
    if ($reports[0].plugin_sha256 -eq $reports[1].plugin_sha256) { throw 'Os algoritmos não geraram binários distintos.' }
    $reports | ConvertTo-Json | Set-Content -LiteralPath '.local/clap-rebuild-evidence.json' -Encoding utf8
    foreach ($negative in @(@('invalid-abi', 'abi'), @('missing-entry', 'entry'))) {
        & "$PSScriptRoot/Build-ClapFixture.ps1" -Variant $negative[0]
        & $probe --reject (Join-Path $nodivuRoot ".local/clap-fixture/$($negative[0]).clap") $negative[1]
        if ($LASTEXITCODE) { throw "Falha de rejeição: $($negative[0])" }
    }
    if ((Get-FileHash -LiteralPath $probe).Hash -ne $hostHash) { throw 'Host mudou durante casos negativos.' }
    'PASS: plugin recompilado com algoritmo diferente, host idêntico e ensaios completos nas duas versões.'
} finally { Pop-Location }

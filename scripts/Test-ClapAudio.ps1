param(
    [ValidateRange(3,60)][int]$Seconds = 12,
    [switch]$Audible,
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
Push-Location $nodivuRoot
try {
    if (-not $NoBuild) {
        cargo build -p nodivu-app-backend --example clap_audio --release --locked
        if ($LASTEXITCODE) { throw 'Build do ensaio WASAPI falhou.' }
        & "$PSScriptRoot/Build-ClapFixture.ps1" -Scale 0.5
    }
    $exe = Join-Path $nodivuRoot 'target/release/examples/clap_audio.exe'
    $plugin = Join-Path $nodivuRoot '.local/clap-fixture/nodivu-fixture.clap'
    $results = @()
    foreach ($mode in @('tone', 'capture', 'separate', 'process-fail', 'nan', 'restart')) {
        $out = Join-Path $nodivuRoot ".local/clap-wasapi-$mode.json"
        $err = Join-Path $nodivuRoot ".local/clap-wasapi-$mode.stderr.log"
        # Paths citados como argumentos literais; não são comandos montados para outro shell.
        $arguments = @(('"' + $plugin + '"'), '0.5', $mode, "$Seconds")
        if ($Audible -and $mode -in @('tone', 'capture', 'separate')) { $arguments += 'audible' }
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
        if (-not $process.WaitForExit(($Seconds + 10) * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Ensaio $mode excedeu prazo; processo de teste encerrado."
        }
        $process.Refresh()
        $reason = Get-Content -LiteralPath $err -Raw
        if ($mode -in @('tone', 'capture', 'separate')) {
            if ($process.ExitCode -ne 0) { throw "Ensaio $mode falhou: $reason" }
            $report = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json
            $results += [pscustomobject]@{ mode=$mode; status='PASS'; metrics=$report.metrics; checks=$report.phase_checks }
        } else {
            $expected = switch ($mode) {
                'process-fail' { 'ProcessStatus' }
                'nan' { 'amostra não finita' }
                'restart' { 'reativa' }
            }
            if ($process.ExitCode -eq 0 -or $reason -notmatch $expected) {
                throw "Ensaio negativo $mode não preservou causa esperada: $reason"
            }
            $results += [pscustomobject]@{ mode=$mode; status='EXPECTED_FAILURE'; reason=$reason.Trim() }
        }
    }
    [pscustomobject]@{
        results=$results
        host_sha256=(Get-FileHash -LiteralPath $exe).Hash
        plugin_sha256=(Get-FileHash -LiteralPath $plugin).Hash
    } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath '.local/clap-wasapi-summary.json' -Encoding utf8
    $results | Select-Object mode,status
} finally { Pop-Location }

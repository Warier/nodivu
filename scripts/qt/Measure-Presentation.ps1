# Optional acceptance tooling. No service, driver or group membership is installed.
param(
    [Parameter(Mandatory)][string]$PresentMon,
    [ValidateSet('simple','large')][string]$Scenario = 'large',
    [string]$OutputId = '',
    [switch]$OpenGL,
    [switch]$CompareRenderers,
    [switch]$ElevateCollector
)
$ErrorActionPreference = 'Stop'
if ($OpenGL -and $CompareRenderers) { throw 'Use OpenGL ou CompareRenderers, não ambos.' }
$nodivuRoot = [IO.Path]::GetFullPath((Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$nodivuTool = [IO.Path]::GetFullPath($PresentMon)
# Published digest from the official GameTechDev/PresentMon v2.6.0 release API.
$nodivuToolHash = 'b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af'
if ((Get-FileHash -LiteralPath $nodivuTool -Algorithm SHA256).Hash -ne $nodivuToolHash) { throw 'PresentMon 2.6.0 x64 não corresponde ao SHA256 fixado.' }
$nodivuRunId = [Guid]::NewGuid().ToString('N')
$nodivuOutput = Join-Path $nodivuRoot ('.local/qt-presentation-' + $nodivuRunId)
$nodivuBin = Join-Path $nodivuOutput 'bin'
New-Item -ItemType Directory -Path $nodivuBin | Out-Null
$nodivuImageName = 'NodivuFrameTest-' + $nodivuRunId + '.exe'
$nodivuTest = Join-Path $nodivuBin $nodivuImageName
Copy-Item -LiteralPath (Join-Path $nodivuRoot '.local/qt-build/release/tests/native_visual_acceptance.exe') -Destination $nodivuTest
Copy-Item -LiteralPath (Join-Path $nodivuRoot '.local/qt-build/release/tests/windows_accessibility_actor.exe') -Destination $nodivuBin
$nodivuCsv = Join-Path $nodivuOutput 'presentation.csv'
$nodivuCaptureSeconds = if ($CompareRenderers) { '120' } else { '70' }
$nodivuCollectorArguments = @('--process_name', $nodivuImageName, '--qpc_time_ms', '--timed', $nodivuCaptureSeconds, '--terminate_after_timed', '--no_console_stats', '--session_name', ('Nodivu.QtAcceptance.' + $nodivuRunId), '--output_file', ('"' + $nodivuCsv + '"'))
$nodivuResult = [ordered]@{
    schema_version = 1
    status = 'failed'
    scope = 'Five repetitions per renderer in owned processes; attempted ETW display/GPU coverage, no DwmFlush pacing; frontend/backend CPU and private commit measured separately.'
    presentmon_sha256 = $nodivuToolHash
    runner_sha256 = (Get-FileHash -LiteralPath $nodivuTest -Algorithm SHA256).Hash.ToLowerInvariant()
    elevated_collector = [bool]$ElevateCollector
    scenario = $Scenario
    active_audio = [bool]$OutputId
    experimental_opengl = [bool]$OpenGL
    compare_renderers = [bool]$CompareRenderers
    runs = @()
    error = ''
}
$nodivuPreviousPath = $env:PATH
$nodivuCollector = $null
try {
    $env:PATH = (Join-Path $nodivuRoot '.tools/Qt/bin') + ';' + $env:PATH
    if ($ElevateCollector) {
        # Only the hash-verified, standalone collector requests UAC. The test GUI
        # and backend keep the caller's normal rights and temporary profiles.
        $nodivuCollector = Start-Process -FilePath $nodivuTool -ArgumentList $nodivuCollectorArguments -Verb RunAs -WindowStyle Hidden -PassThru
    } else {
        $nodivuCollector = Start-Process -FilePath $nodivuTool -ArgumentList $nodivuCollectorArguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $nodivuOutput 'collector.log') -RedirectStandardError (Join-Path $nodivuOutput 'collector-errors.log')
    }
    Start-Sleep -Seconds 2
    if ($nodivuCollector.HasExited) { throw "O coletor encerrou antes do ensaio: $($nodivuCollector.ExitCode). ETW exige administrador ou associação prévia a Performance Log Users." }
    $nodivuModes = if ($CompareRenderers) { @('raster','opengl') } elseif ($OpenGL) { @('opengl') } else { @('raster') }
    foreach ($nodivuMode in $nodivuModes) {
    foreach ($nodivuIndex in 1..5) {
        $nodivuEvidence = Join-Path $nodivuOutput ("$nodivuMode-$nodivuIndex.json")
        $nodivuArguments = @('--repository', ('"' + $nodivuRoot + '"'), '--external-trace', '--evidence', ('"' + $nodivuEvidence + '"'))
        if ($Scenario -eq 'large') { $nodivuArguments += '--large' }
        if ($OutputId) { $nodivuArguments += @('--output-id', ('"' + $OutputId + '"'), '--animate-meters') }
        if ($nodivuMode -eq 'opengl') { $nodivuArguments += '--opengl' }
        # Presentation requires a real visible window. A hidden startup window
        # suppresses native paints and cannot establish CPU/GPU/display results.
        $nodivuProcess = Start-Process -FilePath $nodivuTest -ArgumentList $nodivuArguments -WindowStyle Normal -PassThru -RedirectStandardOutput (Join-Path $nodivuOutput "$nodivuMode-$nodivuIndex.log") -RedirectStandardError (Join-Path $nodivuOutput "$nodivuMode-$nodivuIndex-errors.log")
        $nodivuDeadline = [DateTime]::UtcNow.AddSeconds(40)
        while (!$nodivuProcess.HasExited -and [DateTime]::UtcNow -lt $nodivuDeadline) { Start-Sleep -Milliseconds 100 }
        if (!$nodivuProcess.HasExited) {
            # This exact handle belongs to the test started just above.
            $nodivuProcess.Kill()
            throw "A janela de teste $nodivuIndex excedeu 40 segundos."
        }
        $nodivuProcess.WaitForExit()
        if ($nodivuProcess.ExitCode -ne 0) { throw "O cenário funcional falhou: $nodivuEvidence" }
        $nodivuResult.runs += [ordered]@{ pid = $nodivuProcess.Id; renderer = $nodivuMode; evidence = [IO.Path]::GetFileName($nodivuEvidence) }
        Write-Output "PASS: $nodivuMode interação $nodivuIndex/5; PID $($nodivuProcess.Id); $nodivuOutput"
    }
    }
    $nodivuDeadline = [DateTime]::UtcNow.AddSeconds(110)
    while (!$nodivuCollector.HasExited -and [DateTime]::UtcNow -lt $nodivuDeadline) { Start-Sleep -Milliseconds 250 }
    if (!$nodivuCollector.HasExited -or $nodivuCollector.ExitCode -ne 0) { throw 'Coletor não encerrou normalmente; não interrompa outras sessões ETW.' }
    if (!(Test-Path -LiteralPath $nodivuCsv) -or (Get-Item -LiteralPath $nodivuCsv).Length -le 100) { throw 'Sem quadros ETW atribuíveis ao processo; não interpretar ausência de dados como zero.' }
    $nodivuRows = @(Import-Csv -LiteralPath $nodivuCsv)
    if (!$nodivuRows.Count -or !$nodivuRows[0].PSObject.Properties['CPUStartQPCTimeInMs']) { throw 'Coluna QPC do PresentMon 2.6.0 não encontrada.' }
    $nodivuIncompleteCoverage = $false
    foreach ($nodivuRun in $nodivuResult.runs) {
        $nodivuScenario = Get-Content -LiteralPath (Join-Path $nodivuOutput $nodivuRun.evidence) -Raw | ConvertFrom-Json
        $nodivuRun.frames = @($nodivuRows | Where-Object {
            [int]$_.ProcessID -eq $nodivuRun.pid -and
            [double]::Parse($_.CPUStartQPCTimeInMs, [Globalization.CultureInfo]::InvariantCulture) -ge $nodivuScenario.interaction_start_qpc_ms -and
            [double]::Parse($_.CPUStartQPCTimeInMs, [Globalization.CultureInfo]::InvariantCulture) -le $nodivuScenario.interaction_end_qpc_ms
        }).Count
        $nodivuRun.coverage = if ($nodivuRun.frames -lt 30) { 'insufficient' } else { 'captured' }
        if ($nodivuRun.frames -lt 30) { $nodivuIncompleteCoverage = $true }
    }
    if ($nodivuIncompleteCoverage) {
        $nodivuResult.status = 'partial'
        throw 'ETW não forneceu quadros suficientes para todos os renderers. Consulte coverage por processo; ausência de dados não comprova desempenho.'
    }
    $nodivuResult.status = 'captured'
} catch {
    $nodivuResult.error = $_.Exception.Message
    throw
} finally {
    $env:PATH = $nodivuPreviousPath
    $nodivuResult | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $nodivuOutput 'capture-result.json') -Encoding utf8
    Write-Output "Evidência: $nodivuOutput"
}
# 'captured' only confirms trace coverage; assess displayed intervals, missing
# GPU metrics, drop rate and CPU/private-byte measurements before acceptance.

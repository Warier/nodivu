param([Parameter(Mandatory)][string]$Stage)
$ErrorActionPreference = 'Stop'
$nodivuRoot = [IO.Path]::GetFullPath((Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$nodivuPackages = Join-Path $nodivuRoot '.local/qt-packages'
$nodivuStage = [IO.Path]::GetFullPath($Stage)
if (!$nodivuStage.StartsWith($nodivuPackages + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Informe o staging em .local/qt-packages.' }
$nodivuDistribution = Get-Content -LiteralPath (Join-Path $nodivuStage 'distribution.json') -Raw | ConvertFrom-Json
foreach ($nodivuFile in $nodivuDistribution.files) {
    $nodivuPath = [IO.Path]::GetFullPath((Join-Path $nodivuStage $nodivuFile.path))
    if (!$nodivuPath.StartsWith($nodivuStage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        (Get-FileHash -LiteralPath $nodivuPath -Algorithm SHA256).Hash -ne $nodivuFile.sha256) { throw "Hash/caminho divergente: $($nodivuFile.path)" }
}
$nodivuEvidence = Join-Path $nodivuRoot '.local/qt-migration'
New-Item -ItemType Directory -Path $nodivuEvidence -Force | Out-Null
$nodivuScreenshot = Join-Path $nodivuEvidence 'qt-packaged-final.png'
$nodivuExecutable = Join-Path $nodivuStage 'nodivu.exe'
$nodivuOriginalPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $nodivuStarted = [DateTime]::UtcNow
    # GUI subsystem executables need an explicit wait in PowerShell. Otherwise
    # LASTEXITCODE can still belong to an earlier console program.
    $nodivuProcess = Start-Process -FilePath $nodivuExecutable -ArgumentList @('--portable','--smoke-test','--screenshot',('"' + $nodivuScreenshot + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($nodivuProcess.ExitCode -ne 0) { throw "Smoke do pacote falhou: $($nodivuProcess.ExitCode)" }
    $nodivuCapture = Get-Item -LiteralPath $nodivuScreenshot
    if ($nodivuCapture.Length -eq 0 -or $nodivuCapture.LastWriteTimeUtc -lt $nodivuStarted.AddSeconds(-1)) { throw 'O ensaio não produziu uma captura nova.' }
    $nodivuProcess = Start-Process -FilePath $nodivuExecutable -ArgumentList @('--portable','--repository',('"' + $nodivuRoot + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($nodivuProcess.ExitCode -ne 2) { throw "Opções incompatíveis não foram recusadas: $($nodivuProcess.ExitCode)" }
} finally { $env:PATH = $nodivuOriginalPath }
Write-Output 'PASS: inventário, execução com DLLs próprias, projeto/screenshot/shutdown e opções incompatíveis.'

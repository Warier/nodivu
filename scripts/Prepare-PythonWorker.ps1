param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
# Resolve the actual interpreter once. No pip, model download or host rebuild.
$nodivuPython = & $Python -I -c 'import sys; print(sys.executable)'
if ($LASTEXITCODE -or -not (Test-Path -LiteralPath $nodivuPython -PathType Leaf)) { throw 'Python não encontrado.' }
$nodivuManifest = Get-Content -LiteralPath "$nodivuRoot/examples/python-worker/worker.json" -Raw | ConvertFrom-Json
$nodivuManifest.python = $nodivuPython.Trim()
foreach ($nodivuFolder in @('.local/plugins/python-gain','.local/electron/plugins/python-gain')) {
    $nodivuDestination = Join-Path $nodivuRoot $nodivuFolder
    New-Item -ItemType Directory -Force -Path $nodivuDestination | Out-Null
    foreach ($nodivuFile in @('worker.py','dsp.py')) {
        Copy-Item -LiteralPath "$nodivuRoot/examples/python-worker/$nodivuFile" -Destination $nodivuDestination
    }
    [System.IO.File]::WriteAllText("$nodivuDestination/worker.json", ($nodivuManifest | ConvertTo-Json), [System.Text.UTF8Encoding]::new($false))
}
Write-Output "Worker instalado nas pastas locais. Python: $nodivuPython. Feche e reabra o app."

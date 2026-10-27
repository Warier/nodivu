#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedInventorySha256,
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('nodivu-acceptance-' + [Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$nodivuPackage = [IO.Path]::GetFullPath($PackageDirectory)
$nodivuOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $nodivuOutput) { throw 'Use uma pasta de evidências nova; resultados anteriores não serão sobrescritos.' }
New-Item -ItemType Directory -Path $nodivuOutput | Out-Null
$nodivuResult = [ordered]@{
    schema_version = 1
    started_utc = [DateTime]::UtcNow.ToString('o')
    status = 'failed'
    scope = 'Inventário e inicialização/projeto/shutdown em perfil temporário, sem SDK no PATH. Não comprova áudio, instalador, DPI, estabilidade ou máquina limpa.'
    windows_version = [Environment]::OSVersion.Version.ToString()
    platform = ''
    inventory_sha256 = $ExpectedInventorySha256.ToLowerInvariant()
    package_version = ''
    checked_files = 0
    process_exit_code = $null
    error = ''
}
$nodivuOriginalPath = $env:PATH
try {
    if (!$IsWindows -or ![Environment]::Is64BitOperatingSystem) { throw 'Este ensaio exige Windows x64.' }
    $nodivuWindows = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    if ($nodivuWindows.InstallationType -ne 'Client') { throw 'Windows Server não comprova aceite Windows 10/11.' }
    $nodivuBuild = [Environment]::OSVersion.Version.Build
    if ($nodivuBuild -lt 19041) { throw 'Este candidato exige pelo menos Windows 10 2004; captura de aplicativos precisa de aceite separado.' }
    $nodivuResult.platform = if ($nodivuBuild -ge 22000) { 'Windows 11 x64' } else { 'Windows 10 x64' }
    $nodivuInventoryPath = Join-Path $nodivuPackage 'distribution.json'
    if ((Get-Item -LiteralPath $nodivuInventoryPath).Length -gt 2MB -or
        (Get-FileHash -LiteralPath $nodivuInventoryPath -Algorithm SHA256).Hash -ne $ExpectedInventorySha256) { throw 'Inventário não corresponde ao SHA256 informado.' }
    $nodivuInventory = Get-Content -LiteralPath $nodivuInventoryPath -Raw | ConvertFrom-Json
    if ($nodivuInventory.schema_version -ne 1 -or $nodivuInventory.frontend -ne 'Qt Widgets/C++20' -or
        $nodivuInventory.files.Count -lt 1 -or $nodivuInventory.files.Count -gt 8192) { throw 'Inventário incompatível ou fora dos limites.' }
    $nodivuSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($nodivuEntry in $nodivuInventory.files) {
        $nodivuRelative = [string]$nodivuEntry.path
        $nodivuInvalidParts = @($nodivuRelative.Split('/') | Where-Object { $_ -in @('', '.', '..') -or $_ -match '[<>:"|?*]' })
        if (!$nodivuSeen.Add($nodivuRelative) -or !$nodivuRelative -or $nodivuRelative.Contains('\') -or
            [IO.Path]::IsPathRooted($nodivuRelative) -or $nodivuInvalidParts.Count -ne 0) {
            throw 'Caminho inválido ou duplicado no inventário.'
        }
        $nodivuPath = [IO.Path]::GetFullPath((Join-Path $nodivuPackage $nodivuRelative))
        if (!$nodivuPath.StartsWith($nodivuPackage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Caminho fora do pacote.' }
        $nodivuAncestor = Get-Item -LiteralPath $nodivuPath
        while ($nodivuAncestor -and $nodivuAncestor.FullName.StartsWith($nodivuPackage, [StringComparison]::OrdinalIgnoreCase)) {
            if ($nodivuAncestor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'O pacote contém link/junção.' }
            $nodivuAncestor = if ($nodivuAncestor.PSIsContainer) { $nodivuAncestor.Parent } else { $nodivuAncestor.Directory }
        }
        if ((Get-Item -LiteralPath $nodivuPath).Length -ne $nodivuEntry.bytes -or
            (Get-FileHash -LiteralPath $nodivuPath -Algorithm SHA256).Hash -ne $nodivuEntry.sha256) { throw "Arquivo divergente: $nodivuRelative" }
        $nodivuResult.checked_files++
    }
    foreach ($nodivuRequired in @('nodivu.exe', 'bin/nodivu-app-backend.exe', 'Qt6Widgets.dll')) {
        if (!$nodivuSeen.Contains($nodivuRequired)) { throw "Arquivo obrigatório ausente: $nodivuRequired" }
    }
    # Windows can resolve DLLs from the application directory. Checking only
    # listed hashes would miss an extra DLL injected beside the real executable.
    $nodivuDirectories = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $nodivuDirectories.Push([IO.DirectoryInfo]::new($nodivuPackage))
    $nodivuEntries = 0
    while ($nodivuDirectories.Count -gt 0) {
        foreach ($nodivuEntry in $nodivuDirectories.Pop().EnumerateFileSystemInfos()) {
            $nodivuEntries++
            if ($nodivuEntries -gt 32768 -or ($nodivuEntry.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Árvore do pacote excede limites ou contém link.' }
            $nodivuRelative = [IO.Path]::GetRelativePath($nodivuPackage, $nodivuEntry.FullName).Replace('\', '/')
            if ($nodivuRelative.Split('/').Count -gt 32) { throw 'Profundidade do pacote excedida.' }
            if ($nodivuEntry.Attributes -band [IO.FileAttributes]::Directory) {
                $nodivuDirectories.Push([IO.DirectoryInfo]$nodivuEntry)
            } elseif ($nodivuRelative -ne 'distribution.json' -and !$nodivuSeen.Contains($nodivuRelative)) {
                throw "Arquivo fora do inventário: $nodivuRelative. Use uma extração nova do candidato."
            }
        }
    }
    $nodivuResult.package_version = $nodivuInventory.version
    $nodivuStarted = [DateTime]::UtcNow
    $nodivuScreenshot = Join-Path $nodivuOutput 'studio.png'
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    $nodivuProcess = Start-Process -FilePath (Join-Path $nodivuPackage 'nodivu.exe') -ArgumentList @('--portable', '--smoke-test', '--screenshot', ('"' + $nodivuScreenshot + '"')) -WindowStyle Hidden -PassThru
    if (!$nodivuProcess.WaitForExit(30000)) {
        $nodivuProcess.Kill()
        $nodivuProcess.WaitForExit()
        throw 'O processo do ensaio excedeu 30 segundos e foi encerrado.'
    }
    $nodivuResult.process_exit_code = $nodivuProcess.ExitCode
    if ($nodivuProcess.ExitCode -ne 0) { throw "Aplicativo encerrou com código $($nodivuProcess.ExitCode)." }
    $nodivuCapture = Get-Item -LiteralPath $nodivuScreenshot
    if ($nodivuCapture.Length -le 0 -or $nodivuCapture.LastWriteTimeUtc -lt $nodivuStarted.AddSeconds(-1)) { throw 'Não foi produzida uma captura nova.' }
    $nodivuResult.status = 'passed'
} catch {
    $nodivuResult.error = $_.Exception.Message
} finally {
    $env:PATH = $nodivuOriginalPath
    $nodivuResult.finished_utc = [DateTime]::UtcNow.ToString('o')
    $nodivuResult | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $nodivuOutput 'result.json') -Encoding utf8
}
Write-Output "Evidência: $nodivuOutput"
if ($nodivuResult.status -ne 'passed') { throw $nodivuResult.error }
Write-Output "PASS: $($nodivuResult.platform), $($nodivuResult.checked_files) arquivos, projeto e encerramento. Demais gates continuam separados."

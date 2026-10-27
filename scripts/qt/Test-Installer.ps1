param([Parameter(Mandatory)][string]$Stage, [string]$LegacySource = '', [switch]$ViaUpdater)
$ErrorActionPreference = 'Stop'
$nodivuRoot = [IO.Path]::GetFullPath((Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$nodivuStage = [IO.Path]::GetFullPath($Stage)
$nodivuPackages = Join-Path $nodivuRoot '.local/qt-packages'
if (!$nodivuStage.StartsWith($nodivuPackages + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    !(Test-Path -LiteralPath (Join-Path $nodivuStage 'distribution.json'))) { throw 'Informe um staging verificado em .local/qt-packages.' }
$nodivuKey = 'HKCU:\Software\Nodivu.QtMigration.IsolatedTest'
$nodivuUninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Nodivu.QtMigration.IsolatedTest'
$nodivuShortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Nodivu Qt Migration Test.lnk'
if ((Test-Path -LiteralPath $nodivuKey) -or (Test-Path -LiteralPath $nodivuUninstallKey) -or (Test-Path -LiteralPath $nodivuShortcut)) { throw 'Há um ensaio anterior registrado. Inspecione antes de repetir.' }
$nodivuVersion = (Get-Content -LiteralPath (Join-Path $nodivuStage 'distribution.json') -Raw | ConvertFrom-Json).version
$nodivuInstaller = Join-Path $nodivuPackages 'Nodivu-isolated-install-test.exe'
$nodivuInclude = Join-Path $nodivuPackages ('uninstall-test-' + [Guid]::NewGuid().ToString('N') + '.nsh')
$nodivuLines = foreach ($nodivuFile in (Get-ChildItem -LiteralPath $nodivuStage -Recurse -File)) {
    'ClearErrors'
    'Delete "$INSTDIR\' + [IO.Path]::GetRelativePath($nodivuStage, $nodivuFile.FullName).Replace('$','$$').Replace('"','$\"') + '"'
    'IfErrors uninstall_failed'
}
$nodivuLines += foreach ($nodivuDirectory in (Get-ChildItem -LiteralPath $nodivuStage -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)) {
    'RMDir "$INSTDIR\' + [IO.Path]::GetRelativePath($nodivuStage, $nodivuDirectory.FullName).Replace('$','$$').Replace('"','$\"') + '"'
}
$nodivuLines | Set-Content -LiteralPath $nodivuInclude -Encoding utf8
$nodivuTestDefines = @('/V2', '/DMIGRATION_TEST')
if ($ViaUpdater) { $nodivuTestDefines += '/DUPDATE_TEST' }
& (Join-Path $nodivuRoot '.tools/nsis/nsis-3.13/makensis.exe') @nodivuTestDefines "/DSTAGE=$nodivuStage" "/DOUTPUT=$nodivuInstaller" "/DVERSION=$nodivuVersion" "/DUNINSTALL_FILES=$nodivuInclude" (Join-Path $PSScriptRoot 'desktop.nsi')
if ($LASTEXITCODE) { throw 'Falha ao compilar instalador de ensaio.' }
$nodivuInstall = [IO.Path]::GetFullPath((Join-Path $nodivuRoot ('.local/qt-install-test-' + [Guid]::NewGuid().ToString('N'))))
if (!$nodivuInstall.StartsWith($nodivuRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Destino de teste fora do workspace.' }
$nodivuSentinel = Join-Path $nodivuInstall 'user-project-preserve.nodivu.json'
$nodivuSentinelHash = ''
if ($LegacySource) {
    $nodivuLegacy = [IO.Path]::GetFullPath($LegacySource)
    if (!$nodivuLegacy.StartsWith($nodivuRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        !(Test-Path -LiteralPath (Join-Path $nodivuLegacy 'nodivu-electron.exe'))) { throw 'Informe uma referência Electron local dentro do workspace.' }
    $nodivuLegacyPackage = Get-Content -LiteralPath (Join-Path $nodivuLegacy 'resources/app/package.json') -Raw | ConvertFrom-Json
    if ($nodivuLegacyPackage.name -ne 'nodivu-electron') { throw 'Referência Electron não reconhecida.' }
    Copy-Item -LiteralPath $nodivuLegacy -Destination $nodivuInstall -Recurse
    '{"fixture":"user data must survive Electron to Qt upgrade"}' | Set-Content -LiteralPath $nodivuSentinel -Encoding utf8
    $nodivuSentinelHash = (Get-FileHash -LiteralPath $nodivuSentinel -Algorithm SHA256).Hash
    $nodivuLegacyHash = (Get-FileHash -LiteralPath (Join-Path $nodivuInstall 'nodivu-electron.exe') -Algorithm SHA256).Hash
    New-Item -Path $nodivuKey -Force | Out-Null
    New-Item -Path $nodivuUninstallKey -Force | Out-Null
    Set-ItemProperty -LiteralPath $nodivuKey -Name InstallLocation -Value $nodivuInstall
    Set-ItemProperty -LiteralPath $nodivuUninstallKey -Name DisplayVersion -Value '0.2.0'
    Set-ItemProperty -LiteralPath $nodivuUninstallKey -Name InstallLocation -Value $nodivuInstall
}
for ($nodivuAttempt = 0; $nodivuAttempt -lt 2; $nodivuAttempt++) {
    if ($nodivuAttempt -eq 1 -and $ViaUpdater) {
        $nodivuUpdateEvidence = Join-Path $nodivuPackages ('update-handoff-' + [Guid]::NewGuid().ToString('N') + '.json')
        $nodivuPreviousPath = $env:PATH
        try {
            $env:PATH = (Join-Path $nodivuRoot '.tools/Qt/bin') + ';' + $env:PATH
            & (Join-Path $nodivuRoot '.local/qt-build/release/tests/native_update_acceptance.exe') --install-dir $nodivuInstall --installer $nodivuInstaller --version $nodivuVersion --evidence $nodivuUpdateEvidence
            if ($LASTEXITCODE) { throw "Handoff autenticado falhou. Evidência: $nodivuUpdateEvidence" }
        } finally { $env:PATH = $nodivuPreviousPath }
        Start-Sleep -Seconds 2
        Write-Output "PASS: handoff TUF→helper→NSIS real: $nodivuUpdateEvidence"
    } else {
    $nodivuProcess = Start-Process -FilePath $nodivuInstaller -ArgumentList "/S /D=$nodivuInstall" -WindowStyle Hidden -Wait -PassThru
    if ($nodivuProcess.ExitCode -ne 0) { throw "Instalação/atualização falhou: $($nodivuProcess.ExitCode)" }
    }
    if ((Get-ItemProperty -LiteralPath $nodivuKey).InstallLocation -ne $nodivuInstall) { throw 'Registro de instalação incorreto.' }
    if (!(Test-Path -LiteralPath $nodivuShortcut)) { throw 'Atalho ausente.' }
    if ($nodivuAttempt -eq 0 -and !$LegacySource) {
        '{"fixture":"user data must survive install and uninstall"}' | Set-Content -LiteralPath $nodivuSentinel -Encoding utf8
        $nodivuSentinelHash = (Get-FileHash -LiteralPath $nodivuSentinel -Algorithm SHA256).Hash
    }
    if ($nodivuAttempt -eq 0 -and $LegacySource) {
        if ((Test-Path -LiteralPath (Join-Path $nodivuInstall 'nodivu-electron.exe')) -or
            (Test-Path -LiteralPath (Join-Path $nodivuInstall 'resources/app'))) { throw 'Runtime Electron permaneceu na instalação ativa.' }
        $nodivuReceipt = Get-Content -LiteralPath (Join-Path $nodivuInstall 'migration-receipt.json') -Raw | ConvertFrom-Json
        if ($nodivuReceipt.previous_frontend -ne 'Electron') { throw 'Migração Electron não foi registrada.' }
        $nodivuBackup = [IO.Path]::GetFullPath((Join-Path (Split-Path $nodivuInstall -Parent) $nodivuReceipt.backup))
        if (!$nodivuBackup.StartsWith($nodivuRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            (Get-FileHash -LiteralPath (Join-Path $nodivuBackup 'nodivu-electron.exe') -Algorithm SHA256).Hash -ne $nodivuLegacyHash) { throw 'Backup da versão Electron não confere.' }
    }
    foreach ($nodivuRelative in @('nodivu.exe','nodivu-update-helper.exe','bin/nodivu-app-backend.exe','Qt6Widgets.dll')) {
        if ((Get-FileHash -LiteralPath (Join-Path $nodivuInstall $nodivuRelative) -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $nodivuStage $nodivuRelative) -Algorithm SHA256).Hash) { throw "Arquivo instalado divergente: $nodivuRelative" }
    }
}
$nodivuUninstaller = Join-Path $nodivuInstall 'Uninstall Nodivu.exe'
$nodivuBusyCapture = Join-Path $nodivuInstall 'busy-uninstall-smoke.png'
$nodivuGui = Start-Process -FilePath (Join-Path $nodivuInstall 'nodivu.exe') -ArgumentList @('--portable', '--smoke-test', '--benchmark-ms', '60000', '--screenshot', ('"' + $nodivuBusyCapture + '"')) -WindowStyle Hidden -PassThru
try {
    $nodivuDeadline = [DateTime]::UtcNow.AddSeconds(20)
    while (!(Test-Path -LiteralPath $nodivuBusyCapture) -and !$nodivuGui.HasExited -and [DateTime]::UtcNow -lt $nodivuDeadline) { Start-Sleep -Milliseconds 100 }
    if (!(Test-Path -LiteralPath $nodivuBusyCapture) -or $nodivuGui.HasExited) { throw 'O aplicativo não ficou pronto para testar a recusa da desinstalação em uso.' }
    $nodivuBusyAttempt = Start-Process -FilePath $nodivuUninstaller -ArgumentList "/S _?=$nodivuInstall" -WindowStyle Hidden -Wait -PassThru
    if ($nodivuBusyAttempt.ExitCode -eq 0 -or !(Test-Path -LiteralPath (Join-Path $nodivuInstall 'nodivu.exe')) -or
        !(Test-Path -LiteralPath $nodivuKey) -or !(Test-Path -LiteralPath $nodivuShortcut)) { throw 'A desinstalação não preservou a instalação em uso.' }
} finally {
    if (!$nodivuGui.HasExited) {
        [void]$nodivuGui.CloseMainWindow()
        if (!$nodivuGui.WaitForExit(5000)) { $nodivuGui.Kill(); $nodivuGui.WaitForExit() }
    }
}
$nodivuProcess = Start-Process -FilePath $nodivuUninstaller -ArgumentList "/S _?=$nodivuInstall" -WindowStyle Hidden -Wait -PassThru
if ($nodivuProcess.ExitCode -ne 0) { throw "Desinstalação falhou: $($nodivuProcess.ExitCode)" }
if ((Test-Path -LiteralPath $nodivuKey) -or (Test-Path -LiteralPath $nodivuUninstallKey) -or (Test-Path -LiteralPath $nodivuShortcut) -or
    (Test-Path -LiteralPath (Join-Path $nodivuInstall 'nodivu.exe'))) { throw 'Desinstalação não removeu seus próprios arquivos/registro/atalho.' }
if ((Get-FileHash -LiteralPath $nodivuSentinel -Algorithm SHA256).Hash -ne $nodivuSentinelHash) { throw 'Dado do usuário alterado.' }
Write-Output "PASS: instalação, reinstalação e desinstalação transacionais isoladas; recusa de desinstalação em uso; migração Electron=$([bool]$LegacySource); dado desconhecido preservado em $nodivuInstall"

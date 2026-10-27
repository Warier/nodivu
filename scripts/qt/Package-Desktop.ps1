param([switch]$Installer, [string]$Version = '')
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$nodivuVersion = (Get-Content -LiteralPath (Join-Path $nodivuRoot 'apps/nodivu-desktop/VERSION') -Raw).Trim()
if (!$Version) { $Version = $nodivuVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+(-(dev|beta)\.\d+)?$' -or $Version -ne $nodivuVersion) { throw 'A versão do pacote deve corresponder a apps/nodivu-desktop/VERSION.' }
& (Join-Path $PSScriptRoot 'Build-Desktop.ps1')
$nodivuVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nodivuVs = & $nodivuVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$nodivuCmake = Join-Path $nodivuVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$nodivuPackages = Join-Path $nodivuRoot '.local/qt-packages'
$nodivuStage = Join-Path $nodivuPackages "$Version-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $nodivuStage -Force | Out-Null
& $nodivuCmake --install (Join-Path $nodivuRoot '.local/qt-build/release') --prefix $nodivuStage
if ($LASTEXITCODE) { throw 'Falha na distribuição Qt.' }
$nodivuBackend = Join-Path $nodivuRoot 'target/release/nodivu-app-backend.exe'
New-Item -ItemType Directory -Path (Join-Path $nodivuStage 'bin') -Force | Out-Null
Copy-Item -LiteralPath $nodivuBackend -Destination (Join-Path $nodivuStage 'bin/nodivu-app-backend.exe')
foreach ($nodivuPlugin in @('mp3','windows-audio','voice-cleanup','peak-limiter')) {
    $nodivuSource = Join-Path $nodivuRoot ".local/plugins/$nodivuPlugin"
    if (!(Test-Path -LiteralPath (Join-Path $nodivuSource 'plugin.json'))) { throw "Pacote ausente: $nodivuPlugin" }
    Copy-Item -LiteralPath $nodivuSource -Destination (Join-Path $nodivuStage "plugins/$nodivuPlugin") -Recurse
}
$nodivuCrt = Get-ChildItem -LiteralPath (Join-Path $nodivuVs 'VC/Redist/MSVC') -Directory |
    Where-Object { $nodivuParsedVersion = $null; [Version]::TryParse($_.Name, [ref]$nodivuParsedVersion) -and $nodivuParsedVersion -ge [Version]'14.42' } |
    Sort-Object { [Version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'vcruntime140.dll') } |
    Select-Object -First 1
if (!$nodivuCrt) { throw 'Runtime MSVC x64 app-local 14.42+ não encontrado.' }
Get-ChildItem -LiteralPath $nodivuCrt -Filter '*.dll' | Copy-Item -Destination $nodivuStage
& (Join-Path $PSScriptRoot 'Package-Licenses.ps1') -Destination $nodivuStage
@('@echo off', 'start "" "%~dp0nodivu.exe" --portable') | Set-Content -LiteralPath (Join-Path $nodivuStage 'Nodivu Portable.cmd') -Encoding ascii
@('Nodivu Qt/C++ - pacote de desenvolvimento', '', 'Para avaliar sem usar o perfil Electron, execute Nodivu Portable.cmd ou nodivu.exe --portable.', 'O perfil separado fica em portable-profile ao lado do executável.', 'Não instalar sobre a versão de uso diário antes do aceite de migração. Consulte apps/nodivu-desktop/README.md no código-fonte.') |
    Set-Content -LiteralPath (Join-Path $nodivuStage 'LEIA-ME-Qt.txt') -Encoding utf8
$nodivuFiles = @(Get-ChildItem -LiteralPath $nodivuStage -Recurse -File)
if ($nodivuFiles | Where-Object { $_.Name -match '^(electron|node)\.exe$|^(chrome|chromium).*\.(exe|dll)$|^Qt6(WebEngine|Qml|Quick|Test).*\.dll$' -or $_.Extension -in @('.js','.cjs','.mjs') }) {
    throw 'O staging contém uma dependência web ou de teste não permitida.'
}
$nodivuManifest = foreach ($nodivuFile in $nodivuFiles) {
    [ordered]@{path=[IO.Path]::GetRelativePath($nodivuStage,$nodivuFile.FullName).Replace('\','/');bytes=$nodivuFile.Length;sha256=(Get-FileHash -LiteralPath $nodivuFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
[ordered]@{schema_version=1;version=$Version;frontend='Qt Widgets/C++20';qt='6.12.0';backend_sha256=(Get-FileHash -LiteralPath $nodivuBackend -Algorithm SHA256).Hash;files=@($nodivuManifest)} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $nodivuStage 'distribution.json') -Encoding utf8
$nodivuOutput = Join-Path $nodivuPackages "Nodivu-$Version-windows-x64-qt-portable.zip"
Push-Location $nodivuStage
try { & $nodivuCmake -E tar cf $nodivuOutput --format=zip . } finally { Pop-Location }
if ($LASTEXITCODE) { throw 'Falha ao criar ZIP portátil.' }
if ($Installer) {
    $nodivuNsis = Join-Path $nodivuRoot '.tools/nsis/nsis-3.13/makensis.exe'
    if (!(Test-Path -LiteralPath $nodivuNsis)) { throw 'Prepare o NSIS 3.13 portátil verificado em .tools/nsis.' }
    # Uninstall only exact packaged files, never recurse into profile or user plugins.
    $nodivuUninstall = @()
    foreach ($nodivuFile in @(Get-ChildItem -LiteralPath $nodivuStage -Recurse -File)) {
        $nodivuRelative = [IO.Path]::GetRelativePath($nodivuStage,$nodivuFile.FullName).Replace('$','$$').Replace('"','$\"')
        $nodivuUninstall += 'ClearErrors'
        $nodivuUninstall += 'Delete "$INSTDIR\' + $nodivuRelative + '"'
        $nodivuUninstall += 'IfErrors uninstall_failed'
    }
    foreach ($nodivuDirectory in @(Get-ChildItem -LiteralPath $nodivuStage -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)) {
        $nodivuRelative = [IO.Path]::GetRelativePath($nodivuStage,$nodivuDirectory.FullName).Replace('$','$$').Replace('"','$\"')
        $nodivuUninstall += 'RMDir "$INSTDIR\' + $nodivuRelative + '"'
    }
    $nodivuUninstallFile = Join-Path $nodivuPackages 'uninstall-files.nsh'
    $nodivuUninstall | Set-Content -LiteralPath $nodivuUninstallFile -Encoding utf8
    $nodivuInstaller = Join-Path $nodivuPackages "Nodivu-$Version-windows-x64-qt-setup.exe"
    & $nodivuNsis /V2 "/DSTAGE=$nodivuStage" "/DOUTPUT=$nodivuInstaller" "/DVERSION=$Version" "/DUNINSTALL_FILES=$nodivuUninstallFile" (Join-Path $PSScriptRoot 'desktop.nsi')
    if ($LASTEXITCODE) { throw 'Falha ao compilar instalador nativo.' }
    Get-FileHash -LiteralPath $nodivuInstaller -Algorithm SHA256
}
Write-Output "Staging: $nodivuStage"
Get-FileHash -LiteralPath $nodivuOutput -Algorithm SHA256

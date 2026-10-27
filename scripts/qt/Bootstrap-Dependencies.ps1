param([switch]$Installer, [switch]$VerifyDownloads)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$nodivuTools = [IO.Path]::GetFullPath((Join-Path $nodivuRoot '.tools'))
$nodivuDownloads = Join-Path $nodivuTools 'downloads'
$nodivuManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Raw | ConvertFrom-Json
$nodivuArchives = @($nodivuManifest.archives | Where-Object { !$_.installer_only -or $Installer })
New-Item -ItemType Directory -Path $nodivuDownloads -Force | Out-Null
foreach ($nodivuArchive in $nodivuArchives) {
    $nodivuFile = Join-Path $nodivuDownloads $nodivuArchive.file
    if (!(Test-Path -LiteralPath $nodivuFile)) {
        if ($VerifyDownloads) { throw "Download ausente: $nodivuFile" }
        $nodivuTemporary = "$nodivuFile.partial"
        Invoke-WebRequest -Uri $nodivuArchive.url -OutFile $nodivuTemporary -MaximumRedirection 8
        if ((Get-FileHash -LiteralPath $nodivuTemporary -Algorithm SHA256).Hash -ne $nodivuArchive.sha256) {
            throw "SHA256 incorreto: $($nodivuArchive.component). Nenhum SDK foi instalado."
        }
        Move-Item -LiteralPath $nodivuTemporary -Destination $nodivuFile
    }
    if ((Get-FileHash -LiteralPath $nodivuFile -Algorithm SHA256).Hash -ne $nodivuArchive.sha256) {
        throw "SHA256 incorreto: $nodivuFile"
    }
    Write-Output "Verificado: $($nodivuArchive.component)"
}
if ($VerifyDownloads) { return }
$nodivuVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nodivuVs = & $nodivuVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$nodivuCmake = Join-Path $nodivuVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (!(Test-Path -LiteralPath $nodivuCmake)) { throw 'Instale MSVC 2022 com CMake/Ninja.' }
foreach ($nodivuGroup in ($nodivuArchives | Group-Object destination)) {
    $nodivuDestination = [IO.Path]::GetFullPath((Join-Path $nodivuTools $nodivuGroup.Name))
    $nodivuStage = [IO.Path]::GetFullPath((Join-Path $nodivuTools ('sdk-stage-' + [Guid]::NewGuid().ToString('N'))))
    if (!$nodivuDestination.StartsWith($nodivuTools + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        !$nodivuStage.StartsWith($nodivuTools + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Destino fora de .tools.' }
    if (Test-Path -LiteralPath $nodivuDestination) {
        Write-Output "Preservado SDK existente: $nodivuDestination"
        continue
    }
    New-Item -ItemType Directory -Path $nodivuStage | Out-Null
    Push-Location $nodivuStage
    try {
        foreach ($nodivuArchive in $nodivuGroup.Group) {
            & $nodivuCmake -E tar xf (Join-Path $nodivuDownloads $nodivuArchive.file)
            if ($LASTEXITCODE) { throw "Falha ao extrair $($nodivuArchive.component). Staging preservado: $nodivuStage" }
        }
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'dependencies.json') -Destination (Join-Path $nodivuStage 'nodivu-sdk-source.json')
    } finally { Pop-Location }
    # Both absolute paths were checked inside the intended .tools directory.
    Move-Item -LiteralPath $nodivuStage -Destination $nodivuDestination
    Write-Output "Preparado: $nodivuDestination"
}

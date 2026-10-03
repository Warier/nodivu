param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$nodivuRoot=Split-Path $PSScriptRoot -Parent
Push-Location $nodivuRoot
try {
    if(-not $SkipBuild){& "$PSScriptRoot/Build-Electron.ps1"}
    $downloads=Join-Path $nodivuRoot '.tools/downloads'
    New-Item -ItemType Directory -Force -Path $downloads | Out-Null
    $zip=Join-Path $downloads 'VBCABLE_Driver_Pack45.zip'
    if(-not(Test-Path -LiteralPath $zip)){
        Invoke-WebRequest 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip' -OutFile $zip
    }
    if((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne 'B950E39F01AF1D04EA623C8F6D8EB9B6EA5C477C637295FABF20631C85116BFB'){
        throw 'Checksum VB-CABLE inesperado; pacote não será distribuído.'
    }
    Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $nodivuRoot '.tools/vb-cable') -Force
    node scripts/release/prepare.cjs
    if($LASTEXITCODE){throw 'Preparação dos avisos/driver falhou'}
    # Build outside synced folders: Electron extraction uses an atomic directory rename.
    $stageOutput=Join-Path ([IO.Path]::GetTempPath()) ('Nodivu-release-'+[guid]::NewGuid().ToString())
    npm.cmd --prefix apps/nodivu-electron run dist -- "--config.directories.output=$stageOutput"
    if($LASTEXITCODE){throw 'Build NSIS falhou'}
    New-Item -ItemType Directory -Force .local/releases | Out-Null
    Get-ChildItem -LiteralPath $stageOutput -File | Where-Object {$_.Extension -in '.exe','.yml','.blockmap'} | Copy-Item -Destination .local/releases
    Get-ChildItem .local/releases -File | Where-Object {$_.Extension -in '.exe','.yml','.blockmap'} | Select-Object Name,Length
} finally {Pop-Location}

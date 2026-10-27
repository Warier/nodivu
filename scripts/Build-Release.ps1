param([switch]$SkipBuild)
#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
if (!$SkipBuild) { & "$PSScriptRoot/Build-Desktop.ps1" -Test }
& "$PSScriptRoot/qt/Package-Desktop.ps1" -Installer

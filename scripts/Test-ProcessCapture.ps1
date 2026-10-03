param([Parameter(Mandatory=$true)][string]$RenderId, [switch]$VerifyExclusion)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
& "$PSScriptRoot/Build-ProcessCapture.ps1"
if ($LASTEXITCODE) { throw 'Build falhou.' }
# Uses only the explicit endpoint. No device/default/volume modifications.
# Prefer the installed cable: physical outputs will audibly play test tones.
$arguments = @('--render-id', $RenderId)
if ($VerifyExclusion) { $arguments += '--verify-exclusion' }
& "$nodivuRoot/.local/process-capture/process-capture-test.exe" @arguments
if ($LASTEXITCODE) { throw 'Ensaio Windows falhou; preserve HRESULT/diagnóstico acima.' }

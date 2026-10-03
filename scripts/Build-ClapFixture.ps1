param(
    [ValidateSet('0.5', '0.25')][string]$Scale = '0.5',
    [ValidateSet('normal', 'invalid-abi', 'missing-entry')][string]$Variant = 'normal'
)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path $PSScriptRoot -Parent
# Test-ClapFixture chama este script várias vezes: não acumular PATH/INCLUDE do DevShell.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -notin @('x64', 'amd64')) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Ferramentas MSVC x64 não encontradas.' }
    & (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -notin @('x64', 'amd64')) {
        throw 'Inicialização do ambiente MSVC x64 falhou.'
    }
}
$stage = Join-Path $nodivuRoot '.local/clap-fixture'
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Push-Location $stage
try {
    $defines = @("/DGAIN_SCALE=${Scale}f")
    $fileName = 'nodivu-fixture.clap'
    if ($Variant -eq 'invalid-abi') { $defines += '/DINVALID_CLAP_ABI'; $fileName = 'invalid-abi.clap' }
    if ($Variant -eq 'missing-entry') { $defines += '/DNO_CLAP_ENTRY'; $fileName = 'missing-entry.clap' }
    & cl.exe /nologo /std:c11 /O2 /W4 /WX /LD /MD @defines "/I$nodivuRoot/third_party/clap-1.2.2/include" "$nodivuRoot/examples/clap-gain/plugin.c" /link "/OUT:$fileName"
    if ($LASTEXITCODE) { throw 'Compilação da fixture C falhou.' }
    Get-FileHash -LiteralPath (Join-Path $stage $fileName)
} finally { Pop-Location }

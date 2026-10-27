param([switch]$Test, [switch]$Deploy, [switch]$Clean)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$nodivuRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$nodivuVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nodivuVisualStudio = & $nodivuVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $nodivuVisualStudio) { throw 'Instale as ferramentas MSVC 2022 x64.' }
& (Join-Path $nodivuVisualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$nodivuCmakeRoot = Join-Path $nodivuVisualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake'
$env:PATH = "$nodivuCmakeRoot/CMake/bin;$nodivuCmakeRoot/Ninja;$nodivuRoot/.tools/Qt/bin;$env:PATH"
Push-Location (Join-Path $nodivuRoot 'apps/nodivu-desktop')
try {
    cmake --preset windows-release
    if ($LASTEXITCODE) { throw 'Falha na configuração CMake.' }
    if ($Clean) { cmake --build --preset windows-release --clean-first }
    else { cmake --build --preset windows-release }
    if ($LASTEXITCODE) { throw 'Falha na compilação Qt/C++.' }
    if ($Test) {
        ctest --preset windows-release
        if ($LASTEXITCODE) { throw 'Falha nos testes Qt/C++.' }
    }
    if ($Deploy) {
        cmake --install "$nodivuRoot/.local/qt-build/release"
        if ($LASTEXITCODE) { throw 'Falha ao preparar distribuição Qt.' }
    }
} finally { Pop-Location }

param([Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference = 'Stop'
$nodivuRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$nodivuLicenses = Join-Path $Destination 'third-party/licenses'
New-Item -ItemType Directory -Path $nodivuLicenses -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $nodivuRoot 'LICENSE') -Destination (Join-Path $Destination 'LICENSE-Nodivu.txt')
Copy-Item -LiteralPath (Join-Path $nodivuRoot 'apps/nodivu-desktop/THIRD-PARTY.md') -Destination (Join-Path $Destination 'THIRD-PARTY.md')
foreach ($nodivuDependency in @('monocypher-4.0.3', 'miniz-3.1.2', 'clap-1.2.2', 'rnnoise-0.2')) {
    $nodivuSource = Join-Path $nodivuRoot "third_party/$nodivuDependency"
    $nodivuTarget = Join-Path $nodivuLicenses $nodivuDependency
    New-Item -ItemType Directory -Path $nodivuTarget -Force | Out-Null
    Get-ChildItem -LiteralPath $nodivuSource -File | Where-Object Name -Match '^(LICENSE|LICENCE|COPYING|NOTICE|AUTHORS|SOURCE)' | Copy-Item -Destination $nodivuTarget
}
Copy-Item -LiteralPath (Join-Path $nodivuRoot '.tools/Qt/sbom') -Destination (Join-Path $nodivuLicenses 'Qt-SBOM') -Recurse
$nodivuQtLicenses = Join-Path $nodivuRoot 'third_party/qt-6.12.0-licenses'
if (!(Test-Path -LiteralPath $nodivuQtLicenses)) { throw 'Prepare os textos de licença Qt antes de distribuir.' }
Copy-Item -LiteralPath $nodivuQtLicenses -Destination (Join-Path $nodivuLicenses 'Qt') -Recurse
$env:PATH = "$(Join-Path $nodivuRoot '.tools/rust/cargo/bin');$env:PATH"
if (Test-Path (Join-Path $nodivuRoot '.tools/rust/rustup')) {
    $env:RUSTUP_HOME = Join-Path $nodivuRoot '.tools/rust/rustup'
    $env:CARGO_HOME = Join-Path $nodivuRoot '.tools/rust/cargo'
}
Push-Location $nodivuRoot
try {
    $nodivuMetadataText = cargo metadata --offline --locked --format-version 1
    if ($LASTEXITCODE) { throw 'Falha ao resolver o inventário Rust já instalado.' }
    $nodivuMetadata = $nodivuMetadataText | ConvertFrom-Json
    $nodivuInventory = @()
    foreach ($nodivuCrate in $nodivuMetadata.packages | Where-Object source) {
        $nodivuFolder = Split-Path $nodivuCrate.manifest_path -Parent
        $nodivuTexts = @(Get-ChildItem -LiteralPath $nodivuFolder | Where-Object Name -Match '^(LICENSE|COPYING|NOTICE)')
        if (!$nodivuTexts.Count) { throw "Avisos Rust ausentes: $($nodivuCrate.name)" }
        $nodivuCrateTarget = Join-Path $nodivuLicenses "rust/$($nodivuCrate.name)-$($nodivuCrate.version)"
        New-Item -ItemType Directory -Path $nodivuCrateTarget -Force | Out-Null
        $nodivuTexts | Copy-Item -Destination $nodivuCrateTarget -Recurse
        $nodivuInventory += [ordered]@{name=$nodivuCrate.name;version=$nodivuCrate.version;license=$nodivuCrate.license;repository=$nodivuCrate.repository}
    }
    $nodivuInventory | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $nodivuLicenses 'rust-dependencies.json') -Encoding utf8
    $nodivuSysroot = rustc --print sysroot
    if ($LASTEXITCODE) { throw 'Falha ao localizar licenças da biblioteca padrão.' }
    Copy-Item -LiteralPath (Join-Path $nodivuSysroot 'share/doc/rust/COPYRIGHT-library.html') -Destination (Join-Path $nodivuLicenses 'Rust-standard-library.html')
    Copy-Item -LiteralPath (Join-Path $nodivuSysroot 'share/doc/rust/licenses') -Destination (Join-Path $nodivuLicenses 'Rust-licenses') -Recurse
} finally { Pop-Location }

// Preserve notices in the locally assembled app; never reads installer assets.
const fs = require('node:fs/promises');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

module.exports = async function packageLicenses(root, stage) {
  const licenses = path.join(stage, 'third-party/licenses');
  await fs.mkdir(licenses, { recursive: true });
  await fs.copyFile(path.join(root, 'LICENSE'), path.join(stage, 'LICENSE-Nodivu.txt'));
  await fs.copyFile(path.join(root, 'THIRD-PARTY-NOTICES.md'), path.join(stage, 'THIRD-PARTY-NOTICES.md'));
  await fs.copyFile(path.join(root, 'third_party/clap-1.2.2/LICENSE'), path.join(licenses, 'CLAP-MIT.txt'));
  const options = { cwd: root, maxBuffer: 16 * 1024 * 1024, encoding: 'utf8', windowsHide: true };
  const metadata = JSON.parse(execFileSync('cargo', ['metadata', '--locked', '--format-version', '1'], options));
  const sysroot = execFileSync('rustc', ['--print', 'sysroot'], options).trim();
  await fs.cp(path.join(sysroot, 'share/doc/rust/COPYRIGHT-library.html'), path.join(licenses, 'Rust-standard-library.html'));
  await fs.cp(path.join(sysroot, 'share/doc/rust/licenses'), path.join(licenses, 'Rust-licenses'), { recursive: true });
  const index = [];
  for (const pkg of metadata.packages.filter(p => p.source)) {
    const folder = path.dirname(pkg.manifest_path);
    const names = (await fs.readdir(folder)).filter(n => /^(LICENSE|COPYING|NOTICE)/i.test(n));
    if (!names.length) throw new Error(`Avisos de licenca ausentes: ${pkg.name}`);
    for (const name of names) {
      await fs.cp(path.join(folder, name), path.join(licenses, `${pkg.name}-${pkg.version}`, name), { recursive: true });
    }
    index.push({ name: pkg.name, version: pkg.version, license: pkg.license, repository: pkg.repository });
  }
  await fs.writeFile(path.join(licenses, 'rust-dependencies.json'), JSON.stringify(index, null, 2) + '\n');
};

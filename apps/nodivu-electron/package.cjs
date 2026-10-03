const fs = require('node:fs/promises');
const path = require('node:path');
// Electron 44 baixa o runtime sob demanda; preparar também em um npm ci limpo.
const electronExecutable = require('electron');
(async () => {
  const root = path.resolve(__dirname, '../..');
  const stage = path.join(root, '.local/electron');
  const app = path.join(stage, 'resources/app');
  await fs.access(path.join(root, 'target/release/nodivu-app-backend.exe'));
  await fs.cp(path.dirname(electronExecutable), stage, { recursive: true });
  await fs.mkdir(path.join(app, 'bin'), { recursive: true });
  for (const file of ['smoke-update.cjs','updates.cjs','plugin-storage.cjs','diagnostics.cjs','smoke-startup.cjs','package.json', 'demo-engine.cjs', 'main.cjs', 'preload.cjs', 'engine.cjs', 'project-files.cjs', 'recent-projects.cjs', 'smoke.cjs','smoke-capture.cjs', 'smoke-virtual-cable.cjs','smoke-plugin.cjs','smoke-mp3.cjs','smoke-worker.cjs','smoke-routing.cjs','smoke-viewport.cjs','smoke-project.cjs'])
    await fs.copyFile(path.join(__dirname, file), path.join(app, file));
  await fs.cp(path.join(__dirname, 'demo'), path.join(app, 'demo'), {recursive:true});
  await fs.cp(path.join(__dirname, 'ui'), path.join(app, 'ui'), { recursive: true });
  await fs.copyFile(path.join(root, 'target/release/nodivu-app-backend.exe'), path.join(app, 'bin/nodivu-app-backend.exe'));
  for (const name of ['mp3', 'windows-audio']) {
    await fs.cp(path.join(root, '.local/plugins', name), path.join(stage, 'plugins', name), { recursive: true });
  }
  await require('../../scripts/package-licenses.cjs')(root, stage);
  await fs.rename(path.join(stage, 'electron.exe'), path.join(stage, 'nodivu-electron.exe'));
  console.log(`Aplicativo local: ${stage}/nodivu-electron.exe`);
})().catch(error => { console.error(error); process.exitCode = 1; });

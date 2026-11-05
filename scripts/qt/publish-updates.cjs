// Maintainer-only tool. Node's built-in crypto + Windows DPAPI; no runtime Node dependency.
// Usage: init FEED_DIR PRIVATE_DIR | publish FEED_DIR PRIVATE_DIR INSTALLER VERSION TAG
// FEED_DIR is the separate qt-updates branch. Private keys MUST live outside both repos.
'use strict';
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const roles = ['root', 'targets', 'snapshot', 'timestamp'];
const repo = path.resolve(__dirname, '../..');
const canonical = x => x === null || typeof x !== 'object' ? JSON.stringify(x)
  : Array.isArray(x) ? '[' + x.map(canonical).join(',') + ']'
  : '{' + Object.keys(x).sort().map(k => JSON.stringify(k) + ':' + canonical(x[k])).join(',') + '}';
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
const description = b => ({ length: b.length, hashes: { sha256: hash(b) } });
const expiry = days => new Date(Date.now() + days * 86400000).toISOString();
function keyPair() {
  const k = crypto.generateKeyPairSync('ed25519');
  const pub = k.publicKey.export({ type: 'spki', format: 'der' }).subarray(-32).toString('hex');
  return { id: hash(pub), public: pub, private: k.privateKey.export({ type: 'pkcs8', format: 'pem' }) };
}
function sign(signed, key) {
  return Buffer.from(JSON.stringify({ signed, signatures: [{ keyid: key.id,
    sig: crypto.sign(null, Buffer.from(canonical(signed)), key.private).toString('hex') }] }));
}
function protect(bytes, decode = false) {
  if (process.platform !== 'win32') throw Error('Custódia de chaves exige DPAPI CurrentUser no Windows.');
  const code = `Add-Type -AssemblyName System.Security; $b=[Convert]::FromBase64String([Console]::In.ReadToEnd()); $r=[System.Security.Cryptography.ProtectedData]::${decode ? 'Unprotect' : 'Protect'}($b,$null,[System.Security.Cryptography.DataProtectionScope]::CurrentUser); [Console]::Out.Write([Convert]::ToBase64String($r))`;
  return Buffer.from(execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', code],
    { input: bytes.toString('base64'), encoding: 'utf8', windowsHide: true }).trim(), 'base64');
}
function write(file, bytes) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file + '.tmp', bytes, { flag: 'wx' }); fs.renameSync(file + '.tmp', file);
}
function main() {
  const [operation, feedArg, keysArg, installerArg, version, tag] = process.argv.slice(2);
  if (!['init', 'publish'].includes(operation) || !feedArg || !keysArg) throw Error('Use init FEED PRIVATE ou publish FEED PRIVATE INSTALLER VERSION TAG.');
  const feed = path.resolve(feedArg), privateDir = path.resolve(keysArg), meta = path.join(feed, 'metadata');
  for (const publicDir of [repo, feed]) if (privateDir === publicDir || privateDir.startsWith(publicDir + path.sep)) throw Error('Chaves devem ficar fora dos repositórios públicos.');
  const keyFile = path.join(privateDir, 'keys.dpapi');
  if (operation === 'init') {
    if (fs.existsSync(keyFile) || fs.existsSync(path.join(meta, 'root.json'))) throw Error('Canal já inicializado; não substitua a raiz de confiança.');
    const keys = Object.fromEntries(roles.map(r => [r, keyPair()]));
    const root = { _type: 'root', spec_version: '1.0.31', version: 1, expires: expiry(365), consistent_snapshot: false, keys: {}, roles: {} };
    for (const role of roles) {
      const key = keys[role]; root.keys[key.id] = { keytype: 'ed25519', scheme: 'ed25519', keyval: { public: key.public } };
      root.roles[role] = { keyids: [key.id], threshold: 1 };
    }
    write(keyFile, protect(Buffer.from(JSON.stringify(keys))));
    const signedRoot = sign(root, keys.root);
    write(path.join(meta, '1.root.json'), signedRoot); write(path.join(meta, 'root.json'), signedRoot);
    write(path.join(feed, 'source.json'), JSON.stringify({ schema_version: 1, official_publishers: [],
      metadata_url: 'https://raw.githubusercontent.com/Warier/nodivu/qt-updates/metadata/',
      target_url: 'https://raw.githubusercontent.com/Warier/nodivu/qt-updates/targets/',
      release_repository: 'Warier/nodivu', root: signedRoot.toString() }, null, 2) + '\n');
    console.log('Raiz Qt independente criada. Chaves privadas criptografadas com DPAPI.'); return;
  }
  if (!/^\d+\.\d+\.\d+$/.test(version || '') || tag !== `v${version}-qt.1`) throw Error('Versão/tag Qt inválida.');
  const installer = path.resolve(installerArg), name = `Nodivu-${version}-windows-x64-qt-setup.exe`;
  if (path.basename(installer) !== name) throw Error('Nome do instalador incompatível.');
  const bytes = fs.readFileSync(installer);
  if (bytes.length > 256 * 1024 * 1024 || bytes.subarray(0, 2).toString() !== 'MZ') throw Error('Instalador inválido ou maior que 256 MiB.');
  const keys = JSON.parse(protect(fs.readFileSync(keyFile), true));
  const root = JSON.parse(fs.readFileSync(path.join(meta, 'root.json'))).signed;
  for (const role of roles) if (!root.roles[role].keyids.includes(keys[role].id)) throw Error('Chaves não correspondem à raiz pública.');
  const old = path.join(meta, 'timestamp.json'); const revision = fs.existsSync(old) ? JSON.parse(fs.readFileSync(old)).signed.version + 1 : 1;
  const offer = Buffer.from(JSON.stringify({ schema_version: 1, platform: 'win32-x64', channel: 'stable', version,
    installer: name, bytes: bytes.length, notes: 'Edição Qt independente: atalhos, player e correções do backend. Não substitui o Electron.' }));
  const targets = sign({ _type: 'targets', spec_version: '1.0.31', version: revision, expires: expiry(90), targets: {
    'windows-x64/stable.json': description(offer), [name]: { ...description(bytes), custom: {
      download_url: `https://github.com/Warier/nodivu/releases/download/${tag}/${name}` } } } }, keys.targets);
  const snapshot = sign({ _type: 'snapshot', spec_version: '1.0.31', version: revision, expires: expiry(30),
    meta: { 'targets.json': { version: revision, ...description(targets) } } }, keys.snapshot);
  const timestamp = sign({ _type: 'timestamp', spec_version: '1.0.31', version: revision, expires: expiry(14),
    meta: { 'snapshot.json': { version: revision, ...description(snapshot) } } }, keys.timestamp);
  write(path.join(feed, 'targets/windows-x64/stable.json'), offer);
  for (const [name, bytes] of [['targets.json', targets], ['snapshot.json', snapshot], ['timestamp.json', timestamp]]) write(path.join(meta, name), bytes);
  console.log(`Qt metadata revision ${revision}; renew before ${JSON.parse(timestamp).signed.expires}. Installer SHA256 ${hash(bytes)}`);
}
if (require.main === module) { try { main(); } catch (e) { console.error(e.message); process.exitCode = 1; } }
module.exports = { canonical, sign, description };

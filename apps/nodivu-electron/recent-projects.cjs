// Local metadata only; opening still goes through the Rust project validator.
// No project/PCM is cached and no path is opened automatically on startup.
const fs = require('node:fs/promises');
const path = require('node:path');
const {readProject, writeProject} = require('./project-files.cjs');
const LIMIT = 10;
const key = file => process.platform === 'win32' ? file.toLowerCase() : file;
function validPath(file) {
  return typeof file === 'string' && path.isAbsolute(file) && !file.includes('\0') && Buffer.byteLength(file) <= 4096;
}
class RecentProjects {
  constructor(file) { this.file = file; }
  async list() {
    let text;
    try { text = await readProject(this.file); }
    catch (error) { if (error.code === 'ENOENT') return []; throw error; }
    const data = JSON.parse(text);
    if (data?.version !== 1 || !Array.isArray(data.files) || data.files.length > LIMIT || !data.files.every(validPath))
      throw new Error('Lista de projetos recentes inválida. O arquivo original foi preservado.');
    return [...new Map(data.files.map(file => [key(path.normalize(file)), path.normalize(file)])).values()];
  }
  async remember(file) {
    if (!validPath(file)) throw new Error('Caminho de projeto recente inválido.');
    file = path.normalize(file);
    const files = [file, ...(await this.list()).filter(item => key(item) !== key(file))].slice(0, LIMIT);
    await fs.mkdir(path.dirname(this.file), {recursive:true});
    await writeProject(this.file, {version:1, files});
    return files;
  }
  async resolve(file) {
    // The renderer may choose only a path already recorded by the main process.
    if (!validPath(file)) throw new Error('Selecione um projeto da lista de recentes.');
    const known = (await this.list()).find(item => key(item) === key(path.normalize(file)));
    if (!known) throw new Error('Projeto não consta na lista de recentes. Use Abrir.');
    return known;
  }
}
module.exports = {RecentProjects};

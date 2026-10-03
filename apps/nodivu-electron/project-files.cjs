// Main-process file adapter. Graph validation remains in Rust; no PCM or plugin code here.
const fs = require('node:fs/promises');
const path = require('node:path');
const {randomUUID} = require('node:crypto');
const LIMIT = 64 * 1024;
async function readProject(file) {
  const handle = await fs.open(file, 'r');
  try {
    const bytes = Buffer.alloc(LIMIT + 1);
    let length = 0;
    while (length < bytes.length) {
      const result = await handle.read(bytes, length, bytes.length - length, null);
      if (!result.bytesRead) break;
      length += result.bytesRead;
    }
    if (length > LIMIT) throw new Error('Projeto excede 64 KiB.');
    return new TextDecoder('utf-8', {fatal:true}).decode(bytes.subarray(0, length));
  } finally { await handle.close(); }
}
async function writeProject(file, project) {
  const data = JSON.stringify(project, null, 2) + '\n';
  if (Buffer.byteLength(data) > LIMIT) throw new Error('Projeto excede 64 KiB.');
  const temporary = path.join(path.dirname(file), '.' + path.basename(file) + '.' + randomUUID() + '.tmp');
  try {
    const handle = await fs.open(temporary, 'wx');
    try { await handle.writeFile(data, 'utf8'); await handle.sync(); }
    finally { await handle.close(); }
    // Same-volume rename replaces the target via Node/libuv's Windows implementation.
    // Never delete the old file first; on failure it remains intact.
    await fs.rename(temporary, file);
  } finally { await fs.rm(temporary, {force:true}); }
}
function resourcesForFile(project, file, opening) {
  return {...project, resources:project.resources.map(resource => {
    const absolute = path.resolve(path.dirname(file), resource.path);
    const relative = path.relative(path.dirname(file), absolute);
    return {...resource, path: opening ? absolute : relative && !relative.startsWith('..') && !path.isAbsolute(relative) ? relative : absolute};
  })};
}
module.exports = {readProject, writeProject, resourcesForFile};

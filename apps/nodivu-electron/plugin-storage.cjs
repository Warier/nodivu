const fs = require('node:fs/promises');
const path = require('node:path');

// User plugins must survive an installer replacing/removing the application folder.
// Built-ins have reserved folders; other packages are copied only on first migration.
async function preparePlugins(bundled, destination) {
  await fs.mkdir(destination,{recursive:true});
  for (const entry of await fs.readdir(bundled,{withFileTypes:true})) {
    if (entry.isSymbolicLink() || (!entry.isDirectory() && !entry.isFile())) continue;
    const source=path.join(bundled,entry.name), target=path.join(destination,entry.name);
    const managed=entry.isDirectory() && ['mp3','windows-audio'].includes(entry.name);
    // Preserve edits to external packages in userData. Do not follow symlinks.
    if (!managed && await fs.stat(target).then(()=>true,()=>false)) continue;
    await fs.cp(source,target,{recursive:true,force:managed,dereference:false,
      filter:async file=>!(await fs.lstat(file)).isSymbolicLink()});
  }
  return path.join(destination,'plugin.json');
}
module.exports={preparePlugins};

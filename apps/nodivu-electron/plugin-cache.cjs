const fs=require('node:fs/promises'),path=require('node:path');
// Only disposable downloads and unreferenced staging directories. Trust metadata,
// installed modules, version backups, projects and local sources are never candidates.
async function clearDirectory(directory,accept){
 let count=0;
 const stat=await fs.lstat(directory).catch(e=>{if(e.code==='ENOENT')return null;throw e;});
 if(!stat)return count;
 if(!stat.isDirectory()||stat.isSymbolicLink())throw new Error('Pasta de cache inválida.');
 const root=await fs.realpath(directory);
 for(const entry of await fs.readdir(root,{withFileTypes:true})){
  if(entry.isSymbolicLink()||!accept(entry.name))continue;
  const file=path.resolve(root,entry.name);
  if(path.dirname(file)!==root)throw new Error('Caminho de cache inválido.');
  await fs.rm(file,{recursive:true,force:true});++count;
 }
 return count;
}
async function clearPluginCache(catalog,manager){
 if(catalog.refreshing)await catalog.refreshing;
 catalog.client=null;
 let removed=0;
 if(catalog.source){
  removed+=await clearDirectory(catalog.targets,()=>true);
  removed+=await clearDirectory(catalog.directory,n=>/^unpack-[a-f0-9-]{36}$/.test(n)||/^readme-[a-f0-9]{64}\.md$/.test(n));
 }
 const journals=await manager.versions.journals();
 if(manager.versions.errors.length)throw new Error('Downloads limpos; staging preservado porque há registros de atualização inválidos.');
 const referenced=new Set(journals.map(j=>j.stage));
 removed+=await clearDirectory(manager.versions.staging,n=>/^local-[a-f0-9-]{36}$/.test(n)&&!referenced.has(n));
 return removed;
}
module.exports={clearPluginCache};

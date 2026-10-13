const fs=require('node:fs/promises');
const path=require('node:path');
// Resolve the installed, lockfile-pinned production dependency tree, including nested versions.
// Use the same inventory for runtime assembly and license collection.
module.exports=async function runtimePackages(app){
 const modules=path.join(app,'node_modules'),found=new Map();
 async function visit(name,from){
  let file;
  try{file=require.resolve(name+'/package.json',{paths:[from]});}
  catch{file=require.resolve(name,{paths:[from]});}
  let dir=path.dirname(file),info;
  for(;;){
   try{const data=JSON.parse(await fs.readFile(path.join(dir,'package.json'),'utf8'));if(data.name===name){info=data;break;}}catch(e){if(e.code!=='ENOENT')throw e;}
   const parent=path.dirname(dir);if(parent===dir)throw new Error('Package metadata missing: '+name);dir=parent;
  }
  if(found.has(dir))return;
  const relative=path.relative(modules,dir);if(relative.startsWith('..')||path.isAbsolute(relative))throw new Error('Dependency outside node_modules: '+name);
  found.set(dir,{name,version:info.version,license:info.license,dir,relative});
  for(const dep of Object.keys(info.dependencies||{}))await visit(dep,dir);
 }
 const pkg=JSON.parse(await fs.readFile(path.join(app,'package.json'),'utf8'));
 for(const name of Object.keys(pkg.dependencies||{}))await visit(name,app);
 return [...found.values()];
};

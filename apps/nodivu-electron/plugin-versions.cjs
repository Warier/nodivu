const fs=require('node:fs/promises');
const path=require('node:path');
const {randomUUID}=require('node:crypto');
const exists=file=>fs.lstat(file).then(()=>true,e=>{if(e.code==='ENOENT')return false;throw e;});
const keyValid=key=>typeof key==='string'&&/^(?:local-)?[a-zA-Z0-9_-]{1,150}$/.test(key);
const versionValid=value=>typeof value==='string'&&/^(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})$/.test(value);
function compareVersion(a,b){for(let i=0;i<3;i++){const d=Number(a.split('.')[i])-Number(b.split('.')[i]);if(d)return Math.sign(d);}return 0;}
async function metadata(folder,id){
 const file=path.join(folder,'nodivu-package.json');
 if(!await exists(file))return null;
 const stat=await fs.lstat(file);if(stat.isSymbolicLink()||stat.size>16384)throw new Error('Metadados de pacote inválidos.');
 const m=JSON.parse(await fs.readFile(file,'utf8'));
 if(!m||m.schema_version!==1||m.plugin_id!==id||!versionValid(m.version)||typeof m.author!=='string'||!m.author.trim()||m.author.length>128)throw new Error('nodivu-package.json exige schema 1, mesmo plugin_id, versão x.y.z e autor.');
 if(m.host_api!==undefined&&![1,2].includes(m.host_api))throw new Error('Este pacote requer outro contrato do Nodivu.');
 if(m.platform!==undefined&&m.platform!==`${process.platform}-${process.arch}`)throw new Error('Pacote incompatível com este sistema/arquitetura.');
 if(m.repository!==undefined){const url=new URL(m.repository);if(url.protocol!=='https:'||url.username||url.password)throw new Error('Repositório exige HTTPS.');}
 if(m.resource_tier!==undefined&&!['light','moderate','heavy','unknown'].includes(m.resource_tier))throw new Error('Categoria de consumo inválida.');
 return {version:m.version,author:m.author,repository:m.repository||'',resourceTier:m.resource_tier||'unknown',license:typeof m.license==='string'?m.license.slice(0,128):''};
}

class PluginVersions {
 constructor(root,archive){this.root=root;this.archive=archive;this.errors=[];this.updates=path.join(path.dirname(root),'plugin-updates');this.staging=path.join(path.dirname(root),'plugin-staging');}
 async journals(){
  await fs.mkdir(this.updates,{recursive:true});
  const result=[];this.errors=[];
  for(const name of await fs.readdir(this.updates)){
   if(!/^[a-f0-9-]{36}\.json$/.test(name))continue;
   try{
   const file=path.join(this.updates,name),stat=await fs.lstat(file);
   if(stat.isSymbolicLink()||stat.size>16384)throw new Error('Registro de atualização inválido.');
   const data=JSON.parse(await fs.readFile(file,'utf8'));
   if(data.schema!==1||!keyValid(data.key)||!keyValid(data.stage)||!keyValid(data.backup)||typeof data.pluginId!=='string'||!versionValid(data.version))throw new Error('Registro de atualização inválido.');
   result.push({...data,file});
   }catch(error){this.errors.push('Registro de atualização '+name+': '+error.message);}
  }
  return result;
 }
 async schedule(key,stage,pluginId,version){
  if(!keyValid(key)||!keyValid(stage))throw new Error('Este pacote legado não admite atualização pelo gerenciador.');
  if((await this.journals()).some(j=>j.key===key))throw new Error('Já há uma atualização pendente para este pacote.');
  if(this.errors.length)throw new Error(this.errors.join(' '));
  const file=path.join(this.updates,randomUUID()+'.json');
  const data={schema:1,key,stage,pluginId,version,backup:key+'-'+randomUUID()};
  await fs.writeFile(file+'.tmp',JSON.stringify(data),{flag:'wx'});await fs.rename(file+'.tmp',file);
 }
 async apply(inspect){
  const journals=await this.journals(),warnings=[...this.errors];
  if(warnings.length)return warnings;
  for(const j of journals){
   const current=path.join(this.root,j.key),staged=path.join(this.staging,j.stage),backup=path.join(this.archive,j.backup);
   try{
    if(await exists(staged)){
     const next=await inspect(staged);if(next.data.plugin_id!==j.pluginId||next.metadata?.version!==j.version)throw new Error('ID ou versão da atualização mudou.');
     await fs.mkdir(this.archive,{recursive:true});
     if(!await exists(backup)){
      // Preserve activation choice; installer metadata can never silently enable a disabled plugin.
      for(const name of ['.nodivu-disabled','.nodivu-managed'])if(await exists(path.join(current,name)))await fs.writeFile(path.join(staged,name),'preservado\n');
      await fs.rename(current,backup);
     }
     if(await exists(current))throw new Error('Destino de atualização ocupado.');
     await fs.rename(staged,current);
    }else if(!await exists(current)||!await exists(backup))throw new Error('Arquivos da atualização incompletos.');
    await fs.unlink(j.file);
   }catch(error){
    if(!await exists(current)&&await exists(backup))await fs.rename(backup,current).catch(e=>warnings.push('Falha ao restaurar pacote: '+e.message));
    warnings.push('Atualização pendente de '+j.pluginId+': '+error.message);
   }
  }
  return warnings;
 }
 async previous(pluginId){
  if(!await exists(this.archive))return [];
  const results=[];
  for(const entry of await fs.readdir(this.archive,{withFileTypes:true})){
   if(!entry.isDirectory()||!keyValid(entry.name))continue;
   try{const file=path.join(this.archive,entry.name,'plugin.json'),stat=await fs.lstat(file);if(!stat.isFile()||stat.isSymbolicLink()||stat.size>65536)continue;const manifest=JSON.parse(await fs.readFile(file,'utf8'));if(manifest.plugin_id===pluginId){const m=await metadata(path.join(this.archive,entry.name),pluginId);if(m)results.push({key:entry.name,...m});}}catch{/* Other archived packages are not candidates for this ID. */}
  }
  return results.sort((a,b)=>compareVersion(b.version,a.version));
 }
}
module.exports={PluginVersions,metadata,compareVersion};

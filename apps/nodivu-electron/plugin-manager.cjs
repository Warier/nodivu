const fs = require('node:fs/promises');
const path = require('node:path');
const {randomUUID} = require('node:crypto');
const {PluginVersions,metadata,compareVersion}=require('./plugin-versions.cjs');

const DISABLED = '.nodivu-disabled', REMOVED = '.nodivu-removed';
const exists = file => fs.lstat(file).then(()=>true, e=>{if(e.code==='ENOENT')return false;throw e;});

// Import never executes plugin code. Limits include all files, including optional assets.
async function inspectFolder(folder) {
  const root = await fs.realpath(folder);
  let bytes=0, files=0;
  async function walk(dir, depth) {
    if(depth>12)throw new Error('Pacote com subpastas demais.');
    for(const entry of await fs.readdir(dir,{withFileTypes:true})) {
      const file=path.join(dir,entry.name), stat=await fs.lstat(file);
      if(stat.isSymbolicLink() || (!stat.isDirectory()&&!stat.isFile()))throw new Error('Links e arquivos especiais não são permitidos no pacote.');
      if(++files>2048)throw new Error('Pacote excede 2048 arquivos/pastas.');
      if(stat.isDirectory())await walk(file,depth+1);
      else {bytes+=stat.size;if(bytes>256*1024*1024)throw new Error('Pacote excede 256 MiB nesta primeira versão.');}
    }
  }
  await walk(root,0);
  const manifest=path.join(root,'plugin.json');
  if((await fs.stat(manifest)).size>65536)throw new Error('Manifesto excede 64 KiB.');
  const data=JSON.parse(await fs.readFile(manifest,'utf8'));
  if(!data || typeof data!=='object' || Array.isArray(data) || data.schema_version!==1 || typeof data.plugin_id!=='string' || !data.plugin_id.length || data.plugin_id.length>128 || typeof data.title!=='string' || !data.title.trim() || data.title.length>128)
    throw new Error('Manifesto de plugin inválido.');
  if(typeof data.library!=='string' || !data.library || path.isAbsolute(data.library) || data.library.split(/[\\/]/).some(p=>p==='..'||p.includes(':')) || path.extname(data.library)!=='.clap')
    throw new Error('A biblioteca .clap precisa estar dentro da pasta do pacote.');
  const library=await fs.realpath(path.join(root,data.library));
  if(!library.startsWith(root+path.sep) || !(await fs.stat(library)).isFile())throw new Error('Biblioteca fora do pacote.');
  if(await exists(path.join(root,'worker.json')))throw new Error('Importe somente um plugin CLAP por pasta nesta versão.');
  return {data,bytes,metadata:await metadata(root,data.plugin_id)};
}

class PluginManager {
  constructor(root, archive, reportFile) {this.root=path.resolve(root);this.archive=path.resolve(archive);this.reportFile=reportFile;this.initial=new Map();this.warnings=[];this.versions=new PluginVersions(this.root,this.archive);}
  folder(key) {
    if(key==='.')return this.root;
    if(typeof key!=='string' || !/^[a-zA-Z0-9_.-]{1,160}$/.test(key) || key==='..')throw new Error('Identificador de pacote inválido.');
    return path.join(this.root,key);
  }
  async checkedFolder(key) {
    const folder=this.folder(key),stat=await fs.lstat(folder);
    if(!stat.isDirectory()||stat.isSymbolicLink())throw new Error('Pasta de pacote inválida.');
    return folder;
  }
  async init() {
    await fs.mkdir(this.root,{recursive:true});
    this.warnings=await this.versions.apply(inspectFolder);
    // Deferred removal runs before starting the backend, never while its DLL is loaded.
    for(const entry of await this.entries())if(entry.removed&&!entry.protected) {
      try {
        await fs.mkdir(this.archive,{recursive:true});
        await fs.rename(this.folder(entry.key),path.join(this.archive,entry.key+'-'+randomUUID()));
      }catch(error){
        // A locked file must not block the entire app. The removal marker keeps it inactive.
        this.warnings.push('Remoção de '+entry.title+' aguarda liberação dos arquivos: '+error.message);
      }
    }
    this.initial=new Map((await this.entries()).map(e=>[e.key,e.enabled]));
  }
  async entries() {
    const keys=['.',...(await fs.readdir(this.root,{withFileTypes:true})).filter(e=>e.isDirectory()&&!e.isSymbolicLink()&&/^[a-zA-Z0-9_.-]{1,160}$/.test(e.name)).map(e=>e.name)];
    const rows=[];
    for(const key of keys) {
      const folder=await this.checkedFolder(key);
      const native=await exists(path.join(folder,'plugin.json'));
      const worker=await exists(path.join(folder,'worker.json'));
      if(!native&&!worker)continue;
      let data={},error='',meta=null;
      try {
        const file=path.join(folder,native?'plugin.json':'worker.json'),stat=await fs.lstat(file);
        if(stat.isSymbolicLink()||!stat.isFile()||stat.size>65536)throw new Error('Manifesto inválido ou maior que 64 KiB.');
        data=JSON.parse(await fs.readFile(file,'utf8'));
        if(!data || typeof data!=='object' || Array.isArray(data) || typeof data.plugin_id!=='string'||!data.plugin_id)throw new Error('ID ausente no manifesto.');
        meta=await metadata(folder,data.plugin_id);
      }catch(e){data={};error=e.message;}
      const removed=await exists(path.join(folder,REMOVED));
      const enabled=!removed&&!await exists(path.join(folder,DISABLED));
      rows.push({key,id:typeof data.plugin_id==='string'?data.plugin_id:'',title:typeof data.title==='string'?data.title:key,
        kind:native?'CLAP nativo':'Worker',error,enabled,removed,metadata:meta,protected:['.','mp3','windows-audio','voice-cleanup','peak-limiter'].includes(key),
        pending:this.initial.has(key)?this.initial.get(key)!==enabled||removed:true});
    }
    return rows.sort((a,b)=>a.title.localeCompare(b.title));
  }
  async list(){
    const entries=await this.entries();
    const updates=await this.versions.journals();
    const warnings=[...this.warnings,...this.versions.errors];
    for(const entry of entries){
      entry.update=updates.find(j=>j.key===entry.key)?.version||null;
      try{entry.previous=await this.versions.previous(entry.id);}catch(e){entry.previous=[];warnings.push('Não foi possível consultar versões anteriores: '+e.message);}
      if(entry.update)entry.pending=true;
    }
    if(this.reportFile){
      try{
        if((await fs.stat(this.reportFile)).size>256*1024)throw new Error('Diagnóstico de plugins excedeu o limite.');
        const issues=JSON.parse(await fs.readFile(this.reportFile,'utf8'));
        if(!Array.isArray(issues))throw new Error('Diagnóstico de plugins inválido.');
        for(const entry of entries){
          const issue=issues.find(i=>typeof i.manifest==='string'&&path.dirname(path.resolve(i.manifest))===this.folder(entry.key));
          if(issue&&typeof issue.message==='string')entry.loadError=issue.message;
        }
      }catch(e){if(e.code!=='ENOENT')warnings.push(e.message);}
    }
    return {entries,limit:8,warnings};
  }
  async setEnabled(key,enabled) {
    if(typeof enabled!=='boolean')throw new Error('Estado inválido.');
    const rows=await this.entries(),entry=rows.find(e=>e.key===key);
    if(!entry)throw new Error('Pacote não encontrado.');
    if(entry.removed)throw new Error('Cancele a remoção antes de ativar.');
    if(enabled&&entry.error)throw new Error('Corrija o manifesto antes de ativar.');
    if(enabled&&!entry.enabled&&rows.filter(e=>e.enabled).length>=8)throw new Error('Até oito tipos ativos. Desative outro plugin primeiro.');
    const file=path.join(await this.checkedFolder(key),DISABLED);
    if(enabled)await fs.unlink(file).catch(e=>{if(e.code!=='ENOENT')throw e;});
    else await fs.writeFile(file,'Desativado pelo gerenciador Nodivu.\n',{flag:'wx'}).catch(e=>{if(e.code!=='EEXIST')throw e;});
    return this.list();
  }
  async remove(key) {
    const entry=(await this.entries()).find(e=>e.key===key);
    if(!entry||entry.protected)throw new Error('Este pacote acompanha o app. Você pode desativá-lo.');
    if((await this.versions.journals()).some(j=>j.key===key))throw new Error('Cancele a atualização pendente antes de remover.');
    await fs.writeFile(path.join(await this.checkedFolder(key),REMOVED),'Remoção pendente.\n',{flag:'wx'}).catch(e=>{if(e.code!=='EEXIST')throw e;});
    return this.list();
  }
  async cancelRemoval(key) {
    const entry=(await this.entries()).find(e=>e.key===key);
    if(!entry)throw new Error('Pacote não encontrado.');
    if(!await exists(path.join(this.folder(key),DISABLED))&&(await this.entries()).filter(e=>e.enabled).length>=8)
      throw new Error('Até oito tipos ativos. Desative outro plugin primeiro.');
    await fs.unlink(path.join(await this.checkedFolder(key),REMOVED)).catch(e=>{if(e.code!=='ENOENT')throw e;});
    return this.list();
  }
  async install(source,rollback=false) {
    const info=await inspectFolder(source),rows=await this.entries();
    const existing=rows.find(e=>e.id===info.data.plugin_id);
    if(existing){
      if(!existing.metadata||!info.metadata)throw new Error('ID já está instalado. Para atualizar, ambos os pacotes precisam de nodivu-package.json com versão.');
      if(existing.removed)throw new Error('Cancele a remoção antes de atualizar.');
      if(!rollback&&compareVersion(info.metadata.version,existing.metadata.version)<=0)throw new Error('A atualização precisa ter versão mais recente.');
    }
    const key='local-'+randomUUID();
    const stage=path.join(path.dirname(this.root),'plugin-staging',key);
    await fs.mkdir(stage,{recursive:true});
    try {
      await fs.cp(source,stage,{recursive:true,dereference:false});
      // Revalidate the copied tree before publication, including links changed during copy.
      const copied=await inspectFolder(stage);
      if(copied.data.plugin_id!==info.data.plugin_id)throw new Error('O pacote mudou durante a cópia. Tente novamente.');
      for(const marker of [DISABLED,REMOVED,'.nodivu-managed'])await fs.unlink(path.join(stage,marker)).catch(e=>{if(e.code!=='ENOENT')throw e;});
      if(existing){
        await fs.writeFile(path.join(stage,'.nodivu-managed'),'Gerenciado por versões.\n');
        await this.versions.schedule(existing.key,key,info.data.plugin_id,info.metadata.version);
        return this.list();
      }
      if(rows.filter(e=>e.enabled).length>=8)await fs.writeFile(path.join(stage,DISABLED),'Limite de tipos ativos atingido.\n');
      await fs.rename(stage,this.folder(key));
    }catch(e){await fs.rm(stage,{recursive:true,force:true});throw e;}
    return this.list();
  }
  async rollback(key,previousKey){
    const entry=(await this.entries()).find(e=>e.key===key);if(!entry)throw new Error('Pacote ausente.');
    const previous=(await this.versions.previous(entry.id)).find(p=>p.key===previousKey);if(!previous)throw new Error('Versão anterior ausente.');
    return this.install(path.join(this.archive,previous.key),true);
  }
  async cancelUpdate(key){
    const journal=(await this.versions.journals()).find(j=>j.key===key);if(!journal)throw new Error('Atualização ausente.');
    if(await exists(path.join(this.archive,journal.backup)))throw new Error('Atualização parcialmente aplicada. Reabra o app para recuperar.');
    // Preserve the downloaded folder as an inert cache; never remove a loaded module.
    await fs.unlink(journal.file);return this.list();
  }
  async details(key){
    const entry=(await this.entries()).find(e=>e.key===key);if(!entry)throw new Error('Pacote ausente.');
    const folder=await this.checkedFolder(key);let markdown='Este pacote não inclui README.md.';
    for(const name of ['README.md','readme.md']){
      const file=path.join(folder,name);if(!await exists(file))continue;
      const stat=await fs.lstat(file);if(stat.isSymbolicLink()||!stat.isFile()||stat.size>256*1024)throw new Error('README inválido ou maior que 256 KiB.');
      markdown=await fs.readFile(file,'utf8');break;
    }
    return {entry,markdown};
  }
  async image(key,relative){
    if(typeof relative!=='string'||relative.length>240||/[\\:#?]/.test(relative)||relative.startsWith('/')||relative.split('/').some(p=>p==='..'))throw new Error('Imagem deve estar dentro do pacote.');
    const folder=await this.checkedFolder(key),file=await fs.realpath(path.join(folder,relative));
    if(!file.startsWith(folder+path.sep))throw new Error('Imagem fora do pacote.');
    const mime={'.png':'image/png','.jpg':'image/jpeg','.jpeg':'image/jpeg','.webp':'image/webp','.gif':'image/gif'}[path.extname(file).toLowerCase()];
    if(!mime||(await fs.stat(file)).size>2*1024*1024)throw new Error('Imagem inválida ou maior que 2 MiB.');
    return {url:'data:'+mime+';base64,'+(await fs.readFile(file)).toString('base64')};
  }
}
module.exports={PluginManager,inspectFolder};

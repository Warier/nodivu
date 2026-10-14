const fs=require('node:fs/promises');
const path=require('node:path');
const {createHash,randomUUID}=require('node:crypto');
const {Updater,BaseFetcher}=require('tuf-js');
const {DownloadHTTPError}=require('tuf-js/dist/error');
const {extractArchive}=require('./plugin-archive.cjs');
const {inspectFolder}=require('./plugin-manager.cjs');
const MAX_PACKAGE=256*1024*1024,MAX_CATALOG=1024*1024;
const exists=file=>fs.access(file).then(()=>true,e=>{if(e.code==='ENOENT')return false;throw e;});
const text=(v,max)=>typeof v==='string'&&v.trim().length>0&&v.length<=max;
const targetPath=v=>text(v,240)&&v.split('/').every(p=>/^[a-zA-Z0-9][a-zA-Z0-9_.-]*$/.test(p)&&!p.endsWith('.'));
const version=v=>typeof v==='string'&&/^(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})$/.test(v);

function validateCatalog(data,officialPublishers=[]){
 if(!data||data.schema_version!==1||!Array.isArray(data.plugins)||data.plugins.length>500)throw new Error('Catálogo inválido ou maior que 500 plugins.');
 const ids=new Set();
 return data.plugins.map(p=>{
  if(!p||!text(p.id,128)||!/^[a-zA-Z0-9][a-zA-Z0-9._-]*$/.test(p.id)||ids.has(p.id)||!version(p.version)||!text(p.title,128)||!text(p.author,128)||!text(p.publisher_id,128)||!text(p.description,1024)||!targetPath(p.archive)||!targetPath(p.readme)||!Number.isSafeInteger(p.download_bytes)||p.download_bytes<=0||p.download_bytes>MAX_PACKAGE)throw new Error('Item inválido ou ID duplicado no catálogo.');
  if(!['light','moderate','heavy','unknown'].includes(p.resource_tier)||!text(p.platform,32)||!Number.isSafeInteger(p.host_api))throw new Error('Requisitos inválidos no catálogo.');
  if(p.logo!==undefined&&(!targetPath(p.logo)||!['.png','.jpg','.jpeg','.webp'].includes(path.extname(p.logo).toLowerCase())))throw new Error('Logo inválido.');
  const images=p.images||{};
  if(typeof images!=='object'||Array.isArray(images)||Object.keys(images).length>12||Object.entries(images).some(([relative,target])=>!targetPath(relative)||!targetPath(target)||!['.png','.jpg','.jpeg','.webp','.gif'].includes(path.extname(target).toLowerCase())))throw new Error('Imagens inválidas no catálogo.');
  if(p.repository!==undefined){const url=new URL(p.repository);if(url.protocol!=='https:'||url.username||url.password)throw new Error('Repositório inválido.');}
  ids.add(p.id);
  // Authority comes from a trusted catalog and a list shipped with the host, never package author text.
  return {id:p.id,version:p.version,title:p.title,author:p.author,publisherId:p.publisher_id,description:p.description,
   archive:p.archive,readme:p.readme,images,logo:p.logo||null,downloadBytes:p.download_bytes,resourceTier:p.resource_tier,
   platform:p.platform,hostApi:p.host_api,repository:p.repository||'',official:officialPublishers.includes(p.publisher_id),
   compatible:p.platform===`${process.platform}-${process.arch}`&&[1,2].includes(p.host_api)};
 });
}

// Explicit source configuration is shipped by the maintainer, not accepted from renderer input.
// HTTP exists only for an isolated loopback test; normal sources require HTTPS without redirects.
class CatalogFetcher extends BaseFetcher{
 constructor(source,operation=()=>null){super();this.source=source;this.operation=operation;}
 async fetch(value){
  const url=new URL(value),bases=[this.source.metadata_url,this.source.target_url].map(v=>new URL(v));
  if(!bases.some(b=>url.origin===b.origin&&url.pathname.startsWith(b.pathname))||url.username||url.password)throw new Error('Download fora da origem do catálogo.');
  const operation=this.operation();const signal=operation?AbortSignal.any([operation.controller.signal,AbortSignal.timeout(60000)]):AbortSignal.timeout(60000);
  if(operation)Object.assign(operation,{phase:'download',file:path.basename(url.pathname),received:0,total:0});
  const response=await fetch(url,{redirect:'manual',signal,headers:{'User-Agent':'Nodivu-plugin-store'}});
  if(!response.ok||!response.body){await response.body?.cancel();throw new DownloadHTTPError('Falha HTTP no catálogo: '+response.status,response.status);}
  if(!operation)return response.body;
  const size=Number(response.headers.get('content-length'));operation.total=Number.isSafeInteger(size)&&size>0?size:0;
  return response.body.pipeThrough(new TransformStream({transform(chunk,controller){operation.received+=chunk.byteLength;controller.enqueue(chunk);}}));
 }
 async downloadFile(url,length,handler){
  // Signed lengths are still untrusted resource requests. Cap metadata as well as package bytes.
  const cap=new URL(url).pathname.startsWith(new URL(this.source.metadata_url).pathname)?2*1024*1024:MAX_PACKAGE;
  if(!Number.isSafeInteger(length)||length<0||length>cap)throw new Error('Download excede o limite do catálogo.');
  return super.downloadFile(url,length,handler);
 }
}

class PluginCatalog{
 constructor(profile,source=null,{allowLoopback=false}={}){
  this.source=source;this.client=null;this.rows=[];this.error='';this.checkedAt=null;this.allowLoopback=allowLoopback;
  if(!source){this.directory=path.join(profile,'plugin-catalog-unconfigured');return;}
  if(source.schema_version!==1||!text(source.root,512000)||!Array.isArray(source.official_publishers)||source.official_publishers.some(v=>!text(v,128)))throw new Error('Configuração da loja inválida.');
  for(const name of ['metadata_url','target_url']){
   const url=new URL(source[name]);
   if((url.protocol!=='https:'&&!(allowLoopback&&url.protocol==='http:'&&url.hostname==='127.0.0.1'))||url.username||url.password||url.search||url.hash||!url.pathname.endsWith('/'))throw new Error('Origem da loja exige HTTPS e caminho terminado em /.');
  }
  // A different bootstrap/source gets an independent trust/cache directory. Never merge trust domains.
  const identity=createHash('sha256').update(JSON.stringify(source)).digest('hex');
  this.directory=path.join(profile,'plugin-catalog',identity);this.metadataDir=path.join(this.directory,'metadata');this.targets=path.join(this.directory,'targets');
 }
 async init(){
  if(!this.source)return;
  await fs.mkdir(this.metadataDir,{recursive:true});await fs.mkdir(this.targets,{recursive:true});
  const root=path.join(this.metadataDir,'root.json');
  // Initial trust is shipped out of band. Rotated roots thereafter are verified/persisted by TUF.
  if(!await exists(root))await fs.writeFile(root,this.source.root,{flag:'wx'});
  try{const cached=path.join(this.directory,'display.json');if((await fs.stat(cached)).size>MAX_CATALOG)throw new Error('Cache excede limite.');const data=JSON.parse(await fs.readFile(cached,'utf8'));this.rows=validateCatalog(data,this.source.official_publishers);}
  catch(e){if(e.code!=='ENOENT')this.error='Cache do catálogo indisponível: '+e.message;}
 }
 snapshot(){return {configured:Boolean(this.source),entries:this.rows,online:Boolean(this.client),checkedAt:this.checkedAt,error:this.error};}
 async refresh(){
  if(this.refreshing)return this.refreshing;
  this.refreshing=this.refreshOnce();try{return await this.refreshing;}finally{this.refreshing=null;}
 }
 async refreshOnce(){
  this.lastAttempt=Date.now();this.client=null;if(!this.source)return this.snapshot();
  try{
   const client=new Updater({metadataDir:this.metadataDir,metadataBaseUrl:this.source.metadata_url,targetDir:this.targets,targetBaseUrl:this.source.target_url,fetcher:new CatalogFetcher(this.source,()=>this.operation),config:{maxRootRotations:16,maxDelegations:8,rootMaxLength:512000,timestampMaxLength:16384,snapshotMaxLength:1048576,targetsMaxLength:2097152}});
   await client.refresh();
   const file=await this.target(client,'catalog.json',MAX_CATALOG);
   const data=JSON.parse(await fs.readFile(file,'utf8')),rows=validateCatalog(data,this.source.official_publishers);
   await fs.writeFile(path.join(this.directory,'display.json.tmp'),JSON.stringify(data));await fs.rename(path.join(this.directory,'display.json.tmp'),path.join(this.directory,'display.json'));
   this.rows=rows;this.client=client;this.checkedAt=new Date().toISOString();this.error='';
  }catch(e){this.error='Não foi possível verificar o catálogo: '+e.message;}
  return this.snapshot();
 }
 entry(id){const item=this.rows.find(p=>p.id===id);if(!item)throw new Error('Plugin não encontrado no catálogo.');return item;}
 async target(client,name,max,expected){
  if(!targetPath(name))throw new Error('Caminho inválido no catálogo.');
  const info=await client.getTargetInfo(name);
  if(!info||info.length>max||expected!==undefined&&info.length!==expected)throw new Error('Arquivo ausente ou tamanho incompatível nos metadados assinados.');
  return await client.findCachedTarget(info)||await client.downloadTarget(info);
 }
 async details(id){
  const item=this.entry(id);let markdown;
  const cache=path.join(this.directory,'readme-'+createHash('sha256').update(item.id+'@'+item.version).digest('hex')+'.md');
  if(this.client){const file=await this.target(this.client,item.readme,256*1024);markdown=await fs.readFile(file,'utf8');await fs.writeFile(cache,markdown);}
  else {if(!await exists(cache))throw new Error('Documentação ainda não disponível offline. Atualize o catálogo com conexão.');if((await fs.stat(cache)).size>256*1024)throw new Error('README excede limite.');markdown=await fs.readFile(cache,'utf8');}
  return {entry:{title:item.title,metadata:{author:item.author,version:item.version,license:''}},markdown};
 }
 async image(id,relative){
  if(!this.client)return {};
  const item=this.entry(id),name=relative===null?item.logo:Object.hasOwn(item.images,relative)?item.images[relative]:null;
  if(!name)return {};
  const file=await this.target(this.client,name,2*1024*1024);
  const mime={'.png':'image/png','.jpg':'image/jpeg','.jpeg':'image/jpeg','.webp':'image/webp','.gif':'image/gif'}[path.extname(name).toLowerCase()];
  return {url:'data:'+mime+';base64,'+(await fs.readFile(file)).toString('base64')};
 }
 beginOperation(){if(this.operation)throw new Error('Outra instalação em andamento.');this.operation={controller:new AbortController(),phase:'verify',file:'',received:0,total:0,cancelable:true};}
 operationStatus(){const p=this.operation;return p?{active:true,phase:p.phase,file:p.file,received:p.received,total:p.total,cancelable:p.cancelable}:{active:false};}
 cancel(){if(this.operation?.cancelable)this.operation.controller.abort(new Error('Instalação cancelada.'));return this.operationStatus();}
 checkCanceled(){this.operation?.controller.signal.throwIfAborted();}
 finishOperation(){this.operation=null;}
 async prepare(id,expectedVersion){
  // Refresh at installation time: offline cache may be browsed, but never authorizes new activation.
  await this.refresh();this.checkCanceled();if(!this.client)throw new Error(this.error||'Catálogo não configurado.');
  const item=this.entry(id);if(item.version!==expectedVersion)throw new Error('A versão do catálogo mudou. Revise a lista e tente novamente.');
  if(!item.compatible)throw new Error('Plugin incompatível com este sistema ou contrato do Nodivu.');
  const archive=await this.target(this.client,item.archive,MAX_PACKAGE,item.downloadBytes);
  this.checkCanceled();if(this.operation)this.operation.phase='verify';
  const temporary=path.join(this.directory,'unpack-'+randomUUID());
  try{
   const folder=await extractArchive(archive,temporary),info=await inspectFolder(folder);this.checkCanceled();
   if(info.data.plugin_id!==item.id||info.metadata?.version!==item.version)throw new Error('ID/versão do pacote não corresponde ao catálogo.');
   return {item,folder,cleanup:()=>fs.rm(temporary,{recursive:true,force:true})};
  }catch(e){await fs.rm(temporary,{recursive:true,force:true});throw e;}
 }
}
module.exports={PluginCatalog,validateCatalog};

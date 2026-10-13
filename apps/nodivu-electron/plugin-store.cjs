const fs=require('node:fs/promises');
const path=require('node:path');
const {PluginCatalog}=require('./plugin-catalog.cjs');
const {compareVersion}=require('./plugin-versions.cjs');

async function createStore(app,{test=false}={}){
 let catalog;
 try{
  // An installed build never accepts an environment override outside an isolated smoke test.
  const config=(!app.isPackaged||test)&&process.env.NODIVU_STORE_CONFIG||path.join(__dirname,'store/source.json');
  let source=null;
  try{const stat=await fs.lstat(config);if(!stat.isFile()||stat.isSymbolicLink()||stat.size>1024*1024)throw new Error('Configuração da loja inválida.');source=JSON.parse(await fs.readFile(config,'utf8'));}
  catch(e){if(e.code!=='ENOENT')throw e;}
  catalog=new PluginCatalog(app.getPath('userData'),source,{allowLoopback:test});await catalog.init();
 }catch(e){catalog=new PluginCatalog(app.getPath('userData'));catalog.error='Configuração da loja indisponível: '+e.message;}
 return catalog;
}
async function handleStore(catalog,manager,action,payload,dialog,win){
 if(action==='store-list'||action==='store-refresh'){
  const state=action==='store-refresh'||Date.now()-(catalog.lastAttempt||0)>15*60*1000?await catalog.refresh():catalog.snapshot();
  const installed=(await manager.list()).entries;
  return {...state,entries:state.entries.map(p=>{
   const local=installed.find(i=>i.id===p.id),installedVersion=local?.metadata?.version;
   return {...p,installedVersion:installedVersion||null,installed:Boolean(local),updateAvailable:Boolean(installedVersion&&compareVersion(p.version,installedVersion)>0),pendingVersion:local?.update||null};
  })};
 }
 if(action==='store-details')return catalog.details(payload?.key);
 if(action==='store-image')return catalog.image(payload?.key,payload?.relative);
 if(action==='store-install'){
  catalog.beginOperation();let prepared;
  try{
   prepared=await catalog.prepare(payload?.key,payload?.version);if(catalog.operation)catalog.operation.phase='confirm';
   const p=prepared.item;
   const choice=await dialog.showMessageBox(win,{type:'question',message:'Instalar '+p.title+' '+p.version+'?',detail:'Autor: '+p.author+'. Origem e integridade verificadas pelo catálogo. O plugin executa código nativo com os acessos do Nodivu; isso não é uma auditoria do código. A mudança será aplicada na próxima abertura.',buttons:['Cancelar','Instalar'],defaultId:0,cancelId:0});
   catalog.checkCanceled();if(catalog.operation){catalog.operation.phase='install';catalog.operation.cancelable=false;}
   return choice.response===1?await manager.install(prepared.folder):await manager.list();
  }finally{try{if(prepared)await prepared.cleanup();}finally{catalog.finishOperation();}}
 }
 throw new Error('Ação da loja desconhecida.');
}
module.exports={createStore,handleStore};

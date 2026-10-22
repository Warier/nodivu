const fs=require('node:fs/promises'),path=require('node:path');
const {EventEmitter}=require('node:events');
const MAX_BINDINGS=64;
const keyNames=new Map(['Space','Tab','Enter','Escape','Backspace','Delete','Insert','Home','End','PageUp','PageDown','Up','Down','Left','Right','MediaPlayPause','MediaStop','MediaNextTrack','MediaPreviousTrack'].map(k=>[k.toLowerCase(),k]));
const modifiers=new Map([['ctrl','Control'],['control','Control'],['shift','Shift'],['alt','Alt']]);
function accelerator(value){
 if(typeof value!=='string'||value.length>80)throw Error('Atalho inválido.');
 const tokens=value.trim().split('+').map(s=>s.trim().toLowerCase()),mods=new Set();
 const keyToken=tokens.pop();
 for(const token of tokens){const mod=modifiers.get(token);if(!mod||mods.has(mod))throw Error('Use Ctrl, Alt ou Shift; combinações com Win são reservadas.');mods.add(mod);}
 const key=/^[a-z0-9]$/.test(keyToken)?keyToken.toUpperCase():/^f([1-9]|1[0-9]|2[0-4])$/.test(keyToken)?keyToken.toUpperCase():keyNames.get(keyToken);
 if(!key)throw Error('Tecla não suportada. Macros de mouse podem enviar F13–F24.');
 const result=[...['Control','Alt','Shift'].filter(m=>mods.has(m)),key].join('+');
 if(key==='F12'||(mods.has('Alt')&&['F4','Tab','Escape','Space'].includes(key))||(mods.has('Control')&&key==='Escape')||(mods.has('Control')&&mods.has('Alt')&&key==='Delete'))throw Error('Combinação reservada pelo Windows.');
 if(['Control+S','Control+O','Control+C','Control+V','Control+X','Control+Z','Control+A','Control+F','Control+P','Control+W','Control+N','Control+R','Control+Shift+Z','Control+Shift+S'].includes(result))throw Error('Combinação reservada para edição/menus. Use outra combinação.');
 if(!mods.has('Control')&&!mods.has('Alt')&&!/^F\d+$/.test(key)&&!key.startsWith('Media'))throw Error('Combine a tecla com Ctrl ou Alt, ou use uma tecla F1–F24.');
 // Ctrl+Alt is AltGr on many layouts. Prefer Ctrl+Shift+F8 / F13 for typing-heavy workflows.
 return result;
}
function validate(data){
 if(!data||data.version!==1||typeof data.enabled!=='boolean'||!Array.isArray(data.bindings)||data.bindings.length>MAX_BINDINGS)throw Error('Configuração inválida (máximo 64 atalhos).');
 const ids=new Set(),keys=new Set();
 return {version:1,enabled:data.enabled,bindings:data.bindings.map(b=>{
  if(!b||typeof b.id!=='string'||!/^[\w-]{1,64}$/.test(b.id)||ids.has(b.id)||!['global','local'].includes(b.scope)||typeof b.enabled!=='boolean'||typeof b.action!=='string'||b.action.length>256||!b.action||typeof b.label!=='string'||b.label.length>240)throw Error('Associação de atalho inválida.');
  ids.add(b.id);const key=accelerator(b.accelerator);
  if(b.enabled&&keys.has(key))throw Error('Combinação duplicada: '+key);if(b.enabled)keys.add(key);
  if(b.value!==undefined&&(typeof b.value!=='number'||!Number.isFinite(b.value)))throw Error('Valor do controle inválido.');
  return {id:b.id,scope:b.scope,enabled:b.enabled,action:b.action,label:b.label,accelerator:key,...(b.value===undefined?{}:{value:b.value})};
 })};
}
class Hotkeys extends EventEmitter {
 constructor(file,shortcuts,execute){super();this.file=file;this.shortcuts=shortcuts;this.execute=execute;this.config={version:1,enabled:true,bindings:[]};this.actions=new Map();this.registered=new Map();this.states=new Map();this.registrationStatus=new Map();this.suspended=false;this.pending=false;this.saving=false;this.last=new Map();this.lastResult=null;this.error='';this.corrupt=false;}
 async init(){try{const stat=await fs.stat(this.file);if(stat.size>65536)throw Error('Arquivo de atalhos excede 64 KiB.');this.config=validate(JSON.parse(await fs.readFile(this.file,'utf8')));}catch(e){if(e.code!=='ENOENT'){this.error='Atalhos não carregados; arquivo preservado: '+e.message;this.corrupt=true;}}this.reconcile();}
 setActions(actions){
  if(!Array.isArray(actions)||actions.length>4096||actions.some(a=>!a||typeof a.id!=='string'||a.id.length>256||typeof a.label!=='string'||a.label.length>240))throw Error('Lista de ações inválida.');
  const next=new Map(actions.map(a=>[a.id,{id:a.id,label:a.label,nodeId:a.nodeId||null,needsValue:!!a.needsValue,min:a.min,max:a.max,stepped:!!a.stepped}]));
  if(JSON.stringify([...next])===JSON.stringify([...this.actions]))return;
  this.actions=next;this.reconcile();
 }
 snapshot(){return {config:this.config,actions:[...this.actions.values()],states:Object.fromEntries(this.states),suspended:this.suspended,lastResult:this.lastResult,error:this.error};}
 notify(){this.emit('change',this.snapshot());}
 unregister(){for(const key of this.registered.keys())this.shortcuts.unregister(key);this.registered.clear();}
 reconcile(){
  this.unregister();this.states.clear();
  const keys=new Set(this.config.bindings.map(b=>b.accelerator)),ids=new Set(this.config.bindings.map(b=>b.id));
  for(const key of this.registrationStatus.keys())if(!keys.has(key))this.registrationStatus.delete(key);
  for(const id of this.last.keys())if(!ids.has(id))this.last.delete(id);
  for(const b of this.config.bindings){
   let state=!this.config.enabled||!b.enabled?'Desativado':!this.actions.has(b.action)?'Bloco/ação ausente neste projeto':this.suspended?((b.scope==='local'?'Somente no Nodivu':this.registrationStatus.get(b.accelerator)||'Registro será verificado ao fechar')+' · suspenso durante configuração'):null;
   if(!state&&b.scope==='global'){
    try{if(this.shortcuts.register(b.accelerator,()=>void this.fire(b.id))){this.registered.set(b.accelerator,b.id);state='Global ativo';}else state='Conflito: combinação ocupada ou recusada pelo sistema';}catch(e){state='Falha ao registrar: '+e.message;}
    this.registrationStatus.set(b.accelerator,state);
   }
   this.states.set(b.id,state||'Ativo somente no Nodivu');
  }
  this.notify();
 }
 suspend(value){this.suspended=!!value;this.reconcile();}
 async save(data){
  if(this.saving)throw Error('Salvamento em andamento.');
  const next=validate(data);
  for(const binding of next.bindings){const a=this.actions.get(binding.action);if(a?.needsValue&&(!Number.isFinite(binding.value)||(a.min!==undefined&&binding.value<a.min)||(a.max!==undefined&&binding.value>a.max)||(a.stepped&&!Number.isInteger(binding.value))))throw Error('Valor inválido para '+a.label);}
  this.saving=true;
  try{
   await fs.mkdir(path.dirname(this.file),{recursive:true});
   if(this.corrupt)await fs.copyFile(this.file,this.file+'.preserved-'+Date.now());
   const contents=JSON.stringify(next,null,2)+'\n';if(Buffer.byteLength(contents)>65536)throw Error('Configuração excede 64 KiB. Reduza os atalhos.');
   await fs.writeFile(this.file+'.tmp',contents);await fs.rename(this.file+'.tmp',this.file);
   this.config=next;this.corrupt=false;this.error='';this.reconcile();return this.snapshot();
  }finally{this.saving=false;}
 }
 async fire(id,{test=false}={}){
  const b=this.config.bindings.find(b=>b.id===id);
  const action=b&&this.actions.get(b.action);
  if(!b||!action){this.lastResult={ok:false,message:'Bloco/ação ausente neste projeto.'};this.notify();return false;}
  if(!test&&(!b.enabled||!this.config.enabled||this.suspended))return false;
  if(this.pending){this.lastResult={ok:false,message:'Outra ação está em andamento; atalho ignorado.'};this.notify();return false;}
  const now=Date.now();if(!test&&now-(this.last.get(id)||0)<250)return false;this.last.set(id,now);
  this.pending=true;
  try{await this.execute(b);this.lastResult={ok:true,message:action.label+' — executado',at:now};return true;}
  catch(e){this.lastResult={ok:false,message:action.label+' — '+e.message,at:now};return false;}
  finally{this.pending=false;this.notify();}
 }
 local(input){
  if(input.type!=='keyDown'||input.isAutoRepeat||input.meta)return false;
  let key;try{key=accelerator([input.control?'Control':null,input.alt?'Alt':null,input.shift?'Shift':null,input.key===' '?'Space':input.key.replace(/^Arrow/,'')].filter(Boolean).join('+'));}catch{return false;}
  const b=this.config.bindings.find(b=>b.scope==='local'&&b.accelerator===key&&b.enabled&&this.actions.has(b.action));
  if(!b||!this.config.enabled||this.suspended)return false;void this.fire(b.id);return true;
 }
 close(){this.suspended=true;this.unregister();}
}
module.exports={Hotkeys,accelerator,validate};

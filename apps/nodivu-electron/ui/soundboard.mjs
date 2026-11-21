import {playerPresentation,formatTime} from './playback-state.mjs';
export const resourceKey=r=>r.slot===undefined?r.node_id:`${r.node_id}:${r.slot}`;
export function bankResources(node,runtime,saved){
 const result=[];
 for(let slot=0;slot<10;slot++){
  const state=runtime?.bank?.[slot];
  const path=state?.assigned?state.file:saved.get(`${node.id}:${slot}`);
  if(path)result.push({node_id:node.id,slot,path});
 }
 return result;
}
export function bankPresentation({snapshot,node,slot,devices,online,busy,savedPath}){
 const parent=snapshot.plugin_states?.[node.id],runtime=parent?.bank?.[slot]||{};
 const adapted={...snapshot,plugin_states:{...snapshot.plugin_states,[node.id]:{...runtime,fault:parent?.fault}}};
 const state=playerPresentation({snapshot:adapted,node,devices,online,busy,savedPath:runtime.assigned?undefined:savedPath,error:runtime.error});
 if(runtime.status<0){state.reason='Falha ao carregar: use MP3, WAV ou M4A de até 60 segundos e 32 MiB.';state.canPlay=false;}
 return {...state,playing:runtime.status===3,position:runtime.position_ms||0,duration:runtime.duration_ms||0};
}
export function bankBindings(config,actions,nodeId,uuid){
 const selected=actions.filter(a=>a.nodeId===nodeId&&/\/bank\.[0-9]\.trigger$/.test(a.id));
 if(!selected.length)throw Error('Soundboard indisponível.');
 const ids=new Set(selected.map(a=>a.id));
 const bindings=config.bindings.filter(b=>!ids.has(b.action));
 for(const a of selected){const slot=Number(a.id.match(/bank\.(\d)\.trigger$/)[1]);bindings.push({id:uuid(),label:a.label,action:a.id,enabled:true,scope:'global',accelerator:`Control+Shift+${(slot+1)%10}`});}
 return {...config,bindings};
}
const el=(tag,text)=>{const e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e;};
export function mountBank(root,node,count,{compact,perform,command,preset,choose}){
 const box=el('div');box.className='soundboard '+(compact?'compact':'settings');
 for(let slot=0;slot<count;slot++){
  const row=el('div');row.className='soundboard-slot';row.dataset.bankSlot=slot;
  const play=el('button',String((slot+1)%10));play.dataset.bankAction='trigger';play.setAttribute('aria-label',`Tocar som ${slot+1}`);play.onclick=()=>void perform(()=>command(node.id,slot,'trigger'));
  row.append(play);
  if(!compact){
   const file=el('button','Escolher áudio…');file.dataset.bankAction='file';file.onclick=()=>void perform(async()=>{const path=await choose();if(path)await command(node.id,slot,'load',path);});
   const stop=el('button','Parar');stop.dataset.bankAction='stop';stop.onclick=()=>void perform(()=>command(node.id,slot,'stop'));
   const clear=el('button','×');clear.dataset.bankAction='clear';clear.title='Remover arquivo desta posição';clear.onclick=()=>void perform(()=>command(node.id,slot,'clear'));
   const status=el('small');status.dataset.bankStatus='';row.append(file,stop,clear,status);
  }
  box.append(row);
 }
 if(!compact){const button=el('button','Atalhos 1–0 (Ctrl+Shift)');button.onclick=()=>void perform(()=>preset(node.id));box.append(button);}
 root.append(box);
}
export function syncBank(root,context){
 for(const row of root.querySelectorAll('[data-bank-slot]')){
  const slot=Number(row.dataset.bankSlot),state=bankPresentation({...context,slot,savedPath:context.resources.get(`${context.node.id}:${slot}`)});
  const play=row.querySelector('[data-bank-action=trigger]');play.disabled=!state.canPlay;play.title=state.reason||state.name+' · tocar/reiniciar';play.classList.toggle('playing',state.playing);
  const file=row.querySelector('[data-bank-action=file]');if(!file)continue;
  file.textContent=state.file?state.name:'Escolher áudio…';file.title=state.file;file.disabled=!context.online||context.busy;
  for(const action of ['clear','stop'])row.querySelector(`[data-bank-action=${action}]`).disabled=!context.online||context.busy||!state.file;
  row.querySelector('[data-bank-status]').textContent=state.reason||`${state.fileState} · ${formatTime(state.position)} / ${formatTime(state.duration)}`;
 }
}

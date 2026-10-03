// Presentation only. Identity validation/acquisition stay in the backend/plugin.
const views=new WeakMap();
export function captureMessage(state,selection){
 if(!selection)return 'Selecione um aplicativo ou o som do PC.';
 if(state?.control_error)return state.control_error;
 const code=state?.error?` (0x${(state.error>>>0).toString(16).padStart(8,'0')})`:'';
 switch(state?.status){
  case 2:return 'Capturando · conecte a saída deste bloco ao Mixer ou à Saída.';
  case 3:return 'Aplicativo ausente ou encerrado. Inicie o áudio, atualize e selecione novamente.';
  case 4:return 'Há várias instâncias desse aplicativo. Atualize e escolha uma.';
  case 5:return 'Não foi possível capturar. Atualize a lista e selecione novamente.'+code;
  default:return 'Preparando captura…';
 }
}
export function mountCapture(content,id,{api,perform,configure,schedule}){
 const label=document.createElement('label');label.textContent='Origem do áudio';
 const select=document.createElement('select');select.dataset.captureTarget='';select.setAttribute('aria-label','Aplicativo ou som do PC');label.append(select);
 const refresh=document.createElement('button');refresh.textContent='Atualizar aplicativos';refresh.dataset.captureRefresh='';
 const status=document.createElement('p');status.className='player-reason';status.dataset.captureStatus='';
 const timing=document.createElement('p');timing.className='small';timing.dataset.captureTiming='';
 content.append(label,refresh,status,timing);
 const view={select,refresh,status,timing,targets:[],loading:false,error:'',stamp:'',selection:null,state:null};views.set(content,view);
 async function update(){
  view.loading=true;view.error='';view.stamp='';
  try{
   let result=await api('capture.targets',{refresh:true});
   const deadline=Date.now()+5000;
   while(result.refreshing){if(Date.now()>deadline)throw new Error('A lista demorou a atualizar. Tente novamente.');await new Promise(r=>setTimeout(r,100));result=await api('capture.targets');}
   view.targets=result.targets;
  }catch(error){view.error=error.message;}finally{view.loading=false;view.stamp='';}
 }
 refresh.onclick=()=>void perform(update);
 select.onchange=()=>void perform(async()=>{
  const target=view.targets.find(t=>t.token===select.value);
  const selection=select.value==='system'?{mode:'system'}:target?{mode:'application',executable:target.executable}:null;
  await configure(id,selection,target?.token);
 });
 // A new block lists applications automatically; selection never auto-starts an
 // unrelated app. Detached views may finish a bounded read, without mutation.
 schedule(update);
}
export function syncCapture(root,runtime,busy,online){
 const content=root.querySelector('.node-content'),v=views.get(content);if(!v)return;
 const selection=runtime?.capture_selection??null,state=runtime?.capture;
 const stamp=JSON.stringify([v.targets,selection,state?.process_id]);
 if(v.stamp!==stamp){
  v.stamp=stamp;v.select.replaceChildren(new Option('Escolher aplicativo…',''),new Option('Som do PC · experimental · sem Nodivu','system'));
  for(const target of v.targets){const option=new Option(`${target.label} · ${target.process_id}`,target.token);option.title=target.executable;v.select.add(option);}
  let value='';
  if(selection?.mode==='system')value='system';
  if(selection?.mode==='application'){
   const target=v.targets.find(t=>t.executable===selection.executable&&t.process_id===state?.process_id);
   if(target)value=target.token;else{value='saved';const item=new Option('Selecionado: '+selection.executable.split(/[\\/]/).pop(),value);item.disabled=true;v.select.add(item);}
  }
  v.select.value=value;
 }
 v.select.disabled=busy||!online||v.loading;v.refresh.disabled=busy||!online||v.loading;
 v.status.textContent=v.error||(v.loading?'Atualizando aplicativos…':captureMessage(state,selection));
 v.timing.textContent=state?.status===2?`Fila: ${(state.queued_frames/48).toFixed(1)} ms · aquisição média: ${(state.packet_age_us/1000).toFixed(1)} ms · descartados: ${state.dropped_frames}`:'';
}

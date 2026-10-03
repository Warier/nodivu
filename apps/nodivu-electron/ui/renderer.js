import { mountCapture, syncCapture } from './capture-source.mjs';
import { createViewport } from './viewport.js';
import { blocks } from './blocks/index.js';
import { pluginDescriptor } from './blocks/plugin.js';
import { cableInventory, cableRouteMessage, outputPresentation } from './virtual-cable.mjs';
import { audioPresentation, playerPresentation, formatTime } from './playback-state.mjs';
const externalBlocks=new Map();
const descriptor=node=>node.block.kind==='plugin'?externalBlocks.get(node.block.plugin_id):blocks[node.block.kind];
const fieldValue=(node,key)=>key.startsWith('parameter:')?node.block.parameters[key.slice(10)]:node.block[key];
const $=id=>document.getElementById(id);
window.addEventListener('error',event=>window.nodivu.reportFault(event.message));
window.addEventListener('unhandledrejection',event=>window.nodivu.reportFault(event.reason?.message||String(event.reason)));
let snapshot=null,devices=[],online=false,busy=false,polling=null,minimized=false,message='',armed=null,drag=null;
let deviceIssue='';
const positions=new Map(),views=new Map(),pending=new Map(),resources=new Map(),playerErrors=new Map();let editTimer;
let savedStamp=null,projectPath='',lastDirty=null;
const camera=createViewport({viewport:$('viewport'),canvas:$('canvas'),onChange:()=>{draw();updateDirty();},bounds:()=>{
 if(!positions.size)return null;
 const items=[...positions].map(([id,p])=>({left:p.x-16,top:p.y-16,right:p.x+(views.get(id)?.root.offsetWidth||300)+16,bottom:p.y+(views.get(id)?.root.offsetHeight||350)+16}));
 return {left:Math.min(...items.map(p=>p.left)),top:Math.min(...items.map(p=>p.top)),right:Math.max(...items.map(p=>p.right)),bottom:Math.max(...items.map(p=>p.bottom))};
}});
const el=(tag,text,className)=>{const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(className)e.className=className;return e;};
async function api(command,params={}){const r=await window.nodivu.request(command,params);if(!r.ok){const e=new Error(r.error.message);e.code=r.error.code;throw e;}return r.result;}
function apply(value){if(!Number.isSafeInteger(value?.revision)||!Array.isArray(value.graph?.nodes)||!Array.isArray(value.graph?.edges))throw new Error('Resposta incompatível com o app.');snapshot=value;}
async function perform(action){if(busy)return;busy=true;message='';render();try{if(polling)await polling;await action();}catch(e){window.nodivu.reportFault(e.message);message=e.message;if(e.code==='revision_conflict'){try{apply(await api('session.snapshot'));}catch(e){message=e.message;}}}finally{busy=false;render();if(pending.size){clearTimeout(editTimer);editTimer=setTimeout(flush,40);}}}
function requireSession(){if(!online||!snapshot)throw new Error('O backend ainda não está pronto. Use Reconectar backend.');}
async function edit(mutator){requireSession();const graph=structuredClone(snapshot.graph);mutator(graph);apply(await api('graph.apply',{expected_revision:snapshot.revision,graph}));}
function update(id,key,value){pending.set(id+':'+key,{id,key,value});updateDirty();if(!editTimer)editTimer=setTimeout(flush,60);}
function flush(){editTimer=null;if(!online||busy||!pending.size)return;const changes=[...pending.values()];pending.clear();void perform(()=>edit(g=>{for(const c of changes){const n=g.nodes.find(n=>n.id===c.id);if(n){if(c.key.startsWith('parameter:'))n.block.parameters[c.key.slice(10)]=c.value;else n.block[c.key]=c.value;}}}));}
function projectDocument(){return {format:'nodivu-project',version:1,graph:snapshot.graph,positions:snapshot.graph.nodes.map(n=>({node_id:n.id,...positions.get(n.id)})),camera:camera.save(),captures:snapshot.graph.nodes.filter(n=>snapshot.plugin_states?.[n.id]?.capture_selection).map(n=>({node_id:n.id,selection:snapshot.plugin_states[n.id].capture_selection})),resources:snapshot.graph.nodes.filter(n=>n.block.kind==='plugin').map(n=>({node_id:n.id,path:snapshot.plugin_states?.[n.id]?.file||resources.get(n.id)})).filter(r=>r.path)};}
function updateDirty(){
 if(!snapshot||snapshot.graph.nodes.some(n=>!positions.has(n.id)))return;
 const stamp=JSON.stringify(projectDocument());if(savedStamp===null)savedStamp=stamp;
 const dirty=stamp!==savedStamp||pending.size>0;
 if(dirty!==lastDirty){lastDirty=dirty;window.nodivu.setProjectDirty(dirty);}
 $('project-name').textContent=(projectPath?projectPath.split(/[\\/]/).pop():'Projeto sem título')+(dirty?' •':'');
}
async function settleEdits(){
 clearTimeout(editTimer);editTimer=null;
 if(!pending.size)return;
 const changes=[...pending.values()];pending.clear();
 try{await edit(g=>{for(const c of changes){const n=g.nodes.find(n=>n.id===c.id);if(n){if(c.key.startsWith('parameter:'))n.block.parameters[c.key.slice(10)]=c.value;else n.block[c.key]=c.value;}}});}
 catch(e){for(const c of changes)pending.set(c.id+':'+c.key,c);throw e;}
}
function renderRecents(files){
 const select=$('project-recents');select.replaceChildren(new Option('Recentes…',''));
 for(const file of files){const option=new Option(file.split(/[\\/]/).pop(),file);option.title=file;select.add(option);}
}
function projectAction(action,recentPath){void perform(async()=>{
 requireSession();
 await settleEdits();
 const result=await window.nodivu.projectFile(action,{expected_revision:snapshot.revision,...(action==='save'?{contents:JSON.stringify(projectDocument())}:action==='open-recent'?{path:recentPath}:{})});
 if(!result.ok){const e=new Error(result.error.message);e.code=result.error.code;throw e;}
 if(result.canceled)return;
 projectPath=result.path;
 if(action==='open'||action==='open-recent'){
  armed=null;drag=null;resources.clear();playerErrors.clear();positions.clear();
  for(const view of views.values())view.root.remove();views.clear();edgeStamp='';
  for(const p of result.project.positions)positions.set(p.node_id,{x:p.x,y:p.y});
  for(const r of result.project.resources)resources.set(r.node_id,r.path);
  apply(result.snapshot);render();camera.restore(result.project.camera);
  message='';
  // Opening in the product resumes the validated session immediately. The API's
  // suspended transition only separates document replacement from device setup.
  try{await activateAudio();}catch(e){message='Não foi possível preparar o áudio: '+e.message;}
 }
 if(result.recents)renderRecents(result.recents);message=[message,result.warning].filter(Boolean).join(' ');
 savedStamp=JSON.stringify(projectDocument());updateDirty();
});}
async function refresh(){
 try{const result=await api('devices.list');if(!Array.isArray(result.devices))throw new Error('Lista de dispositivos inválida.');devices=result.devices;deviceIssue=(result.warnings||[]).join('\n');}
 catch(e){devices=[];deviceIssue=e.message;throw e;}
 finally{for(const v of views.values())v.deviceStamp='';}
}
async function boot(){
 online=false;
 const h=await api('system.hello');if(h.demo){document.body.dataset.demo='true';$('mode-label').textContent='DEMONSTRAÇÃO · sem áudio · frontend Linux/Windows';}
 if(h.api!=='graph-v2')throw new Error('Backend incompatível: reconstrua o app.');
 const catalog=await api('plugins.list');externalBlocks.clear();$('palette').querySelectorAll('[data-kind=plugin]').forEach(b=>b.remove());
 for(const p of catalog.plugins){const d=pluginDescriptor(p);externalBlocks.set(p.id,d);paletteButton(d);}
 // The editable document does not depend on MMDevice discovery succeeding.
 apply(await api('session.snapshot'));online=true;
 try{await refresh();}catch{/* refresh preserves the actual failure in deviceIssue. */}
 const recent=await window.nodivu.recentProjects();if(recent.ok)renderRecents(recent.files);else message=recent.error.message;
}
function add(kind,pluginId){if(!online||!snapshot||busy)return;void perform(async()=>{requireSession();apply(await api('node.add',{expected_revision:snapshot.revision,kind,...(kind==='plugin'?{plugin_id:pluginId}:{})}));});}
function remove(id){void perform(()=>edit(g=>{g.nodes=g.nodes.filter(n=>n.id!==id);g.edges=g.edges.filter(e=>e.from!==id&&e.to!==id);}));}
function disconnect(edge){void perform(()=>edit(g=>{g.edges=g.edges.filter(e=>e.from!==edge.from||e.to!==edge.to||(e.from_port||0)!==(edge.from_port||0)||(e.to_port||0)!==(edge.to_port||0));}));}
function connect(to,to_port=0){if(!armed||busy)return;const from=armed.id,from_port=armed.port;armed=null;void perform(()=>edit(g=>g.edges.push({from,to,from_port,to_port})));}
function location(node,index){
 if(!positions.has(node.id)){
  const base={capture:[45,75],gain:[365,175],output:[690,75],tone:[45,450],plugin:[670,300],mixer:[365,450],meter:[1000,75]}[node.block.kind];
  const width=node.block.kind==='plugin'?300:250,height=node.block.kind==='plugin'?420:310;
  const free=(x,y)=>[...positions].every(([id,p])=>{const root=views.get(id)?.root;return x+width+24<p.x||p.x+(root?.offsetWidth||300)+24<x||y+height+24<p.y||p.y+(root?.offsetHeight||310)+24<y;});
  let [x,y]=base;
  if(!free(x,y))for(let i=0;i<80;i++){x=30+(i%4)*330;y=35+Math.floor(i/4)*470;if(free(x,y))break;}
  positions.set(node.id,{x,y});
 }
 return positions.get(node.id);
}
function canvasSize(){let width=1600,height=1100;for(const[id,p]of positions){const r=views.get(id)?.root;width=Math.max(width,p.x+(r?.offsetWidth||300)+40);height=Math.max(height,p.y+(r?.offsetHeight||420)+40);}$('canvas').style.width=width+'px';$('canvas').style.height=height+'px';}
function organize(){
 if(!snapshot)return;
 const ordered=[],seen=new Set();
 const walk=node=>{while(node&&!seen.has(node.id)){seen.add(node.id);ordered.push(node);const edge=snapshot.graph.edges.find(e=>e.from===node.id);node=edge?snapshot.graph.nodes.find(n=>n.id===edge.to):null;}};
 for(const node of snapshot.graph.nodes.filter(n=>!snapshot.graph.edges.some(e=>e.to===n.id)))walk(node);
 for(const node of snapshot.graph.nodes)walk(node);
 const columns=Math.max(1,Math.min(4,Math.floor(($('viewport').clientWidth-40)/320)));
 let y=35;for(let i=0;i<ordered.length;i+=columns){const row=ordered.slice(i,i+columns);row.forEach((n,j)=>positions.set(n.id,{x:30+j*320,y}));y+=Math.max(...row.map(n=>views.get(n.id).root.offsetHeight))+45;}
 render();camera.fit();
}
function build(node){
 const d=descriptor(node);if(!d)throw new Error('Tipo de bloco desconhecido.');const root=el('article',undefined,'node '+node.block.kind);root.dataset.id=node.id;root.dataset.kind=node.block.kind;if(d.accent){root.style.setProperty('--plugin-accent',d.accent);root.dataset.layout=d.layout;}
 const title=el('div',undefined,'node-title');title.append(el('span',d.icon,'icon'),el('div',d.title));title.lastChild.append(el('small',d.subtitle));
 const close=el('button','×','node-close');close.title='Remover bloco e seus fios';close.setAttribute('aria-label','Remover '+d.title);close.onclick=()=>remove(node.id);title.append(close);root.append(title);
 const content=el('div',undefined,'node-content'),fields=new Map();
 let lastGroup='';for(const f of d.fields){if(f.group&&f.group!==lastGroup){content.append(el('h3',f.group,'plugin-group'));lastGroup=f.group;}const label=el('label',f.label),control=el(f.type==='device'?'select':'input'),value=el('strong','','field-value');control.dataset.field=f.key;
  if(f.type!=='device')control.type=f.type==='range'?'range':f.type==='number'?'number':'checkbox';
  if(f.type==='range'||f.type==='number'){control.min=f.min;control.max=f.max;control.step=f.step;control.oninput=()=>{update(node.id,f.key,Number(control.value));value.textContent=f.unit===undefined?Number(control.value).toFixed(1)+' dB · '+(100*Math.pow(10,Number(control.value)/20)).toFixed(1)+'%':Number(control.value).toFixed(3)+' '+f.unit;};label.append(value);}
  else control.onchange=()=>update(node.id,f.key,f.type==='checkbox'?(f.key.startsWith('parameter:')?Number(control.checked):control.checked):control.value||null);
  label.append(control);content.append(label);fields.set(f.key,{field:f,control,value});}
 if(d.filePlayer){
  const reload=el('button','Carregar áudio salvo'),choose=el('button','Escolher áudio'),play=el('button',d.fileTransport?'▶ Reproduzir':'▶ Play uma vez'),status=el('p','','small'),reason=el('p','','player-reason');
  choose.dataset.action='file';play.dataset.action='play';reload.dataset.action='reload';status.dataset.playerStatus='';reason.dataset.playerBlocker='';reason.id='player-reason-'+node.id;play.setAttribute('aria-describedby',reason.id);
  choose.onclick=()=>void perform(async()=>{const path=await window.nodivu.chooseFile();if(path)await playerCommand(node.id,'load',path);});
  play.onclick=()=>void perform(async()=>{await settleEdits();const playing=d.fileTransport&&snapshot.plugin_states?.[node.id]?.status===3;if(playing||playerState(snapshot.graph.nodes.find(n=>n.id===node.id),false).canPlay)await playerCommand(node.id,playing?'pause':'play');});
  reload.onclick=()=>void perform(async()=>{const path=resources.get(node.id);if(path)await playerCommand(node.id,'load',path);});
  content.append(choose,reload,play);
  if(d.fileTransport){
   const seek=el('input',undefined,'player-seek'),time=el('output','0:00 / 0:00','player-time');
   seek.type='range';seek.min=0;seek.max=0;seek.step=100;seek.value=0;seek.dataset.action='seek';seek.setAttribute('aria-label','Posição no áudio');time.dataset.playerTime='';
   seek.oninput=()=>{seek.dataset.scrubbing='true';time.textContent=formatTime(Number(seek.value))+' / '+formatTime(Number(seek.max));};
   seek.onchange=()=>{const ms=Math.round(Number(seek.value));delete seek.dataset.scrubbing;void perform(()=>playerCommand(node.id,'seek',undefined,ms));};
   seek.onpointercancel=seek.onblur=()=>{delete seek.dataset.scrubbing;};
   content.append(seek,time);
  }
  content.append(status,reason);
 }
 if(d.captureSource)mountCapture(content,node.id,{api,perform,schedule:action=>{const attempt=()=>{if(!views.has(node.id))return;if(busy||polling){setTimeout(attempt,80);return;}void perform(action);};setTimeout(attempt,0);},configure:async(id,selection,token)=>{await settleEdits();apply(await api('capture.configure',{node_id:id,selection,...(token?{token}:{})}));}});
 if(d.worker){const status=el('p','','small'),restart=el('button','Reiniciar worker');status.dataset.workerStatus='';restart.dataset.action='restart';restart.onclick=()=>void perform(async()=>apply(await api('plugin.command',{node_id:node.id,action:'restart'})));content.append(status,restart);}
 const note=el('p',d.note,'small');note.dataset.blockNote='';content.append(note);root.append(content);
 const level=el('meter',undefined,'node-meter');level.min=0;level.max=1;level.value=0;level.dataset.nodeMeter='';const levelText=el('small','','node-level');levelText.dataset.nodeLevel='';content.append(level,levelText);
 for(const [port,count]of[['sink',d.inputs],['source',d.outputs]])for(let index=0;index<count;index++){
  const b=el('button',count>1?String(index+1):'',`port ${port}`);b.dataset.port=port;b.dataset.portId=String(index);b.dataset.node=node.id;
  if(count>1)b.style.top=(92+index*42)+'px';b.setAttribute('aria-label',(port==='source'?'Enviar de ':'Receber em ')+d.title+' '+(index+1));
  if(port==='source'){b.onpointerdown=e=>{if(e.button!==0||busy)return;armed={id:node.id,port:index};$('viewport').dataset.editing='true';b.setPointerCapture(e.pointerId);draw();};b.onclick=e=>{if(e.detail===0){armed=armed?.id===node.id&&armed?.port===index?null:{id:node.id,port:index};draw();}};}else b.onclick=()=>connect(node.id,index);root.append(b);
 }
 title.onpointerdown=e=>{if(e.button!==0||e.target.closest('button'))return;const p=positions.get(node.id);drag={id:node.id,x:e.clientX,y:e.clientY,left:p.x,top:p.y};$('viewport').dataset.editing='true';title.setPointerCapture(e.pointerId);};
 $('nodes').append(root);const view={root,fields,close,deviceStamp:''};views.set(node.id,view);return view;
}
function syncNode(node,index){const pos=location(node,index),v=views.get(node.id)||build(node);v.root.style.left=pos.x+'px';v.root.style.top=pos.y+'px';v.close.disabled=busy;const ps=snapshot.plugin_states?.[node.id];syncCapture(v.root,ps,busy,online);if(ps?.file)resources.set(node.id,ps.file);const status=v.root.querySelector('[data-player-status]');if(status){
  const state=playerState(node),play=v.root.querySelector('[data-action=play]'),reason=v.root.querySelector('[data-player-blocker]');
  status.textContent=state.name+' · '+state.fileState;status.title=state.file;
  const transport=descriptor(node).fileTransport,playing=transport&&ps?.status===3;
  play.disabled=busy||!online||(!playing&&!state.canPlay);play.textContent=transport?(playing?'Ⅱ Pausar':ps?.status===5?'▶ Continuar':ps?.status===4?'▶ Reproduzir novamente':'▶ Reproduzir'):'▶ Play uma vez';play.title=playing?'Pausar nesta posição':state.reason||'Reproduzir a partir desta posição';
  const seek=v.root.querySelector('[data-action=seek]');if(seek){const duration=ps?.duration_ms||0;seek.max=duration;seek.disabled=busy||!online||![2,3,4,5].includes(ps?.status)||!duration;if(!seek.dataset.scrubbing){seek.value=Math.min(ps?.position_ms||0,duration);v.root.querySelector('[data-player-time]').textContent=formatTime(Number(seek.value))+' / '+formatTime(duration);}seek.setAttribute('aria-valuetext',formatTime(Number(seek.value))+' de '+formatTime(duration));}
  reason.textContent=state.reason||state.warning;reason.hidden=!reason.textContent;
  v.root.querySelector('[data-action=file]').disabled=busy||!online;
 }
 if(node.block.kind==='output'){const p=outputPresentation(devices.find(d=>d.endpoint_id===node.block.endpoint_id)),title=v.root.querySelector('.node-title>div');if(title.firstChild.textContent!==p.title){title.firstChild.textContent=p.title;title.querySelector('small').textContent=p.subtitle;}v.root.querySelector('[data-block-note]').textContent=p.note;}
 const ws=v.root.querySelector('[data-worker-status]');if(ws){const labels=['Aguardando áudio','Preparando Python…','Processando','Falha'];ws.textContent=(labels[ps?.status??0]||'Indisponível')+' · +20 ms · '+(ps?.missing_blocks??0)+' blocos sem resposta'+(ps?.error?' · '+ps.error:'');v.root.querySelector('[data-action=restart]').disabled=busy||!online;}
 const reload=v.root.querySelector('[data-action=reload]');if(reload){reload.hidden=!resources.has(node.id);reload.disabled=busy||!online||ps?.status===1;reload.textContent=ps?.file?'Trocar / recarregar áudio':'Carregar áudio salvo';reload.title=resources.get(node.id)||'';}
 for(const{field:f,control,value}of v.fields.values()){
  if(f.type==='device'){const a=devices.filter(d=>d.flow===f.flow&&d.state==='active').sort((a,b)=>Number(b.is_default)-Number(a.is_default)||a.name.localeCompare(b.name));const stamp=JSON.stringify(a)+node.block[f.key];
   if(v.deviceStamp!==stamp){control.replaceChildren(new Option('Nenhum · liberar dispositivo',''));for(const d of a)control.add(new Option((d.is_default?'★ Padrão · ':'')+d.name,d.endpoint_id));const current=node.block[f.key];if(current&&!a.some(d=>d.endpoint_id===current))control.add(new Option('Selecionado indisponível · remova ou substitua',current));control.value=current||'';v.deviceStamp=stamp;}
  }else if(!pending.has(node.id+':'+f.key)&&document.activeElement!==control){if(f.type==='checkbox')control.checked=Boolean(fieldValue(node,f.key));else control.value=fieldValue(node,f.key)??f.default;}
  if(f.type==='range')value.textContent=f.unit===undefined?Number(control.value).toFixed(1)+' dB · '+(100*Math.pow(10,Number(control.value)/20)).toFixed(1)+'%':Number(control.value).toFixed(3)+' '+f.unit;control.disabled=!online||f.readonly===true;
 }
}
function point(id,port,index=0){const p=views.get(id)?.root.querySelector('[data-port="'+port+'"][data-port-id="'+index+'"]');if(!p)return{x:0,y:0};const r=p.getBoundingClientRect(),c=$('canvas').getBoundingClientRect();return{x:(r.left+r.width/2-c.left)/camera.scale,y:(r.top+r.height/2-c.top)/camera.scale};}
function curve(a,b){const bend=Math.max(70,Math.abs(b.x-a.x)*.45);return`M ${a.x} ${a.y} C ${a.x+bend} ${a.y}, ${b.x-bend} ${b.y}, ${b.x} ${b.y}`;}
function draw(cursor){for(const p of $('wires').querySelectorAll('.wire')){const e=JSON.parse(p.dataset.edge);p.setAttribute('d',curve(point(e.from,'source',e.from_port||0),point(e.to,'sink',e.to_port||0)));}const a=armed?point(armed.id,'source',armed.port):null;$('preview').setAttribute('d',a?curve(a,cursor||{x:a.x+90,y:a.y}):'');for(const v of views.values())v.root.querySelector('[data-port="source"]')?.classList.toggle('armed',v.root.dataset.id===armed?.id);}
let edgeStamp='';
function wires(){const stamp=JSON.stringify(snapshot.graph.edges)+snapshot.graph.nodes.map(n=>n.id).join();if(stamp===edgeStamp)return;edgeStamp=stamp;$('wires').querySelectorAll('.wire').forEach(p=>p.remove());$('connections').replaceChildren();
 for(const e of snapshot.graph.edges){const p=document.createElementNS('http://www.w3.org/2000/svg','path');p.classList.add('wire');p.dataset.edge=JSON.stringify(e);p.oncontextmenu=event=>{event.preventDefault();disconnect(e);};$('wires').append(p);const name=id=>descriptor(snapshot.graph.nodes.find(n=>n.id===id)).title;const b=el('button',name(e.from)+' → '+name(e.to)+' / '+((e.to_port||0)+1)+' ×','quiet edge-remove');b.title='Apagar conexão';b.onclick=()=>disconnect(e);$('connections').append(b);}if(!snapshot.graph.edges.length)$('connections').append(el('p','Nenhum fio.'));
}
function meter(id,peak){const db=Number.isFinite(peak)&&peak>0?20*Math.log10(peak):-Infinity;$(id+'-meter').value=Math.max(0,Math.min(1,(db+60)/60));$(id+'-db').textContent=Number.isFinite(db)?db.toFixed(1)+' dBFS':'Silêncio';}
function render(){
 $('project-recents').disabled=!online||busy||$('project-recents').options.length<2;
 const cable=cableInventory(devices);$('cable-status').textContent=cable.message;$('cable-setup').dataset.state=cable.state;$('cable-route').textContent=cableRouteMessage(snapshot,devices);$('virtual-output').disabled=!online||busy;
 $('reconnect').hidden=online;for(const id of['mute-all','refresh','retry','quick-start','project-save','project-open'])$(id).disabled=!online||busy;$('reconnect').disabled=busy;document.body.dataset.ready=String(online);document.body.dataset.busy=String(busy);
 renderAudio();
 // Disable palette actions even before the first valid snapshot exists.
 for(const b of $('palette').querySelectorAll('button'))if(!snapshot||!online||busy)b.disabled=true;
 if(!snapshot){$('status').textContent=busy?'Conectando…':'Backend indisponível';$('error').textContent=message;$('error').hidden=!message;return;}
 for(const[id,v]of views)if(!snapshot.graph.nodes.some(n=>n.id===id)){v.root.remove();views.delete(id);positions.delete(id);resources.delete(id);playerErrors.delete(id);if(armed?.id===id)armed=null;}
 snapshot.graph.nodes.forEach(syncNode);
 for(const node of snapshot.graph.nodes){const v=views.get(node.id),metric=snapshot.metrics?.route_nodes?.find(m=>m.node_id===node.id),peak=online&&snapshot.engine_state==='running'?(metric?.peak||0):0,db=peak>0?20*Math.log10(peak):-Infinity;v.root.querySelector('[data-node-meter]').value=Math.max(0,Math.min(1,(db+60)/60));v.root.querySelector('[data-node-level]').textContent=Number.isFinite(db)?db.toFixed(1)+' dBFS':'Silêncio';}
canvasSize();$('empty').hidden=snapshot.graph.nodes.length>0;
 for(const b of $('palette').querySelectorAll('button')){const count=snapshot.graph.nodes.filter(n=>n.block.kind===b.dataset.kind).length;b.disabled=busy||!online||count>=(['gain','plugin'].includes(b.dataset.kind)?8:['mixer','meter'].includes(b.dataset.kind)?8:1);}
 wires();draw();$('mute-all').textContent=snapshot.muted?'Restaurar som':'Silenciar tudo';$('mute-all').classList.toggle('primary',snapshot.muted);
 $('retry').hidden=!snapshot.suspended&&!snapshot.last_error&&!snapshot.capture_error;
 $('retry').textContent='Tentar reabrir dispositivos';
 const state=snapshot.engine_state;$('status').textContent=!online?'Backend desconectado':snapshot.suspended?'Preparando áudio do projeto…':state==='running'?(snapshot.signal?'● Áudio conectado':'● Saída pronta · silêncio'):state==='starting'?'Abrindo dispositivos…':state==='error'?'Falha nos dispositivos':'Escolha uma saída';$('status').classList.toggle('active',online&&state==='running');if(snapshot.demo)$('status').textContent='Demonstração · sem áudio';$('revision').textContent='Revisão '+snapshot.revision;
 $('signal-info').textContent=snapshot.muted?'Tudo silenciado.':snapshot.signal===1?'Microfone → saída':snapshot.signal===2?'Tom de teste → saída':snapshot.signal===3?'Plugin → saída':snapshot.signal===4?'Mistura de sinais → saída':'Sem caminho completo · silêncio';const m=online?snapshot.metrics:null;meter('input',m?.input_peak);meter('output',m?.output_peak);$('metrics-info').textContent=m?`Abertura ${snapshot.generation} · Frames: ${m.rendered_frames}\nFalta de captura: ${m.underruns}\nSaída: ${m.render_starvations} · clipping: ${m.clipped_samples}`:'Dispositivos ainda não abertos.';
 const error=message||snapshot.last_error?.message||snapshot.capture_error?.message||deviceIssue||'';$('error').textContent=error;$('error').hidden=!error;document.body.dataset.state=state;document.body.dataset.revision=String(snapshot.revision);document.body.dataset.generation=String(snapshot.generation);document.body.dataset.signal=String(snapshot.signal);updateDirty();
}
function playerState(node,operationBusy=busy){return playerPresentation({snapshot,node,devices,online,busy:operationBusy,savedPath:resources.get(node.id),error:playerErrors.get(node.id)});}
async function playerCommand(id,action,path,position_ms){
 playerErrors.delete(id);
 try{apply(await api('plugin.command',{node_id:id,action,...(path?{path}:{}),...(position_ms!==undefined?{position_ms}:{})}));}
 catch(e){playerErrors.set(id,e.message);throw e;}
}
async function activateAudio(){
 await settleEdits();await refresh();apply(await api('audio.retry'));
 // Opening a user-selected project prepares its saved files, never starts Play.
 const failures=[];
 for(const node of snapshot.graph.nodes){
  if(!descriptor(node)?.filePlayer||!playerState(node).needsLoad)continue;
  try{await playerCommand(node.id,'load',resources.get(node.id));}catch(e){failures.push(e.message);}
 }
 if(failures.length)message='Alguns arquivos não carregaram. Veja o motivo em cada player.';
}
async function focusOutput(){
 await settleEdits();if(!snapshot.graph.nodes.some(n=>n.block.kind==='output'))apply(await api('node.add',{expected_revision:snapshot.revision,kind:'output'}));
 render();camera.fit();const output=snapshot.graph.nodes.find(n=>n.block.kind==='output');views.get(output.id).fields.get('endpoint_id').control.focus({preventScroll:true});
}
function renderAudio(){
 const state=audioPresentation(snapshot,devices,online,deviceIssue);$('audio-title').textContent=state.title;$('audio-detail').textContent=state.message;
 const button=$('audio-action');button.hidden=!state.action;button.textContent=busy?'Aguarde…':state.label||'';button.disabled=busy||(!online&&state.action!=='reconnect');
 $('audio-panel').dataset.ready=String(Boolean(state.ready));
}
$('audio-action').onclick=()=>void perform(async()=>{
 const state=audioPresentation(snapshot,devices,online,deviceIssue);
 if(state.action==='reconnect')await reconnect();else if(state.action==='refresh')await refresh();else if(state.action==='activate')await activateAudio();else if(state.action==='output')await focusOutput();else if(state.action==='unmute')apply(await api('audio.mute',{muted:false}));
});
async function reconnect(){online=false;pending.clear();await window.nodivu.restart();snapshot=null;deviceIssue='';devices=[];positions.clear();resources.clear();playerErrors.clear();savedStamp=null;projectPath='';await boot();}
function paletteButton(d){const b=el('button',undefined,'block-choice');b.dataset.kind=d.kind;b.append(el('span',d.icon,'icon'),el('span',d.title),el('b','+'));b.dataset.pluginId=d.pluginId||'';b.onclick=()=>add(d.kind,d.pluginId);$('palette').append(b);}
for(const d of Object.values(blocks))paletteButton(d);
// The same output owns WASAPI and its clock. This shortcut never selects a device
// or creates a second output, and preserves the existing route and saved IDs.
$('virtual-output').onclick=()=>void perform(async()=>{await settleEdits();if(!snapshot.graph.nodes.some(n=>n.block.kind==='output'))apply(await api('node.add',{expected_revision:snapshot.revision,kind:'output'}));render();camera.fit();$('cable-setup').open=true;const output=snapshot.graph.nodes.find(n=>n.block.kind==='output');views.get(output.id).fields.get('endpoint_id').control.focus({preventScroll:true});});
$('project-recents').onchange=()=>{const file=$('project-recents').value;$('project-recents').value='';if(file)projectAction('open-recent',file);};
$('quick-start').onclick=()=>void perform(async()=>{for(const kind of['capture','output'])if(!snapshot.graph.nodes.some(n=>n.block.kind===kind))apply(await api('node.add',{expected_revision:snapshot.revision,kind}));});
$('mute-all').onclick=()=>void perform(async()=>apply(await api('audio.mute',{muted:!snapshot.muted})));$('refresh').onclick=()=>void perform(refresh);$('retry').onclick=()=>void perform(activateAudio);$('reconnect').onclick=()=>void perform(reconnect);$('center').onclick=organize;$('project-save').onclick=()=>projectAction('save');$('project-open').onclick=()=>projectAction('open');
document.addEventListener('keydown',event=>{if((event.ctrlKey||event.metaKey)&&['s','o'].includes(event.key.toLowerCase())){event.preventDefault();if(online&&!busy)projectAction(event.key.toLowerCase()==='s'?'save':'open');}});
document.addEventListener('pointermove',e=>{if(drag){const p={x:drag.left+(e.clientX-drag.x)/camera.scale,y:drag.top+(e.clientY-drag.y)/camera.scale};positions.set(drag.id,p);const root=views.get(drag.id).root;root.style.left=p.x+'px';root.style.top=p.y+'px';canvasSize();draw();updateDirty();}else if(armed&&e.buttons){draw(camera.world(e.clientX,e.clientY));}});
document.addEventListener('pointerup',e=>{drag=null;$('viewport').dataset.editing='false';if(!armed||e.button!==0)return;for(const p of document.querySelectorAll('.port[data-port="sink"]')){const r=p.getBoundingClientRect();if(e.clientX>=r.left-8&&e.clientX<=r.right+8&&e.clientY>=r.top-8&&e.clientY<=r.bottom+8){connect(p.dataset.node,Number(p.dataset.portId));break;}}draw();});
document.addEventListener('pointercancel',()=>{armed=null;drag=null;$('viewport').dataset.editing='false';draw();});window.addEventListener('blur',()=>{armed=null;drag=null;$('viewport').dataset.editing='false';draw();});document.addEventListener('keydown',e=>{if(e.key==='Escape'){armed=null;draw();}});window.addEventListener('resize',()=>draw());window.nodivu.onFault(error=>{online=false;message=error;render();});window.nodivu.onMinimized(v=>{minimized=v;});
let lastPoll=0;setInterval(()=>{if(!online||busy||polling||Date.now()-lastPoll<(minimized?1000:100))return;lastPoll=Date.now();polling=api('session.snapshot').then(apply).catch(e=>{message=e.message;}).finally(()=>{polling=null;render();});},100);void perform(boot);

$('project-folder').onclick=()=>void perform(async()=>{const result=await window.nodivu.projectFile('folder',{});if(!result.ok)throw new Error(result.error.message);});

$('diagnostics-open').onclick=()=>void perform(async()=>{const result=await window.nodivu.openDiagnostics();if(!result.ok)throw new Error(result.error.message);});

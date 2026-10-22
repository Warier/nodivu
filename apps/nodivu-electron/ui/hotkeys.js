const $=id=>document.getElementById(id),dialog=$('hotkeys-dialog');
let state=null,editing=null,filter=null;
const element=(tag,text)=>{const e=document.createElement(tag);if(text!==undefined)e.textContent=text;return e;};
async function call(action,payload){const result=await window.nodivu.hotkeys(action,payload);if(!result.ok)throw Error(result.error);return result.state;}
function show(next){
 state=next;$('hotkeys-enabled').checked=state.config.enabled;
 $('hotkeys-status').textContent=state.error||state.lastResult?.message||'Nenhuma combinação é ativada por padrão. Configure e salve para registrar.';
 $('hotkeys-status').dataset.error=String(!!state.error||state.lastResult?.ok===false);
 const select=$('hotkey-action'),previous=select.value;select.replaceChildren();
 for(const a of state.actions.filter(a=>!filter||a.nodeId===filter))select.add(new Option(a.label,a.id));
 if([...select.options].some(o=>o.value===previous))select.value=previous;
 const body=$('hotkeys-list');body.replaceChildren();
 for(const b of state.config.bindings){
  const row=element('tr');for(const text of [b.accelerator,b.scope==='global'?'Global':'No Nodivu',b.label,state.states[b.id]||'Pendente'])row.append(element('td',text));
  const controls=element('td');
  const button=(label,fn)=>{const e=element('button',label);e.type='button';e.onclick=()=>void act(fn);controls.append(e);};
  button('Editar',()=>{editing=b.id;filter=null;show(state);if(![...select.options].some(o=>o.value===b.action))select.add(new Option(b.label+' (ausente)',b.action));select.value=b.action;$('hotkey-key').value=b.accelerator;$('hotkey-scope').value=b.scope;$('hotkey-value').value=b.value??'';$('hotkey-save').textContent='Salvar alteração';valueHint();});
  button(b.enabled?'Desativar':'Ativar',async()=>show(await call('save',{...state.config,bindings:state.config.bindings.map(x=>x.id===b.id?{...x,enabled:!x.enabled}:x)})));
  button('Testar',async()=>show(await call('test',{id:b.id})));
  button('Excluir',async()=>{show(await call('save',{...state.config,bindings:state.config.bindings.filter(x=>x.id!==b.id)}));if(editing===b.id)reset();});
  row.append(controls);body.append(row);
 }
 valueHint();
}
function valueHint(){const a=state?.actions.find(a=>a.id===$('hotkey-action').value);$('hotkey-value-wrap').hidden=!a?.needsValue;$('hotkey-range').textContent=a?.needsValue?`Valor na unidade do controle${a.min!==undefined?' · '+a.min+' a '+a.max:''}. Use valor negativo para diminuir.`:'';}
function reset(){editing=null;$('hotkey-key').value='';$('hotkey-value').value='';$('hotkey-save').textContent='Adicionar atalho';}
async function act(fn){try{await fn();}catch(e){$('hotkeys-status').textContent=e.message;$('hotkeys-status').dataset.error='true';}}
export async function openHotkeys(nodeId=null){
 filter=nodeId;reset();dialog.showModal();
 await act(async()=>{await call('suspend',true);show(await call('list'));});
}
$('hotkeys-close').onclick=()=>dialog.close();
dialog.addEventListener('close',()=>void act(async()=>{await call('suspend',false);}));
$('hotkeys-enabled').onchange=()=>void act(async()=>show(await call('save',{...state.config,enabled:$('hotkeys-enabled').checked})));
$('hotkeys-all').onclick=()=>{filter=null;show(state);};
$('hotkey-action').onchange=valueHint;
$('hotkey-cancel').onclick=reset;
$('hotkey-form').onsubmit=e=>{e.preventDefault();void act(async()=>{
 const action=state.actions.find(a=>a.id===$('hotkey-action').value);if(!action)throw Error('Escolha uma ação disponível no projeto atual.');
 const value=$('hotkey-value').value;
 if(action.needsValue&&(value.trim()===''||!Number.isFinite(Number(value))))throw Error('Informe o valor do controle.');
 const binding={id:editing||crypto.randomUUID(),accelerator:$('hotkey-key').value,scope:$('hotkey-scope').value,action:action.id,label:action.label,enabled:true,...(action.needsValue?{value:Number(value)}:{})};
 const bindings=state.config.bindings.filter(b=>b.id!==editing);bindings.push(binding);
 show(await call('save',{...state.config,bindings}));reset();
 });};
$('hotkey-key').onkeydown=e=>{
 if(e.key==='Tab')return;e.preventDefault();e.stopPropagation();
 if(['Control','Alt','Shift','Meta','Dead','AltGraph'].includes(e.key))return;
 const names={ArrowUp:'Up',ArrowDown:'Down',ArrowLeft:'Left',ArrowRight:'Right',' ':'Space'};
 $('hotkey-key').value=[e.ctrlKey?'Control':null,e.altKey?'Alt':null,e.shiftKey?'Shift':null,e.metaKey?'Win':null,names[e.key]||e.key].filter(Boolean).join('+');
};
window.nodivu.onHotkeys(state=>{const conflicts=Object.values(state.states).filter(s=>/Conflito|Falha ao registrar/.test(s)).length;$('hotkeys-open').textContent='⌨ Atalhos de teclado'+(conflicts?' · '+conflicts+' conflito(s)':'');if(dialog.open)show(state);});

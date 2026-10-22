// Capabilities are derived from the current graph and validated plugin controls.
// Bindings refer to UUID + plugin ID, never to order, display name or a device name.
export function actionsFor(snapshot,descriptor){
 const actions=[{id:'app.show',label:'Aplicativo · mostrar janela',operation:'show'}];
 if(!snapshot)return actions;
 for(const [operation,label] of [['mute.toggle','alternar silêncio geral'],['mute.on','silenciar tudo'],['mute.off','restaurar som'],['monitor.toggle','alternar escuta local']])actions.push({id:'app.'+operation,label:'Áudio · '+label,operation});
 for(const node of snapshot.graph.nodes){
  if(snapshot.missing_plugins?.some(p=>p.node_id===node.id))continue;
  const d=descriptor(node),prefix=node.id+'/'+(node.block.plugin_id||node.block.kind)+'/';
  const add=(key,label,extra={})=>actions.push({id:prefix+key,label:`${d.title} [${node.id.slice(0,8)}] · ${label}`.slice(0,240),nodeId:node.id,...extra});
  if(d.filePlayer)for(const [op,label] of [['play','reproduzir'],...(d.fileTransport?[['toggle','play / pause'],['pause','pausar'],['restart','recomeçar'],['stop','parar e voltar ao início']]:[])])add('player.'+op,label,{operation:'player.'+op});
  for(const field of d.fields||[]){
   if(field.readonly||field.type==='momentary'||!['checkbox','range','number','select'].includes(field.type))continue;
   const base={field:field.key,min:field.min,max:field.max,stepped:!!field.stepped,options:field.options};
   if(field.type==='checkbox')add(field.key+'.toggle',field.label+' · alternar',{...base,operation:'toggle'});
   else{
    add(field.key+'.set',field.label+' · definir valor',{...base,operation:'set',needsValue:true});
    if(field.type!=='select')add(field.key+'.adjust',field.label+' · somar valor (+/−)',{...base,operation:'adjust',needsValue:true,min:undefined,max:undefined});
   }
  }
 }
 return actions;
}
export function changeField(node,action,value){
 const parameter=action.field.startsWith('parameter:'),key=parameter?action.field.slice(10):action.field;
 const previous=parameter?node.block.parameters[key]:node.block[key];
 let next;
 if(action.operation==='toggle')next=parameter?Number(!previous):!previous;
 else{
  if(!Number.isFinite(value))throw Error('Informe um valor numérico.');
  next=action.operation==='adjust'?previous+value:value;
  if(action.operation==='adjust'){
   // Bounds below are resolved again from the live descriptor by the caller.
   next=Math.max(action.min,Math.min(action.max,next));
  }
  if(!Number.isFinite(next)||next<action.min||next>action.max||(action.options?.length&&!action.options.some(o=>o.value===next))||(action.stepped&&!Number.isInteger(next)))throw Error('Valor fora do contrato do controle.');
 }
 if(parameter)node.block.parameters[key]=next;else node.block[key]=next;
}

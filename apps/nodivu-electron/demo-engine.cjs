// Explicit UI simulator. No child process, device, PCM, decoder or DSP.
// Production validation belongs to Rust; this model only enables UI iteration.
const {EventEmitter}=require('node:events');
const {randomUUID}=require('node:crypto');
const catalog=require('./demo/catalog.json');
const copy=value=>structuredClone(value);
class DemoEngine extends EventEmitter {
 constructor(){super();this.closed=false;this.revision=0;this.graph={schema_version:2,nodes:[],edges:[]};this.states={};this.muted=false;this.suspended=false;}
 snapshot(){return copy({demo:true,revision:this.revision,graph:this.graph,plugin_states:this.states,engine_state:'idle',generation:0,signal:0,muted:this.muted,suspended:this.suspended,metrics:null,last_error:null,capture_error:null});}
 checkGraph(graph){
  if(!graph||!Array.isArray(graph.nodes)||!Array.isArray(graph.edges)||graph.nodes.length>32||graph.edges.length>64)throw new Error('Grafo de demonstração inválido.');
  const ids=new Set(graph.nodes.map(n=>n.id));if(ids.size!==graph.nodes.length)throw new Error('IDs duplicados.');
  for(const n of graph.nodes)if(!['capture','output','gain','tone','mixer','meter','plugin'].includes(n.block?.kind)||n.block.kind==='plugin'&&!catalog.plugins.some(p=>p.id===n.block.plugin_id))throw new Error('Bloco não disponível na demonstração.');
  const ports=new Set();for(const e of graph.edges){const key=e.to+':'+(e.to_port||0);if(!ids.has(e.from)||!ids.has(e.to)||e.from===e.to||ports.has(key))throw new Error('Fio inválido ou entrada ocupada.');ports.add(key);}
  const visit=(id,stack)=>{if(stack.has(id))throw new Error('Ciclo no grafo.');const next=new Set(stack).add(id);for(const e of graph.edges.filter(e=>e.from===id))visit(e.to,next);};
  for(const id of ids)visit(id,new Set());
 }
 async request(command,p={}){
  if(this.closed)throw new Error('Demonstração encerrada.');
  if(p.expected_revision!==undefined&&p.expected_revision!==this.revision){const e=new Error('Projeto mudou.');e.code='revision_conflict';throw e;}
  switch(command){
   case 'system.hello':return {api:'graph-v2',demo:true};
   case 'plugins.list':return copy(catalog);
   case 'devices.list':return {devices:['capture','render'].map(flow=>({endpoint_id:'demo:'+flow,name:(flow==='capture'?'Microfone':'Fones')+' (demonstração, sem áudio)',flow,is_default:true,state:'active',hardware_kind:'unknown',support:'unknown'}))};
   case 'session.snapshot':return this.snapshot();
   case 'node.add':{
    const defaults={capture:{endpoint_id:null},output:{endpoint_id:null},gain:{gain_db:0,bypass:false},tone:{enabled:true},mixer:{},meter:{}};
    let block={kind:p.kind,...defaults[p.kind]};
    if(p.kind==='plugin'){const plugin=catalog.plugins.find(c=>c.id===p.plugin_id);if(!plugin)throw new Error('Plugin indisponível na demonstração.');block={kind:'plugin',plugin_id:p.plugin_id,source:plugin.source,consumer:plugin.consumer,parameters:{},bypass:false};}
    const graph=copy(this.graph);graph.nodes.push({id:randomUUID(),block});this.checkGraph(graph);this.graph=graph;this.revision++;return this.snapshot();
   }
   case 'graph.apply':this.checkGraph(p.graph);this.graph=copy(p.graph);for(const id of Object.keys(this.states))if(!this.graph.nodes.some(n=>n.id===id))delete this.states[id];this.revision++;return this.snapshot();
   case 'audio.mute':this.muted=!!p.muted;return this.snapshot();
   case 'audio.retry':this.suspended=false;return this.snapshot();
   case 'capture.targets':return {targets:[{token:'demo:browser',process_id:123,executable:'C:\\Demo\\browser.exe',label:'Navegador (demonstração)'}],refreshing:false};
   case 'capture.configure':if(!this.graph.nodes.some(n=>n.id===p.node_id&&n.block.plugin_id==='org.nodivu.windows-audio'))throw new Error('Bloco de captura ausente.');this.states[p.node_id]={capture_selection:p.selection,capture:{status:0,control_error:'Demonstração visual: não captura áudio.'}};this.revision++;return this.snapshot();
   case 'plugin.command':if(p.action!=='load')throw new Error('Reprodução real disponível somente no backend Windows.');this.states[p.node_id]={file:p.path,status:2,position_ms:0,duration_ms:60000};return this.snapshot();
   case 'project.validate':case 'project.open':{
    if(typeof p.contents!=='string'||Buffer.byteLength(p.contents)>65536)throw new Error('Projeto inválido ou maior que 64 KiB.');
    const project=JSON.parse(p.contents);if(project.format!=='nodivu-project'||project.version!==1||!Array.isArray(project.positions)||!Array.isArray(project.resources)||!project.camera)throw new Error('Documento inválido.');this.checkGraph(project.graph);
    if(command==='project.validate')return {project};
    this.graph=copy(project.graph);this.states={};for(const c of project.captures||[])this.states[c.node_id]={capture_selection:c.selection,capture:{status:0,control_error:'Demonstração visual: não captura áudio.'}};this.revision++;this.suspended=true;return {project,snapshot:this.snapshot()};
   }
   default:throw new Error('Comando não implementado na demonstração: '+command);
  }
 }
 async stop(){this.closed=true;}
}
module.exports={DemoEngine};

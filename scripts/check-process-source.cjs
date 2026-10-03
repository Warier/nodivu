// Hardware opt-in. Synthetic processes render to one explicit endpoint; Nodivu
// captures both and sends to a DIFFERENT cable, observed by a second backend.
const assert=require('node:assert/strict');
const path=require('node:path');
const {spawn}=require('node:child_process');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(){
 const [emitId,renderId,captureId]=process.argv.slice(2);
 assert.ok(emitId&&renderId&&captureId&&emitId!==renderId,'Informe IDs explícitos: emissor, saída CABLE Input e recepção CABLE Output.');
 const exe=path.resolve('target/release/nodivu-app-backend.exe');
 const tx=new Engine(exe,{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.resolve('.local/plugins/plugin.json')});
 const rx=new Engine(exe);const children=[];
 try{
  for(const frequency of [997,1733]){const child=spawn(path.resolve('.local/process-capture/process-capture-test.exe'),['--tone',emitId,String(frequency),'90000'],{windowsHide:true,stdio:'ignore'});children.push(child);await new Promise((resolve,reject)=>{child.once('spawn',resolve);child.once('error',reject);});}
  await pause(1000);
  let targets=await tx.request('capture.targets',{refresh:true});
  for(let i=0;targets.refreshing&&i<60;i++){await pause(100);targets=await tx.request('capture.targets');}
  const selected=children.map(c=>targets.targets.find(t=>t.process_id===c.pid));assert.ok(selected.every(Boolean),'Dois processos sintéticos precisam aparecer separadamente.');
  let s=await tx.request('session.snapshot'),r=await rx.request('session.snapshot');
  for(const kind of ['plugin','plugin','mixer','output'])s=await tx.request('node.add',{expected_revision:s.revision,kind,...(kind==='plugin'?{plugin_id:'org.nodivu.windows-audio'}:{})});
  for(const kind of ['capture','meter','output'])r=await rx.request('node.add',{expected_revision:r.revision,kind});
  const [a,b,m,out]=s.graph.nodes.map(n=>n.id);
  s.graph.nodes[3].block.endpoint_id=renderId;
  s.graph.edges=[{from:a,to:m,to_port:0},{from:b,to:m,to_port:1},{from:m,to:out}];
  s=await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  r.graph.nodes[0].block.endpoint_id=captureId;r.graph.nodes[2].block.endpoint_id=renderId;r.graph.edges=[{from:r.graph.nodes[0].id,to:r.graph.nodes[1].id}];
  r=await rx.request('graph.apply',{expected_revision:r.revision,graph:r.graph});
  const configure=(id,t)=>tx.request('capture.configure',{node_id:id,selection:{mode:'application',executable:t.executable},token:t.token});
  await configure(a,selected[0]);await configure(b,selected[1]);
  async function until(predicate,label){const end=Date.now()+9000;do{await pause(100);s=await tx.request('session.snapshot');r=await rx.request('session.snapshot');if(s.last_error||r.last_error)throw new Error(JSON.stringify([s.last_error,r.last_error]));if(predicate())return;}while(Date.now()<end);throw new Error(label+': '+JSON.stringify({states:s.plugin_states,metrics:r.metrics}));}
  await until(()=>[a,b].every(id=>s.plugin_states[id]?.capture.status===2)&&r.metrics?.input_peak>.01,'Capturas não chegaram ao receptor');
  const generation=s.generation;
  console.log(JSON.stringify({phase:'two-captures-to-real-cable',received:r.metrics.input_peak,captures:[a,b].map(id=>s.plugin_states[id].capture)}));
  s.graph.nodes[0].block.bypass=true;s=await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  await until(()=>r.metrics?.input_peak>.01,'Segunda fonte deve continuar com a primeira silenciada');
  s.graph.nodes[1].block.bypass=true;s=await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  await until(()=>r.metrics?.input_peak<.0001,'Silêncio das duas fontes não chegou ao receptor');
  s.graph.nodes[0].block.bypass=false;s=await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  await until(()=>r.metrics?.input_peak>.01,'Fonte não retomou após bypass');
  assert.equal(s.generation,generation,'Edição não deve reabrir saída');
  // A persisted image with two live roots must ask the user, never guess a PID.
  await tx.request('capture.configure',{node_id:a,selection:{mode:'application',executable:selected[0].executable}});
  await until(()=>s.plugin_states[a].capture.status===4&&r.metrics?.input_peak<.0001,'Ambiguidade precisa silenciar');
  await configure(a,selected[0]);await until(()=>s.plugin_states[a].capture.status===2&&r.metrics?.input_peak>.01,'Escolha explícita não retomou');
  s.graph.nodes[3].block.endpoint_id=null;s=await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  await until(()=>s.engine_state==='idle','Saída não encerrou');
  s.graph.nodes[3].block.endpoint_id=renderId;s=await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  await until(()=>s.engine_state==='running'&&s.generation>generation&&r.metrics?.input_peak>.01,'Captura não retomou ao reabrir a saída');
  children[0].kill();
  await until(()=>s.plugin_states[a].capture.status===3&&r.metrics?.input_peak<.0001,'Fim do alvo deve silenciar, sem capturar a outra instância');
  const project={format:'nodivu-project',version:1,graph:s.graph,positions:s.graph.nodes.map((n,i)=>({node_id:n.id,x:i*330,y:0})),camera:{x:0,y:0,scale:1},resources:[],captures:[{node_id:a,selection:{mode:'application',executable:selected[0].executable}}]};
  const validated=await tx.request('project.validate',{contents:JSON.stringify(project),expected_revision:s.revision});assert.deepEqual(validated.project.captures,project.captures);
  project.captures[0].node_id=m;await assert.rejects(tx.request('project.validate',{contents:JSON.stringify(project),expected_revision:s.revision}));
  await tx.request('capture.configure',{node_id:a,selection:null});await tx.request('capture.configure',{node_id:b,selection:null});
  await until(()=>[a,b].every(id=>s.plugin_states[id].capture.status===0),'Clear precisa encerrar os coletores');
  console.log('PASS: duas fontes reais, Mixer, bypass independente, reconexão, ambiguidade, fim de processo e contrato persistente.');
 }finally{for(const child of children)if(child.exitCode===null)child.kill();await tx.stop();await rx.stop();}
}
main().catch(e=>{console.error(e);process.exitCode=1;});

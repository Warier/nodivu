// Opt-in de hardware: saída real sem microfone, tom baixo, ganho/bypass/mudo/fios ao vivo.
const assert=require('node:assert/strict');
const path=require('node:path');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
async function main(){
 const engine=new Engine(path.resolve(__dirname,'../target/release/nodivu-app-backend.exe'));
 let s;
 const read=async()=>s=await engine.request('session.snapshot');
 async function until(test,label){const end=Date.now()+7000;while(Date.now()<end){await new Promise(r=>setTimeout(r,100));await read();if(test(s))return;}throw new Error(label+': '+JSON.stringify(s.last_error));}
 const edit=async fn=>{fn(s.graph);s=await engine.request('graph.apply',{expected_revision:s.revision,graph:s.graph});};
 try{
  const devices=(await engine.request('devices.list')).devices;
  const output=devices.filter(d=>d.flow==='render'&&d.state==='active').sort((a,b)=>Number(b.is_default)-Number(a.is_default))[0];assert.ok(output,'Nenhuma saída ativa');
  s=await read();for(const kind of['tone','gain','output'])s=await engine.request('node.add',{expected_revision:s.revision,kind});
  const [tone,gain,out]=s.graph.nodes.map(n=>n.id);
  await edit(g=>{g.nodes[2].block.endpoint_id=output.endpoint_id;g.edges=[{from:tone,to:gain},{from:gain,to:out}];});
  await until(s=>s.engine_state==='running'&&s.metrics.output_peak>0.01,'Tom sem saída');
  const generation=s.generation,frames=s.metrics.rendered_frames,loud=s.metrics.output_peak;
  assert.equal(s.metrics.captured_frames,0,'Não deveria abrir captura');
  await edit(g=>{g.nodes[1].block.gain_db=-24;});
  await until(s=>s.metrics.output_peak>0&&s.metrics.output_peak<loud/10,'Ganho não reduziu sinal');
  await edit(g=>{g.nodes[1].block.bypass=true;});
  await until(s=>s.metrics.output_peak>loud*.9,'Bypass não restaurou sinal');
  await edit(g=>{g.edges=[];});await until(s=>s.metrics.output_peak===0,'Desconexão não silenciou');
  assert.equal(s.engine_state,'running');assert.equal(s.generation,generation);assert.ok(s.metrics.rendered_frames>frames);
  await edit(g=>{g.edges=[{from:tone,to:out}];});await until(s=>s.metrics.output_peak>0.01,'Reconexão não restaurou');
  s=await engine.request('audio.mute',{muted:true});await until(s=>s.metrics.output_peak===0,'Mudo não silenciou');
  assert.equal(s.generation,generation);
  console.log(JSON.stringify({result:'PASS',output:output.name,generation,peak:loud,captured_frames:s.metrics.captured_frames,rendered_frames:s.metrics.rendered_frames}));
 }finally{await engine.stop();assert.equal(engine.closed,true);}
}
main().catch(e=>{console.error(e);process.exitCode=1;});

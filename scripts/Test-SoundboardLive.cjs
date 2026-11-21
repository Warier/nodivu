// Real backend + CLAP + Windows decode. --output uses only an explicit endpoint.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),{spawn}=require('node:child_process'),{createInterface}=require('node:readline'),{once}=require('node:events');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..'),pause=ms=>new Promise(r=>setTimeout(r,ms));
function wave(seconds,hz){const samples=48000*seconds,b=Buffer.alloc(44+samples*2);b.write('RIFF');b.writeUInt32LE(b.length-8,4);b.write('WAVEfmt ',8);b.writeUInt32LE(16,16);b.writeUInt16LE(1,20);b.writeUInt16LE(1,22);b.writeUInt32LE(48000,24);b.writeUInt32LE(96000,28);b.writeUInt16LE(2,32);b.writeUInt16LE(16,34);b.write('data',36);b.writeUInt32LE(samples*2,40);for(let i=0;i<samples;i++)b.writeInt16LE(Math.round(3000*Math.sin(2*Math.PI*hz*i/48000)),44+i*2);return b;}
(async()=>{
 const folder=await fs.mkdtemp(path.join(root,'.local/soundboard-live-'));for(const [name,seconds,hz]of [['one',10,997],['two',10,1733],['limit',60,997],['over',61,997]])await fs.writeFile(path.join(folder,name+'.wav'),wave(seconds,hz));
 const engine=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_PLUGIN_MANIFEST:path.join(root,'.local/plugins/soundboard/plugin.json')});let receiver;let sequence=0;
 try{
  assert.equal((await engine.request('system.hello')).plugin_host_api,3);
  const list=await engine.request('plugins.list');assert.ok(JSON.stringify(list).includes('"file_bank_slots":10'));
  let s=await engine.request('session.snapshot');s=await engine.request('node.add',{expected_revision:s.revision,kind:'plugin',plugin_id:'org.nodivu.soundboard'});const id=s.graph.nodes.find(n=>n.block.kind==='plugin').id;
  const command=(slot,action,path)=>engine.request('plugin.bank',{node_id:id,slot,action,...(path?{path}:{})});
  async function ready(slot,expected=2){for(let i=0;i<200;i++){s=await engine.request('session.snapshot');const status=s.plugin_states[id].bank[slot].status;if(expected<0?status<0:status===expected)return;await pause(25);}throw Error('Slot status timeout '+JSON.stringify(s.plugin_states[id]));}
  await assert.rejects(command(10,'load',path.join(folder,'one.wav')));
  await assert.rejects(command(0,'load','\\\\server/share/audio.wav'));
  await command(9,'load',path.join(folder,'limit.wav'));await ready(9);assert.equal(s.plugin_states[id].bank[9].duration_ms,60000);
  await command(9,'load',path.join(folder,'over.wav'));await ready(9,-1);await command(9,'clear');
  for(const [slot,name] of [[0,'one'],[1,'two']]){await command(slot,'load',path.join(folder,name+'.wav'));await ready(slot);}
  const outputIndex=process.argv.indexOf('--output'),endpoint=outputIndex<0?null:process.argv[outputIndex+1];
  if(endpoint){
   s=await engine.request('session.snapshot');s=await engine.request('node.add',{expected_revision:s.revision,kind:'output'});const out=s.graph.nodes.find(n=>n.block.kind==='output');out.block.endpoint_id=endpoint;s.graph.edges=[{from:id,from_port:0,to:out.id,to_port:0}];s=await engine.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
   for(let i=0;i<100&&s.engine_state!=='running';i++){await pause(30);s=await engine.request('session.snapshot');}assert.equal(s.engine_state,'running',JSON.stringify(s.last_error));
   const generation=s.generation;
   const actorIndex=process.argv.indexOf('--actor');let lines=[];let read;
   if(actorIndex>=0){receiver=spawn(process.argv[actorIndex+1],['--receiver',String(engine.child.pid)],{windowsHide:true,stdio:['pipe','pipe','pipe']});const rl=createInterface({input:receiver.stdout});rl.on('line',l=>lines.push(JSON.parse(l)));read=async()=>{for(let i=0;i<300&&!lines.length;i++)await pause(10);assert.ok(lines.length,'receiver timeout');return lines.shift();};assert.equal((await read()).ready,true);}
   await command(0,'trigger');await command(1,'trigger');await pause(150);s=await engine.request('session.snapshot');assert.equal(s.plugin_states[id].bank[0].status,3);assert.equal(s.plugin_states[id].bank[1].status,3);assert.ok(s.metrics.output_peak>0.05);
   const evidence={generation,peak:s.metrics.output_peak};
   if(read){receiver.stdin.write('sample\n');evidence.mixed=await read();assert.ok(evidence.mixed.a997>0.04&&evidence.mixed.b1733>0.04);assert.equal(evidence.mixed.nonfinite,0);}
   await command(0,'stop');await pause(100);s=await engine.request('session.snapshot');assert.equal(s.plugin_states[id].bank[0].status,5);assert.equal(s.plugin_states[id].bank[1].status,3);assert.equal(s.generation,generation);
   if(read){receiver.stdin.write('sample\n');evidence.stopped=await read();assert.ok(evidence.stopped.a997<0.002&&evidence.stopped.b1733>0.04);}
   console.log(JSON.stringify(evidence));await command(1,'stop');
  }
  s=await engine.request('session.snapshot');const doc={format:'nodivu-project',version:1,graph:s.graph,positions:s.graph.nodes.map(n=>({node_id:n.id,x:0,y:0})),camera:{x:0,y:0,scale:1},resources:[{node_id:id,slot:0,path:path.join(folder,'one.wav')},{node_id:id,slot:1,path:path.join(folder,'two.wav')}]};
  const checked=await engine.request('project.validate',{contents:JSON.stringify(doc),expected_revision:s.revision});assert.equal(checked.project.resources.length,2);
  await command(0,'clear');s=await engine.request('session.snapshot');assert.equal(s.plugin_states[id].bank[0].file,'');assert.equal(s.plugin_states[id].bank[0].assigned,true);
  console.log('PASS: real CLAP, slots, 60/61 second limit, project contract'+(endpoint?', WASAPI two voices + independent stop':' (no hardware requested)'));
 }finally{if(receiver){receiver.kill();await once(receiver,'close');}await engine.stop();}
})().catch(e=>{console.error(e);process.exitCode=1;});

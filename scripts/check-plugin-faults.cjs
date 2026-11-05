// Opt-in native WASAPI regression: real CLAP error/NaN alongside a healthy source.
// The output is attenuated by 60 dB; no microphone or system defaults are changed.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..'),stage=path.join(root,'.local/fault-test'),wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 await fs.mkdir(stage,{recursive:true});
 for(const kind of ['source','process-fail','nan']){
  const dir=path.join(stage,kind);await fs.mkdir(dir,{recursive:true});
  await fs.copyFile(path.join(root,'.local/clap-fixture/nodivu-fixture.clap'),path.join(dir,'fixture.clap'));
  await fs.writeFile(path.join(dir,'plugin.json'),JSON.stringify({schema_version:1,library:'fixture.clap',plugin_id:'org.nodivu.fixture.'+kind,title:kind,description:'Deliberate test fixture',accent:'#e6aa68',layout:'stack',controls:[]}));
 }
 const e=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(stage,'plugin.json')});
 e.on('fault',m=>console.error(m));
 try{
  let s=await e.request('session.snapshot');
  const add=async(kind,plugin_id)=>{s=await e.request('node.add',{expected_revision:s.revision,kind,...(plugin_id?{plugin_id}:{})});return s.graph.nodes.at(-1).id;};
  const source=await add('plugin','org.nodivu.fixture.source'),bad=await add('plugin','org.nodivu.fixture.process-fail'),nan=await add('plugin','org.nodivu.fixture.nan'),mixer=await add('mixer'),gain=await add('gain'),out=await add('output');
  const device=(await e.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(device,'active default output required');
  s.graph.nodes.find(n=>n.id===out).block.endpoint_id=device.endpoint_id;
  s.graph.nodes.find(n=>n.id===gain).block.gain_db=-60;
  const wire=(from,to,to_port=0)=>({from,to,from_port:0,to_port});
  s.graph.edges=[wire(source,mixer),wire(source,bad),wire(bad,mixer,1),wire(source,nan),wire(nan,mixer,2),wire(mixer,gain),wire(gain,out)];
  // Diagnostic seqlocks deliberately omit a node when its writer overlaps the
  // reader. Never interpret an absent meter as silence or as a successful test.
  const completeMetrics=()=>[source,bad,nan,mixer].every(id=>s.metrics?.route_nodes.some(n=>n.node_id===id));
  const until=async predicate=>{for(let i=0;i<100;i++){await wait(50);s=await e.request('session.snapshot');if(predicate())return;}throw Error('No complete observation within 5s: '+JSON.stringify({metrics:s.metrics,plugins:s.plugin_states}));};
  s=await e.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  await until(()=>s.plugin_states[bad].fault&&s.plugin_states[nan].fault&&s.metrics?.rendered_frames>4800&&completeMetrics());
  assert.equal(s.engine_state,'running');assert.equal(s.plugin_states[bad].fault.kind,1);assert.equal(s.plugin_states[nan].fault.kind,2);
  const generation=s.generation;
  const peak=id=>s.metrics.route_nodes.find(n=>n.node_id===id)?.peak;
  assert.ok(peak(source)>0.1,JSON.stringify({metrics:s.metrics,plugins:s.plugin_states}));assert.equal(peak(bad),0);assert.equal(peak(nan),0);assert.ok(Math.abs(peak(mixer)-peak(source))<0.001);
  s.graph.nodes.find(n=>n.id===bad).block.bypass=true;s.graph.nodes.find(n=>n.id===nan).block.bypass=true;
  s=await e.request('graph.apply',{expected_revision:s.revision,graph:s.graph});await wait(350);
  for(let sample=0;sample<5;sample++){
   await until(completeMetrics);
   assert.equal(s.generation,generation);assert.equal(s.engine_state,'running');assert.ok(peak(source)>0.1);assert.equal(peak(bad),0);assert.equal(peak(nan),0);assert.ok(Math.abs(peak(mixer)-peak(source))<0.001);
  }
  s.graph.nodes=s.graph.nodes.filter(n=>n.id!==bad);s.graph.edges=s.graph.edges.filter(w=>w.from!==bad&&w.to!==bad);
  s=await e.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  const replacement=await add('plugin','org.nodivu.fixture.process-fail');assert.equal(s.plugin_states[replacement].fault,null);
  console.log(JSON.stringify({status:'PASS',realClap:true,processErrorAndNaN:'silenced',healthyBranch:'preserved',bypass:'cannot expose dry',generation,replacement:'healthy slot',outputAttenuationDb:-60}));
 }finally{await e.stop();}
})().catch(error=>{console.error(error);process.exitCode=1;});

// Opt-in WASAPI regression. Only synthetic tone, attenuated 60 dB at the output.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..'),stage=path.join(root,'.local/worker-fault-test'),wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 await fs.mkdir(stage,{recursive:true});const dir=path.join(stage,'worker');await fs.mkdir(dir,{recursive:true});
 const manifest=JSON.parse(await fs.readFile(path.join(root,'examples/python-worker/worker.json'),'utf8'));
 manifest.python=process.argv[2]||'python';
 await fs.writeFile(path.join(dir,'worker.json'),JSON.stringify(manifest));
 await fs.copyFile(path.join(root,'examples/python-worker/dsp.py'),path.join(dir,'dsp.py'));
 await fs.writeFile(path.join(dir,'worker.py'),'raise RuntimeError("deliberate audit test failure")\n');
 const e=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(stage,'plugin.json')});
 e.on('fault',m=>console.error(m));
 try{
  let s=await e.request('session.snapshot');
  const add=async(kind,plugin_id)=>{s=await e.request('node.add',{expected_revision:s.revision,kind,...(plugin_id?{plugin_id}:{})});return s.graph.nodes.at(-1).id;};
  const tone=await add('tone'),worker=await add('plugin',manifest.plugin_id),mix=await add('mixer'),gain=await add('gain'),out=await add('output');
  const device=(await e.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(device);
  s.graph.nodes.find(n=>n.id===out).block.endpoint_id=device.endpoint_id;
  s.graph.nodes.find(n=>n.id===tone).block.enabled=true;s.graph.nodes.find(n=>n.id===gain).block.gain_db=-60;
  const wire=(from,to,to_port=0)=>({from,to,from_port:0,to_port});
  s.graph.edges=[wire(tone,worker),wire(worker,mix,1),wire(tone,mix),wire(mix,gain),wire(gain,out)];
  s=await e.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  const until=async predicate=>{for(let i=0;i<100;i++){await wait(50);s=await e.request('session.snapshot');if(predicate(s))return;}throw Error(JSON.stringify(s.plugin_states));};
  await until(s=>s.plugin_states[worker].status===3);const generation=s.generation;
  s.graph.nodes.find(n=>n.id===worker).block.bypass=true;
  s=await e.request('graph.apply',{expected_revision:s.revision,graph:s.graph});await wait(350);
  await until(s=>[worker,mix].every(id=>s.metrics?.route_nodes.some(n=>n.node_id===id)));
  const peak=id=>s.metrics.route_nodes.find(n=>n.node_id===id)?.peak;
  assert.equal(peak(worker),0);assert.ok(peak(mix)>0.01);assert.equal(s.engine_state,'running');
  await fs.copyFile(path.join(root,'examples/python-worker/worker.py'),path.join(dir,'worker.py'));
  await wait(200);s=await e.request('session.snapshot');assert.equal(s.plugin_states[worker].status,3,'repair alone must not restart failed code');
  await e.request('plugin.command',{node_id:worker,action:'restart'});
  await until(s=>s.plugin_states[worker].status===2&&s.metrics.route_nodes.find(n=>n.node_id===worker)?.peak>0.01);
  assert.equal(s.generation,generation);
  console.log(JSON.stringify({status:'PASS',failedBypass:'silent',healthyBranch:'audible',recovery:'explicit restart',sameAudioGeneration:true}));
 }finally{await e.stop();}
})().catch(error=>{console.error(error);process.exitCode=1;});

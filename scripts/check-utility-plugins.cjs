const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs/promises');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..'),wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 const folder=await fs.mkdtemp(path.join(root,'.local/utility-live-'));
 for(const name of ['ducking','talk-button'])await fs.cp(path.join(root,'../nodivu-'+name+'/dist/package'),path.join(folder,name),{recursive:true});
 const engine=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(folder,'plugin.json')});
 try{
  const hello=await engine.request('system.hello');assert.equal(hello.plugin_host_api,2);
  const catalog=await engine.request('plugins.list');assert.deepEqual(catalog.plugins.find(p=>p.id==='org.nodivu.ducking').inputs,[0,1]);
  let s=await engine.request('session.snapshot');
  const add=async(kind,plugin_id)=>{s=await engine.request('node.add',{expected_revision:s.revision,kind,...(plugin_id?{plugin_id}:{})});return s.graph.nodes.at(-1).id;};
  const tone=await add('tone'),gain=await add('gain'),duck=await add('plugin','org.nodivu.ducking'),talk=await add('plugin','org.nodivu.talk-button'),out=await add('output');
  const endpoint=(await engine.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(endpoint);
  const edge=(from,to,to_port=0)=>({from,to,from_port:0,to_port});
  const edit=async fn=>{const graph=structuredClone(s.graph);fn(graph);s=await engine.request('graph.apply',{graph,expected_revision:s.revision});};
  await edit(g=>{g.nodes.find(n=>n.id===gain).block.gain_db=-18;g.nodes.find(n=>n.id===out).block.endpoint_id=endpoint.endpoint_id;g.edges=[edge(tone,gain),edge(gain,duck),edge(tone,duck,1),edge(duck,talk),edge(talk,out)];});
  const generation=s.generation,observations=[];
  async function observe(check){for(let i=0;i<30;i++){await wait(70);s=await engine.request('session.snapshot');if(s.engine_state==='running'&&s.metrics?.processor_errors===0&&check(s)){assert.equal(s.generation,generation);observations.push(s);return;}}throw Error(JSON.stringify(s));}
  await observe(s=>s.metrics.output_peak<1e-6);
  await assert.rejects(engine.request('plugin.gesture',{node_id:duck,parameter:0,pressed:true}),/momentâneo/);
  const rev=s.revision;
  for(let i=0;i<8;i++){await engine.request('plugin.gesture',{node_id:talk,parameter:2,pressed:true});await wait(60);}
  s=await engine.request('session.snapshot');assert.equal(s.revision,rev);assert.equal(s.graph.nodes.find(n=>n.id===talk).block.parameters['2'],0);assert.ok(s.metrics.output_peak>0.0001);observations.push(s);
  // No release: simulate a renderer that stopped sending heartbeats; backend must close it.
  await observe(s=>s.metrics.output_peak<1e-6);
  await edit(g=>{g.nodes.find(n=>n.id===talk).block.parameters['0']=0;});
  const expected=10**(-24/20)*10**(-18/20)*10**(-18/20);
  await observe(s=>Math.abs(s.metrics.output_peak-expected)<0.00003);
  await edit(g=>{g.edges=g.edges.filter(e=>!(e.to===duck&&e.to_port===1));});
  await observe(s=>s.metrics.output_peak>expected*3);
  const graph=structuredClone(s.graph);graph.nodes.find(n=>n.id===talk).block.parameters['2']=1;
  await assert.rejects(engine.request('graph.apply',{expected_revision:s.revision,graph}),/salvo/);
  await fs.writeFile(path.join(root,'.local/utility-live-evidence.json'),JSON.stringify({generation,catalog,observations},null,2));
  console.log('PASS: WASAPI sidechain, live gain, transient gesture, watchdog, unpersisted hold and unchanged audio generation');
 }finally{await engine.stop();}
})().catch(e=>{console.error(e);process.exitCode=1;});

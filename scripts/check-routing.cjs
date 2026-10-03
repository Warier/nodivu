// Real WASAPI integration: branching, native + Python latency alignment and live edits.
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..');
const engine=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(root,'.local/plugins/plugin.json')});
const wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 let s=await engine.request('session.snapshot');
 assert.equal((await engine.request('system.hello')).api,'graph-v2');
 const add=async(kind,plugin_id)=>{s=await engine.request('node.add',{expected_revision:s.revision,kind,...(plugin_id?{plugin_id}:{})});return s.graph.nodes.at(-1).id;};
 const edit=async fn=>{const graph=structuredClone(s.graph);fn(graph);s=await engine.request('graph.apply',{expected_revision:s.revision,graph});};
 const tone=await add('tone'),gain=await add('gain'),worker=await add('plugin','org.nodivu.python.gain'),mix=await add('mixer'),meter=await add('meter'),out=await add('output'),consumer=await add('plugin','org.nodivu.fixture.consumer');
 const endpoint=(await engine.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(endpoint);
 const edge=(from,to,to_port=0)=>({from,to,from_port:0,to_port});
 await edit(g=>{g.nodes.find(n=>n.id===out).block.endpoint_id=endpoint.endpoint_id;g.nodes.find(n=>n.id===gain).block.gain_db=20*Math.log10(0.5);g.edges=[edge(tone,gain),edge(gain,mix),edge(tone,worker),edge(worker,mix,1),edge(tone,meter),edge(tone,consumer),edge(mix,out)];});
 assert.equal(s.graph.nodes.find(n=>n.id===consumer).block.consumer,true);
 const generation=s.generation,observations=[];
 async function settled(multiplier,delay){
  for(let i=0;i<100;i++){
   await wait(100);s=await engine.request('session.snapshot');assert.equal(s.last_error,null);assert.notEqual(s.plugin_states[worker]?.status,3,JSON.stringify(s.plugin_states));
   if(Math.abs(s.metrics.output_peak-10**(-36/20)*multiplier)<0.000025&&s.metrics.compensation_frames===delay){
    assert.equal(s.generation,generation);assert.equal(s.metrics.processor_errors,0);
    const nodes=s.metrics.route_nodes;const source=nodes.find(n=>n.node_id===tone),consumer=nodes.find(n=>n.node_id===meter);
    assert.ok(source&&consumer);assert.equal(source.calls,consumer.calls);assert.ok(Math.abs(consumer.peak-10**(-24/20))<0.00002);observations.push(s);return;
   }
  }throw new Error('Mistura nao estabilizou: '+JSON.stringify(s));
 }
 await settled(1.5,960);
 assert.ok(s.metrics.route_nodes.find(n=>n.node_id===consumer)?.calls>0);
 await edit(g=>g.nodes.find(n=>n.id===consumer).block.bypass=true);await settled(1.5,960);
 await edit(g=>g.nodes.find(n=>n.id===consumer).block.bypass=false);await settled(1.5,960);
 await edit(g=>g.nodes.find(n=>n.id===gain).block.gain_db=20*Math.log10(2));await settled(3,960);
 await edit(g=>g.nodes.find(n=>n.id===gain).block.bypass=true);await settled(2,960);
 // Invalid fan-in and cycles must preserve the accepted document and running audio.
 for(const bad of[edge(tone,mix),edge(mix,gain)]){
  const graph=structuredClone(s.graph);if(bad.to===gain)graph.edges=graph.edges.filter(e=>e.to!==gain);graph.edges.push(bad);
  await assert.rejects(engine.request('graph.apply',{expected_revision:s.revision,graph}));
  assert.equal((await engine.request('session.snapshot')).revision,s.revision);
 }
 await edit(g=>g.edges=g.edges.filter(e=>!(e.from===worker&&e.to===mix)));await settled(1,0);
 await edit(g=>g.edges.push(edge(worker,mix,1)));await settled(2,960);
 // Removing a source branch must not remove the other wires leaving the same source.
 await edit(g=>g.edges=g.edges.filter(e=>!(e.from===tone&&e.to===gain)));await settled(1,960);
 fs.writeFileSync(path.join(root,'.local/routing-live-evidence.json'),JSON.stringify({endpoint,observations,final:s},null,2));
 console.log('PASS: WASAPI DAG, fanout sem duplicar fonte, Mixer, medidor consumidor, Python + nativo alinhados, ganho/bypass, rejeicoes e reconexoes sem reopen.');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>engine.stop());

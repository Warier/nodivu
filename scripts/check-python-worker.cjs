// Hardware gate: real Python -> same WASAPI output as native plugins. No user files.
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..'),pid='org.nodivu.python.gain';
const engine=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(root,'.local/plugins/plugin.json')});
const wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 const catalog=await engine.request('plugins.list');assert.ok(catalog.plugins.find(p=>p.id===pid&&p.worker&&p.latency_frames===960));
 let s=await engine.request('session.snapshot');
 const add=async(kind,plugin_id)=>{s=await engine.request('node.add',{expected_revision:s.revision,kind,...(plugin_id?{plugin_id}:{})});return s.graph.nodes.at(-1).id;};
 const edit=async fn=>{const graph=structuredClone(s.graph);fn(graph);s=await engine.request('graph.apply',{expected_revision:s.revision,graph});};
 const tone=await add('tone'),a=await add('plugin',pid),gain=await add('gain'),b=await add('plugin',pid),out=await add('output');
 const endpoint=(await engine.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(endpoint);
 await edit(g=>{g.nodes.find(n=>n.id===out).block.endpoint_id=endpoint.endpoint_id;g.nodes.find(n=>n.id===gain).block.gain_db=0;g.edges=[{from:tone,to:a},{from:a,to:gain},{from:gain,to:b},{from:b,to:out}];});
 const generation=s.generation,observations=[];
 async function settled(expected){
  for(let i=0;i<100;i++){
   await wait(100);s=await engine.request('session.snapshot');assert.equal(s.last_error,null);
   for(const id of[a,b])assert.notEqual(s.plugin_states[id].status,3,JSON.stringify(s.plugin_states));
   if(s.plugin_states[a].completed_blocks>10&&s.plugin_states[b].completed_blocks>10&&Math.abs(s.metrics.output_peak-expected)<0.00002){
    assert.equal(s.generation,generation);assert.equal(s.metrics.processor_errors,0);observations.push(s);return;
   }
  }throw new Error('Sinal não estabilizou: '+JSON.stringify(s));
 }
 // Tone -24 dB, master -12 dB; compare the actual contracted amplitude.
 const reference=10**(-36/20);
 await settled(reference);
 await edit(g=>{for(const id of[a,b])g.nodes.find(n=>n.id===id).block.parameters['7']=0.5;});await settled(reference*0.25);
 await edit(g=>g.nodes.find(n=>n.id===a).block.bypass=true);await settled(reference*0.5);
 await engine.request('plugin.command',{node_id:a,action:'restart'});await wait(600);await settled(reference*0.5);
 // Remove/recreate one node. No stream restart; source -> new UUID -> downstream still independent.
 await edit(g=>{g.nodes=g.nodes.filter(n=>n.id!==a);g.edges=g.edges.filter(e=>e.from!==a&&e.to!==a);});
 const replacement=await add('plugin',pid);
 await edit(g=>g.edges.push({from:tone,to:replacement},{from:replacement,to:gain}));
 for(let i=0;i<80;i++){await wait(100);s=await engine.request('session.snapshot');if(s.plugin_states[replacement].completed_blocks>5&&s.metrics.output_peak>0.0001)break;}
 assert.equal(s.generation,generation);assert.ok(s.metrics.output_peak>0.0001);assert.equal(s.plugin_states[a],undefined);
 fs.writeFileSync(path.join(root,'.local/python-worker-evidence.json'),JSON.stringify({endpoint,observations,final:s},null,2));
 console.log('PASS: dois workers Python reais em cadeia WASAPI, controles isolados, ganho composto, bypass alinhado, restart e novo UUID sem reopen.');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>engine.stop());

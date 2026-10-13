// Opt-in real WASAPI: quiet tone, native latency compensation and cleanup controls.
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs/promises');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..');
const wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 const dir=await fs.mkdtemp(path.join(root,'.local/cleanup-live-'));
 for(const name of ['voice-cleanup','peak-limiter'])await fs.cp(path.join(root,'.local/plugins',name),path.join(dir,name),{recursive:true});
 const engine=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(dir,'plugin.json')});
 try{
  const catalog=await engine.request('plugins.list');
  assert.equal(catalog.plugins.find(p=>p.id==='org.nodivu.voice-cleanup')?.latency_frames,1440);
  assert.equal(catalog.plugins.find(p=>p.id==='org.nodivu.peak-limiter')?.latency_frames,240);
  let s=await engine.request('session.snapshot');
  const add=async(kind,plugin_id)=>{s=await engine.request('node.add',{expected_revision:s.revision,kind,...(plugin_id?{plugin_id}:{})});return s.graph.nodes.at(-1).id;};
  const edit=async fn=>{const graph=structuredClone(s.graph);fn(graph);s=await engine.request('graph.apply',{expected_revision:s.revision,graph});};
  const tone=await add('tone'),gain=await add('gain'),voice=await add('plugin','org.nodivu.voice-cleanup'),limiter=await add('plugin','org.nodivu.peak-limiter'),mixer=await add('mixer'),out=await add('output');
  const endpoint=(await engine.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(endpoint,'Saída padrão ativa necessária');
  const edge=(from,to,to_port=0)=>({from,to,from_port:0,to_port});
  await edit(g=>{g.nodes.find(n=>n.id===out).block.endpoint_id=endpoint.endpoint_id;g.nodes.find(n=>n.id===gain).block.gain_db=-24;g.nodes.find(n=>n.id===voice).block.parameters['0']=0;g.edges=[edge(tone,gain),edge(gain,mixer),edge(gain,voice),edge(voice,limiter),edge(limiter,mixer,1),edge(mixer,out)];});
  const generation=s.generation,observations=[];
  async function settle(check){for(let i=0;i<40;i++){await wait(100);s=await engine.request('session.snapshot');if(s.engine_state==='running'&&s.metrics?.processor_errors===0&&check(s)){assert.equal(s.generation,generation);assert.equal(s.last_error,null);observations.push(s);return;}}throw new Error(JSON.stringify(s));}
  const expected=2*10**(-48/20);
  await settle(s=>s.metrics.compensation_frames===1680&&Math.abs(s.metrics.output_peak-expected)<0.00002);
  await edit(g=>g.nodes.find(n=>n.id===voice).block.bypass=true);
  await settle(s=>Math.abs(s.metrics.output_peak-expected)<0.00002);
  await edit(g=>{g.nodes.find(n=>n.id===voice).block.bypass=false;g.nodes.find(n=>n.id===voice).block.parameters['0']=100;});
  await settle(s=>s.metrics.route_nodes.find(n=>n.node_id===voice)?.calls>0);
  await edit(g=>g.nodes.find(n=>n.id===limiter).block.parameters['0']=-12);
  await settle(s=>s.metrics.processor_errors===0);
  await fs.writeFile(path.join(root,'.local/cleanup-live-evidence.json'),JSON.stringify({catalog,generation,observations},null,2));
  console.log('PASS: catálogo pós-activate; RNNoise/limiter reais; Mixer 1680 frames alinhado; bypass, parâmetros e geração WASAPI preservados.');
 }finally{await engine.stop();}
})().catch(e=>{console.error(e);process.exitCode=1;});

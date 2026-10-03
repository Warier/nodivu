// Opt-in WASAPI test, synthetic fixtures from Test-MediaPlayer.ps1 only.
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),wait=ms=>new Promise(r=>setTimeout(r,ms));
const e=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(root,'.local/plugins/plugin.json')});
(async()=>{
 let s=await e.request('session.snapshot');
 const add=async(kind,plugin_id)=>{s=await e.request('node.add',{expected_revision:s.revision,kind,...(plugin_id?{plugin_id}:{})});return s.graph.nodes.at(-1).id;};
 const player=await add('plugin','org.nodivu.mp3-player'),output=await add('output');
 const catalog=await e.request('plugins.list');assert.ok(catalog.plugins.find(p=>p.id==='org.nodivu.mp3-player').file_transport);
 const device=(await e.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(device);
 s.graph.nodes.find(n=>n.id===output).block.endpoint_id=device.endpoint_id;s.graph.edges=[{from:player,to:output}];
 s=await e.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
 const until=async pred=>{for(let i=0;i<100;i++){await wait(50);s=await e.request('session.snapshot');if(pred(s))return;}throw new Error(JSON.stringify(s.plugin_states));};
 const cmd=(action,extra={})=>e.request('plugin.command',{node_id:player,action,...extra});
 await until(s=>s.engine_state==='running');const generation=s.generation;
 for(const format of ['wav','mp3','m4a']){
  await cmd('load',{path:path.join(root,'.local/media-test/short.'+format)});
  await until(s=>s.plugin_states[player].status===2);const duration=s.plugin_states[player].duration_ms;assert.ok(duration>=2900&&duration<=3100);
  await assert.rejects(cmd('seek',{position_ms:duration+1}));await assert.rejects(cmd('seek',{position_ms:-1}));await assert.rejects(cmd('pause',{position_ms:0}));
  await cmd('play');await until(s=>s.plugin_states[player].position_ms>350&&s.metrics.output_peak>0.05);
  const source=s.metrics.route_nodes.find(n=>n.node_id===player).peak,ratio=s.metrics.output_peak/source;
  assert.ok(ratio>0.97&&ratio<1.03,'Hidden attenuation: '+ratio);
  await cmd('pause');await until(s=>s.plugin_states[player].status===5&&s.metrics.output_peak===0);
  const frozen=s.plugin_states[player].position_ms;await wait(200);s=await e.request('session.snapshot');assert.equal(s.plugin_states[player].position_ms,frozen);
  await cmd('seek',{position_ms:1000});await until(s=>s.plugin_states[player].position_ms===1000);assert.equal(s.plugin_states[player].status,5);
  await cmd('play');await until(s=>s.plugin_states[player].position_ms>1100&&s.metrics.output_peak>0.05);
  await cmd('seek',{position_ms:2500});await until(s=>s.plugin_states[player].position_ms>=2500);await until(s=>s.plugin_states[player].status===4);
  assert.equal(s.generation,generation);assert.equal(s.metrics.processor_errors,0);
  console.log(`PASS ${format}: duration=${duration}ms, output/source=${ratio.toFixed(5)}, pause/seek/resume/EOF; generation=${generation}`);
 }
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(()=>e.stop());

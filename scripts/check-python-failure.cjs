// Isolated test package: modifies its own copy, never the installed/user worker.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..'),stage=fs.mkdtempSync(path.join(root,'.local/worker-failure-'));
// No root CLAP manifest/library: exercise Python-only discovery and audio ownership.
fs.mkdirSync(path.join(stage,'worker'));
const definition=JSON.parse(fs.readFileSync(path.join(root,'.local/plugins/python-gain/worker.json'),'utf8'));
fs.writeFileSync(path.join(stage,'worker/worker.json'),JSON.stringify(definition));
fs.copyFileSync(path.join(root,'examples/python-worker/worker.py'),path.join(stage,'worker/worker.py'));
// Gain 3 blocks the process; 2.5 returns non-finite DSP. Both are test-only code.
fs.writeFileSync(path.join(stage,'worker/dsp.py'),`import time, os, sys, json, subprocess
from pathlib import Path
class Processor:
 def __init__(self):
  child=subprocess.Popen([sys.executable,'-I','-c','import time; time.sleep(30)'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
  Path(__file__).with_name('pids.json').write_text(json.dumps([os.getpid(),child.pid]))
 def reset(self): pass
 def process(self,mode,incoming,outgoing,frames,gain):
  if gain==3: time.sleep(30)
  for i in range(frames):
   outgoing[i]=float('nan') if gain==2.5 else incoming[i]*gain
   outgoing[480+i]=incoming[480+i]*gain
  return frames,False
`);
const engine=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(stage,'plugin.json')});
const wait=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 let s=await engine.request('session.snapshot');
 const add=async(kind)=>{s=await engine.request('node.add',{expected_revision:s.revision,kind,...(kind==='plugin'?{plugin_id:definition.plugin_id}:{})});return s.graph.nodes.at(-1).id;};
 const tone=await add('tone'),worker=await add('plugin'),output=await add('output');
 const edit=async fn=>{const graph=structuredClone(s.graph);fn(graph);s=await engine.request('graph.apply',{expected_revision:s.revision,graph});};
 const endpoint=(await engine.request('devices.list')).devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);assert.ok(endpoint);
 await edit(g=>{g.nodes.find(n=>n.id===output).block.endpoint_id=endpoint.endpoint_id;g.edges=[{from:tone,to:worker},{from:worker,to:output}];});
 const generation=s.generation;let maxRequestMs=0,started=false;
 async function until(predicate){for(let i=0;i<60;i++){await wait(100);const start=performance.now();s=await engine.request('session.snapshot');maxRequestMs=Math.max(maxRequestMs,performance.now()-start);assert.equal(s.generation,generation);if(started)assert.equal(s.engine_state,'running');else{assert.ok(['starting','running'].includes(s.engine_state));started=s.engine_state==='running';}assert.equal(s.last_error,null);if(started&&predicate())return;}throw new Error('Prazo: '+JSON.stringify(s.plugin_states));}
 await until(()=>s.plugin_states[worker].status===2&&s.metrics.output_peak>0.01);
 const evidence=[];
 for(const faultGain of[3,2.5]){
  await edit(g=>g.nodes.find(n=>n.id===worker).block.parameters['7']=faultGain);
  await until(()=>s.plugin_states[worker].status===3&&s.metrics.output_peak===0);
  assert.ok(s.plugin_states[worker].error);const before=s.metrics.rendered_frames;await wait(200);s=await engine.request('session.snapshot');assert.ok(s.metrics.rendered_frames>before);
  evidence.push(s.plugin_states[worker]);
  await edit(g=>g.nodes.find(n=>n.id===worker).block.parameters['7']=1);
  await wait(150);s=await engine.request('session.snapshot');assert.equal(s.plugin_states[worker].status,3,'Não reiniciar silenciosamente');
  await engine.request('plugin.command',{node_id:worker,action:'restart'});
  await until(()=>s.plugin_states[worker].status===2&&s.metrics.output_peak>0.01);
 }
 assert.ok(maxRequestMs<1000,'API bloqueou esperando worker');
 const pids=JSON.parse(fs.readFileSync(path.join(stage,'worker/pids.json'),'utf8'));
 for(const pid of pids)process.kill(pid,0); // Verify that the processes actually existed.
 engine.closing=true;engine.child.kill();await engine.exited;
 const alive=pid=>{try{process.kill(pid,0);return true;}catch(e){if(e.code==='ESRCH')return false;throw e;}};
 for(let i=0;i<50&&pids.some(alive);i++)await wait(20);
 assert.ok(pids.every(pid=>!alive(pid)),'Python ou descendente sobreviveu ao backend');
 fs.writeFileSync(path.join(root,'.local/python-worker-failure-evidence.json'),JSON.stringify({stage,maxRequestMs,evidence,snapshot:s},null,2));
 console.log('PASS: stall/timeout e NaN silenciam só o worker; WASAPI/API continuam; restart recupera; matar backend encerra Python e descendente.');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>engine.stop());

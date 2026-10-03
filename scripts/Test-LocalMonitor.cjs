// Opt-in real WASAPI test: primary render, monitor render and its paired capture.
// Pass opaque IDs explicitly. Receiver writes silence; no microphone feedback.
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const path=require('node:path'),assert=require('node:assert/strict');
const [primary,monitor,capture]=process.argv.slice(2);
if(!primary||!monitor||!capture||primary===monitor)throw new Error('Uso: node scripts/Test-LocalMonitor.cjs <primary-render-id> <monitor-render-id> <monitor-capture-id>');
const exe=path.resolve(__dirname,'../target/release/nodivu-app-backend.exe');
const tx=new Engine(exe),rx=new Engine(exe);const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function sample(){let sent=0,received=0,s;for(let n=0;n<15;n++){await delay(100);s=await tx.request('session.snapshot');const r=await rx.request('session.snapshot');assert.equal(s.last_error,null);assert.equal(r.last_error,null);assert.equal(r.capture_error,null);sent=Math.max(sent,s.metrics?.output_peak||0);received=Math.max(received,r.metrics?.input_peak||0);}return {sent,received,s};}
(async()=>{try{
 let r=await rx.request('session.snapshot');for(const kind of ['capture','output'])r=await rx.request('node.add',{expected_revision:r.revision,kind});r.graph.nodes[0].block.endpoint_id=capture;r.graph.nodes[1].block.endpoint_id=monitor;
 await rx.request('graph.apply',{expected_revision:r.revision,graph:r.graph});
 let s=await tx.request('session.snapshot');for(const kind of ['tone','gain','output'])s=await tx.request('node.add',{expected_revision:s.revision,kind});
 s.graph.nodes[1].block.gain_db=-20;s.graph.nodes[2].block.endpoint_id=primary;
 s.graph.edges=[{from:s.graph.nodes[0].id,to:s.graph.nodes[1].id},{from:s.graph.nodes[1].id,to:s.graph.nodes[2].id}];
 s=await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});const generation=s.generation,revision=s.revision;
 await assert.rejects(tx.request('audio.monitor',{endpoint_id:primary}));
 const baseline=await sample();assert.ok(baseline.received<0.00001,'monitor endpoint has concurrent audio; isolate the test first');
 await tx.request('audio.monitor',{endpoint_id:monitor});
 const active=await sample();assert.equal(active.s.monitor.runtime.state,'running');assert.ok(active.sent>0.001);assert.ok(Math.abs(active.received/active.sent-1)<0.05,'monitor preserves final amplitude');
 s=active.s;s.graph.nodes[1].block.gain_db=-26.0206;await tx.request('graph.apply',{expected_revision:s.revision,graph:s.graph});await delay(400);const half=await sample();assert.ok(Math.abs(half.received/active.received-0.5)<0.03,'gain in monitored copy');
 await tx.request('audio.mute',{muted:true});await delay(400);const muted=await sample();assert.ok(muted.received<0.00001,'master mute in monitored copy');
 await tx.request('audio.mute',{muted:false});await tx.request('audio.monitor',{endpoint_id:null});await delay(400);const off=await sample();assert.ok(off.sent>0.001);assert.ok(off.received<0.00001);assert.equal(off.s.generation,generation,'monitor never reopens primary');assert.equal(off.s.revision,revision+1,'monitor never edits document');
 for(let i=0;i<3;i++){await tx.request('audio.monitor',{endpoint_id:monitor});await delay(80);await tx.request('audio.monitor',{endpoint_id:null});}
 console.log(JSON.stringify({result:'PASS',active:{sent:active.sent,received:active.received,runtime:active.s.monitor.runtime},half:half.received,muted:muted.received,off:off.received,generation},null,2));
 }finally{await tx.stop();await rx.stop();}})().catch(e=>{console.error(e);process.exitCode=1;});

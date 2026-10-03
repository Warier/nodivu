const assert=require('node:assert/strict');
module.exports=async win=>{
 const primary=process.env.NODIVU_TEST_PRIMARY,monitor=process.env.NODIVU_TEST_MONITOR;
 if(!primary||!monitor||primary===monitor)throw new Error('Ensaio de escuta exige IDs explícitos distintos.');
 const run=s=>win.webContents.executeJavaScript(s),pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function wait(test){for(let i=0;i<100;i++){if(await run(test))return;await pause(50);}throw new Error('Monitor smoke: '+await run('document.getElementById("monitor-status").textContent'));}
 async function select(selector,value){await run(`{const e=document.querySelector(${JSON.stringify(selector)});e.value=${JSON.stringify(value)};e.dispatchEvent(new Event('change',{bubbles:true}));}`);await pause(300);}
 // Existing smoke has left tone -> output. Keep this headphone test quiet.
 await run(`(async()=>{let r=await window.nodivu.request('node.add',{kind:'gain',expected_revision:Number(document.body.dataset.revision)});if(!r.ok)throw new Error(r.error.message);let s=r.result;let g=s.graph.nodes.find(n=>n.block.kind==='gain');g.block.gain_db=-40;let t=s.graph.nodes.find(n=>n.block.kind==='tone'),o=s.graph.nodes.find(n=>n.block.kind==='output');s.graph.edges=[{from:t.id,to:g.id},{from:g.id,to:o.id}];let a=await window.nodivu.request('graph.apply',{graph:s.graph,expected_revision:s.revision});if(!a.ok)throw new Error(a.error.message);})()`);await pause(300);
 await select('.node[data-kind="output"] select',primary);await wait('document.body.dataset.state==="running"');
 await select('#monitor-device',monitor);
 assert.equal(await run(`Array.from(document.querySelector('#monitor-device').options).some(o=>o.value===${JSON.stringify(primary)})`),false);
 const generation=await run('document.body.dataset.generation');
 await run('document.getElementById("monitor-toggle").click()');await wait('document.getElementById("monitor-panel").dataset.state==="running"');await pause(1500);
 await run('document.getElementById("monitor-toggle").click()');await wait('document.getElementById("monitor-panel").dataset.state==="off"');
 assert.equal(await run('document.body.dataset.generation'),generation);assert.equal(await run('document.body.dataset.state'),'running');
 console.log('PASS: escuta pelo painel, WASAPI secundário, seleção explícita e saída principal preservada.');
};

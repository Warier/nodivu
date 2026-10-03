// Real renderer/backend/device inventory. Does not install a cable or open audio.
const assert=require('node:assert/strict');
module.exports=async win=>{
  const run=code=>win.webContents.executeJavaScript(code);
  const idle=async()=>{for(let i=0;i<100;i++){if(await run('document.body.dataset.busy === "false"'))return;await new Promise(r=>setTimeout(r,50));}throw new Error('GUI ocupada');};
  // Observe the renderer instead of racing its bounded, single-request IPC poll.
  const snapshot=()=>run(`({graph:{nodes:[...document.querySelectorAll('.node')].map(n=>({id:n.dataset.id,kind:n.dataset.kind,endpoint:n.querySelector('select')?.value})),edges:[...document.querySelectorAll('.wire')].map(e=>e.dataset.edge)},generation:document.body.dataset.generation})`);
  await idle();const before=await snapshot();
  assert.ok(before.graph.nodes.some(n=>n.kind==='output'));
  await run('document.getElementById("cable-setup").open=false;document.getElementById("virtual-output").click()');
  await new Promise(r=>setTimeout(r,150));await idle();
  const after=await snapshot();
  assert.deepEqual(after.graph,before.graph,'Atalho não deve trocar endpoint, fios ou duplicar saída');
  assert.equal(after.generation,before.generation);
  assert.equal(await run('document.getElementById("cable-setup").open'),true);
  assert.equal(await run('document.activeElement.closest(".node")?.dataset.kind'),'output');
  const state=await run('document.getElementById("cable-setup").dataset.state');
  assert.ok(['missing','incomplete','available'].includes(state));
  assert.ok(await run('document.getElementById("cable-status").textContent.length > 20'));
  const img=await win.webContents.capturePage();
  await require('node:fs/promises').writeFile(require('node:path').resolve(__dirname,'../../vb-cable-canvas.png'),img.toPNG());
  console.log('PASS: guia VB-CABLE, inventário real, foco, seleção explícita e preservação da saída. Estado:',state);
};

// Native dialogs alone are substituted; renderer, IPC, Rust and disk remain real.
const {dialog} = require('electron');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
module.exports = async win => {
 const original={open:dialog.showOpenDialog,save:dialog.showSaveDialog,message:dialog.showMessageBox};
 const dir=path.resolve(__dirname,'../..','project-smoke');
 await fs.mkdir(dir,{recursive:true});
 const file=path.join(dir,'round-trip.nodivu.json');
 dialog.showSaveDialog=async()=>({canceled:false,filePath:file});
 dialog.showOpenDialog=async()=>({canceled:false,filePaths:[file]});
 dialog.showMessageBox=async()=>({response:1});
 const run=code=>win.webContents.executeJavaScript(code),pause=ms=>new Promise(r=>setTimeout(r,ms));
 const idle=async()=>{for(let i=0;i<100;i++){if(await run('document.body.dataset.busy==="false"'))return;await pause(50);}throw new Error('Projeto ocupado');};
 const click=async selector=>{await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(120);await idle();};
 const geometry=()=>run(`({camera:document.getElementById('canvas').style.transform,nodes:[...document.querySelectorAll('.node')].map(n=>({id:n.dataset.id,x:n.style.left,y:n.style.top})),wires:document.querySelectorAll('.wire').length})`);
 try {
  await click('#zoom-out');const before=await geometry();
  await click('#project-save');
  const saved=JSON.parse(await fs.readFile(file,'utf8'));assert.equal(saved.format,'nodivu-project');assert.ok(saved.positions.length>0);
  assert.equal(await run('document.getElementById("project-name").textContent.includes("•")'),false);
  await click('#palette [data-kind=gain]');await click('#zoom-in');
  assert.equal(await run('document.getElementById("project-name").textContent.includes("•")'),true);
  const generation=await run('document.body.dataset.generation');
  const hasOutput=saved.graph.nodes.some(n=>n.block.kind==='output'&&n.block.endpoint_id);
  const ready=async()=>{for(let i=0;i<60&&await run('document.body.dataset.state')!==(hasOutput?'running':'idle');i++)await pause(100);assert.equal(await run('document.body.dataset.state'),hasOutput?'running':'idle');};
  await click('#project-open');assert.deepEqual(await geometry(),before);
  await ready();
  assert.equal(Number(await run('document.body.dataset.generation')),Number(generation)+(hasOutput?1:0),'Abrir deve preparar os dispositivos automaticamente');
  assert.equal(await run('document.getElementById("project-name").textContent.includes("•")'),false);
  const revision=await run('document.body.dataset.revision');
  await fs.writeFile(file,'{"format":"broken"}');await click('#project-open');
  assert.equal(await run('document.body.dataset.revision'),revision);assert.deepEqual(await geometry(),before);
  assert.ok(await run('!document.getElementById("error").hidden'));
  await fs.writeFile(file,JSON.stringify(saved));
  // Recent-file selection shares the validated open path and dirty confirmation.
  assert.equal(await run(`[...document.getElementById('project-recents').options].some(o=>o.value===${JSON.stringify(file)})`),true);
  const recent=async()=>{await idle();await run(`{const e=document.getElementById('project-recents');e.value=${JSON.stringify(file)};e.dispatchEvent(new Event('change',{bubbles:true}));}`);await pause(180);await idle();};
  await click('#palette [data-kind=gain]');const edited=await geometry();
  dialog.showMessageBox=async()=>({response:0});await recent();assert.deepEqual(await geometry(),edited,'Cancelar recente deve preservar alterações');
  dialog.showMessageBox=async()=>({response:1});await recent();assert.deepEqual(await geometry(),before);
  await ready();
  const missing=file+'.missing';await fs.rename(file,missing);
  try {await recent();assert.deepEqual(await geometry(),before);assert.ok(await run('!document.getElementById("error").hidden'));}
  finally {await fs.rename(missing,file);}
  console.log('PASS: recentes visíveis, descarte cancelável, preparação automática e arquivo ausente preservando projeto.');
  if(hasOutput){
   assert.equal(await run('document.getElementById("audio-action").hidden'),true,'Nenhuma ativação manual deve ser necessária');
   if(saved.resources.length){
    assert.ok(await run('!document.querySelector("[data-action=reload]").hidden'));
    // Opening prepares saved MP3 references without activation or reload clicks.
    for(let i=0;i<60&&await run('document.querySelector("[data-action=play]").disabled');i++)await pause(100);
    assert.equal(await run('document.querySelector("[data-action=play]").disabled'),false);
    assert.match(await run('document.querySelector("[data-player-status]").textContent'),/Arquivo pronto/,'Abrir não deve iniciar Play');
    await click('[data-action=play]');
   }
   for(let i=0;i<60&&!await run('Number.isFinite(parseFloat(document.getElementById("output-db").textContent))');i++)await pause(50);
   assert.ok(await run('Number.isFinite(parseFloat(document.getElementById("output-db").textContent))'),'Audio nao retornou apos abertura automatica');
  }
  await fs.writeFile(path.join(dir,'project-canvas.png'),await win.webContents.capturePage().then(image=>image.toPNG()));
  console.log('PASS: salvar/abrir via GUI, camera/posicoes/fios, dirty, rejeicao preservando projeto e audio automatico e Play separado.');
 } finally {dialog.showOpenDialog=original.open;dialog.showSaveDialog=original.save;dialog.showMessageBox=original.message;}
};

// Two actual Electron processes share a test-only temporary profile.
// Only native dialogs are substituted; timer, UI, Rust validation and disk are real.
const {app,dialog}=require('electron');
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
module.exports=async(win,recovery,engine)=>{
 const dir=app.getPath('userData'),original=path.join(dir,'original.nodivu.json'),saved=path.join(dir,'recovered.nodivu.json');
 const run=code=>win.webContents.executeJavaScript(code),pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function until(test,label){for(let n=0;n<300;n++){if(await test())return;await pause(50);}throw new Error(label);}
 const idle=()=>until(()=>run('document.body.dataset.ready==="true"&&document.body.dataset.busy==="false"'),'UI not ready');
 async function click(selector){await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(150);await idle();}
 const geometry=()=>run(`({camera:document.getElementById('canvas').style.transform,nodes:[...document.querySelectorAll('.node')].map(n=>({id:n.dataset.id,x:n.style.left,y:n.style.top})),wires:document.querySelectorAll('.wire').length})`);
 const dialogs={save:dialog.showSaveDialog,message:dialog.showMessageBox};
 try {
  await idle();
  if(process.env.NODIVU_RECOVERY_TEST_STAGE==='write'){
   dialog.showSaveDialog=async()=>({canceled:false,filePath:original});
   await click('#quick-start');await click('#project-save');
   const source=await fs.readFile(original,'utf8');await fs.writeFile(path.join(dir,'original-reference.json'),source);
   await click('#palette [data-kind=gain]');await click('#zoom-out');
   const expected=await geometry();await fs.writeFile(path.join(dir,'geometry.json'),JSON.stringify(expected));
   await until(async()=>{try{return JSON.parse(await fs.readFile(recovery.file,'utf8')).graph.nodes.length===3;}catch{return false;}},'Autosave timer did not write draft');
   assert.equal(await fs.readFile(original,'utf8'),source);
   console.log('PASS: timer wrote draft with unsaved edits; original file untouched. Abrupt exit next.');
   // Intentionally skip shutdown/clear to simulate a killed main process.
   app.exit(71);return;
  }
  assert.equal(recovery.status().pending,true);
  assert.equal(await run('document.getElementById("recovery-banner").hidden'),false);
  assert.equal(await run('document.querySelectorAll(".node").length'),0,'never restore implicitly');
  const before=await fs.readFile(recovery.file,'utf8');
  await click('#palette [data-kind=tone]');await pause(5300);
  assert.equal(await fs.readFile(recovery.file,'utf8'),before,'new session must not overwrite pending recovery');
  dialog.showMessageBox=async()=>({response:0});await click('#recovery-restore');
  assert.equal(await run('document.querySelectorAll(".node").length'),1,'canceled restore preserves open document');
  await click('#recovery-discard');assert.equal(await fs.readFile(recovery.file,'utf8'),before);
  // Invalid on-disk data must fail without replacing graph or deleting the draft.
  await fs.writeFile(recovery.file,'{broken');dialog.showMessageBox=async()=>({response:1});
  await click('#recovery-restore');assert.equal(await run('document.querySelectorAll(".node").length'),1);
  assert.equal(await fs.readFile(recovery.file,'utf8'),'{broken');await fs.writeFile(recovery.file,before);
  await click('#recovery-quick-restore');assert.deepEqual(await geometry(),JSON.parse(await fs.readFile(path.join(dir,'geometry.json'),'utf8')));
  assert.equal(await run('document.getElementById("project-name").textContent.includes("•")'),true);
  assert.equal(await fs.readFile(original,'utf8'),await fs.readFile(path.join(dir,'original-reference.json'),'utf8'));
  // Backend death exposes the already durable checkpoint again; reconnect + restore works.
  engine.child.kill();await until(()=>run('document.body.dataset.ready==="false"'),'backend fault not exposed');
  await clickOffline('#reconnect');await idle();assert.equal(recovery.status().pending,true);
  await click('#recovery-restore');assert.equal(await run('document.querySelectorAll(".node").length'),3);
  dialog.showSaveDialog=async()=>({canceled:false,filePath:saved});await click('#project-save');
  assert.equal(JSON.parse(await fs.readFile(saved,'utf8')).graph.nodes.length,3);
  assert.equal(recovery.status().available,false);assert.equal(await run('document.getElementById("project-name").textContent.includes("•")'),false);
  assert.equal(await fs.readFile(original,'utf8'),await fs.readFile(path.join(dir,'original-reference.json'),'utf8'));
  await click('#palette [data-kind=gain]');
  await until(()=>Promise.resolve(recovery.status().available),'new recovery did not write');
  dialog.showMessageBox=async()=>({response:0});win.close();await pause(150);assert.equal(win.isDestroyed(),false);assert.equal(recovery.status().available,true);
  console.log('PASS: canceled normal close retains unsaved checkpoint.');
  console.log('PASS: real restart, protected pending draft, canceled/corrupt restore, geometry, unsaved marker, backend crash/reconnect and Save As cleanup.');
  dialog.showMessageBox=async()=>({response:1});win.close();await pause(1000);
 }finally{dialog.showSaveDialog=dialogs.save;dialog.showMessageBox=dialogs.message;}
 async function clickOffline(selector){await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(180);}
};

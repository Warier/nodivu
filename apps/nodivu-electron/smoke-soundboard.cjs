// Real UI/backend/WASAPI; only OS file dialogs are replaced with test fixtures.
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
module.exports=async(win,manager)=>{
 const endpoint=process.env.NODIVU_SOUNDBOARD_OUTPUT;if(!endpoint)throw Error('Provide explicit NODIVU_SOUNDBOARD_OUTPUT for hardware smoke.');
 const run=code=>win.webContents.executeJavaScript(code,true),pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function until(code,label){for(let i=0;i<150;i++){if(await run(code))return;await pause(40);}throw Error(label+': '+await run('document.getElementById("error").textContent'));}
 const idle=()=>until('document.body.dataset.ready==="true"&&document.body.dataset.busy==="false"','busy');
 async function click(selector){await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(90);await idle();}
 await idle();await click('#palette [data-kind=output]');await click('#palette [data-plugin-id="org.nodivu.soundboard"]');
 const bank='.node[data-plugin-id="org.nodivu.soundboard"]';assert.equal(await run(`document.querySelectorAll('${bank} [data-bank-action=trigger]').length`),10);
 await run(`{const e=document.querySelector('.node.output select');e.value=${JSON.stringify(endpoint)};e.dispatchEvent(new Event('change',{bubbles:true}));}`);await pause(180);await idle();
 await click(bank+' [data-port=source]');await click('.node.output [data-port=sink]');await until('document.body.dataset.state==="running"','audio');
 await click(bank+' .node-config');
 const samples=48000*10,wave=Buffer.alloc(44+samples*2);wave.write('RIFF');wave.writeUInt32LE(wave.length-8,4);wave.write('WAVEfmt ',8);wave.writeUInt32LE(16,16);wave.writeUInt16LE(1,20);wave.writeUInt16LE(1,22);wave.writeUInt32LE(48000,24);wave.writeUInt32LE(96000,28);wave.writeUInt16LE(2,32);wave.writeUInt16LE(16,34);wave.write('data',36);wave.writeUInt32LE(samples*2,40);for(let i=0;i<samples;i++)wave.writeInt16LE(Math.round(1000*Math.sin(2*Math.PI*997*i/48000)),44+i*2);
 const fixture=path.join(path.dirname(manager.file),'sound.wav'),project=path.join(path.dirname(manager.file),'soundboard.nodivu.json');await fs.writeFile(fixture,wave);
 const dialog=require('electron').dialog,originalOpen=dialog.showOpenDialog,originalSave=dialog.showSaveDialog;
 try{
  dialog.showOpenDialog=async()=>({canceled:false,filePaths:[fixture]});
  for(let slot=0;slot<10;slot++){await click(`#inspector-content [data-bank-slot="${slot}"] [data-bank-action=file]`);await until(`!document.querySelector('${bank} [data-bank-slot="${slot}"] [data-bank-action=trigger]').disabled`,'slot '+slot);}
  dialog.showSaveDialog=async()=>({canceled:false,filePath:project});await click('#project-save');
  const saved=JSON.parse(await fs.readFile(project,'utf8'));assert.equal(saved.resources.length,10);assert.equal(new Set(saved.resources.map(r=>r.slot)).size,10);
  await run("Array.from(document.querySelectorAll('#inspector-content button')).find(b=>b.textContent.startsWith('Atalhos 1')).click()");await until('document.getElementById("hotkeys-dialog").open','preset');await click('#hotkeys-close');assert.equal(manager.config.bindings.length,10);assert.ok(manager.config.bindings.every(b=>manager.states.get(b.id)==='Global ativo'));
  const generation=await run('document.body.dataset.generation');win.minimize();
  const first=manager.fire(manager.config.bindings[0].id);const second=manager.fire(manager.config.bindings[1].id);await Promise.all([first,second]);
  await until(`document.querySelectorAll('${bank} .playing').length===2`,'two simultaneous sounds');
  win.restore();win.show();await click('#inspector-content [data-bank-slot="0"] [data-bank-action=stop]');await until(`document.querySelectorAll('${bank} .playing').length===1`,'independent stop');
  assert.equal(await run('document.body.dataset.generation'),generation);
  await click('#inspector-content [data-bank-slot="1"] [data-bank-action=stop]');
  dialog.showOpenDialog=async()=>({canceled:false,filePaths:[project]});await click('#project-open');await until(`document.querySelectorAll('${bank} [data-bank-action=trigger]:enabled').length===10`,'restore ten slots');assert.equal(await run(`document.querySelectorAll('${bank} .playing').length`),0);
  await click(bank+' .node-config');await click('#inspector-content [data-bank-slot="0"] [data-bank-action=clear]');await click('#project-save');assert.equal(JSON.parse(await fs.readFile(project,'utf8')).resources.length,9);
  await click('#project-open');await until(`document.querySelectorAll('${bank} [data-bank-action=trigger]:enabled').length===9`,'explicit clear persisted');
  await click(bank+' .node-config');
  const image=await win.webContents.capturePage();if(!image.isEmpty())await fs.writeFile(path.join(__dirname,'../../../soundboard-electron.png'),image.toPNG());
  console.log('PASS: Electron compact ten pads, inspector file selection, global preset, two actions while minimized, independent stop, ten-file save/open without autoplay, explicit clear persisted.');
 }finally{dialog.showOpenDialog=originalOpen;dialog.showSaveDialog=originalSave;}
};

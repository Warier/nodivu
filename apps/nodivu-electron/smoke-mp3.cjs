// Exercises renderer buttons; only the OS file picker is replaced with a fixture selection.
const assert=require('node:assert/strict'),path=require('node:path');
module.exports=async win=>{
 const run=code=>win.webContents.executeJavaScript(code),pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function click(selector){await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(400);}
 await click('#palette [data-plugin-id="org.nodivu.mp3-player"]');
 assert.equal(await run('document.querySelector(".node.plugin [data-action=play]").textContent'),'▶ Reproduzir');
 // Existing smoke leaves tone -> output. Replace this wire with the source plugin.
 await click('#connections .edge-remove');await click('.node.plugin [data-port=source]');await click('.node.output [data-port=sink]');
 const dialog=require('electron').dialog,original=dialog.showOpenDialog;
 const fs=require('node:fs/promises'),fixture=path.resolve(__dirname,'../../../mp3-test.mp3'),invalid=path.resolve(__dirname,'../../../invalid-test.mp3');
 await fs.writeFile(invalid,'not an MP3');
 try{
  dialog.showOpenDialog=async()=>({canceled:false,filePaths:[fixture+'.missing.mp3']});await click('.node.plugin [data-action=file]');
  assert.equal(await run('document.querySelector("[data-action=play]").disabled'),true);
  assert.ok(await run('!document.querySelector("[data-player-blocker]").hidden'));
  dialog.showOpenDialog=async()=>({canceled:false,filePaths:[invalid]});await click('.node.plugin [data-action=file]');
  for(let i=0;i<60&&!await run('document.querySelector("[data-player-blocker]").textContent.includes("decodificar")');i++)await pause(100);
  assert.match(await run('document.querySelector("[data-player-blocker]").textContent'),/decodificar/);
  dialog.showOpenDialog=async()=>({canceled:false,filePaths:[fixture]});await click('.node.plugin [data-action=file]');
 }finally{dialog.showOpenDialog=original;await fs.unlink(invalid);}
 for(let i=0;i<60;i++){if(await run('!document.querySelector(".node.plugin [data-action=play]").disabled'))break;await pause(100);}
 assert.equal(await run('document.querySelector(".node.plugin [data-action=play]").disabled'),false);
 await click('.node.plugin [data-action=play]');
 assert.equal(await run('document.body.dataset.signal'),'3');
 assert.ok(Number.isFinite(parseFloat(await run('document.getElementById("output-db").textContent'))));
 await click('.node.plugin [data-action=play]');
 assert.match(await run('document.querySelector("[data-action=play]").textContent'),/Continuar/);
 const paused=await run('Number(document.querySelector("[data-action=seek]").value)');await pause(300);
 assert.equal(await run('Number(document.querySelector("[data-action=seek]").value)'),paused,'Pausa deve congelar cursor');
 await run(`{const e=document.querySelector('[data-action=seek]');e.value=500;e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));}`);await pause(300);
 assert.equal(await run('Number(document.querySelector("[data-action=seek]").value)'),500);
 await click('.node.plugin [data-action=play]');assert.ok(await run('Number(document.querySelector("[data-action=seek]").value)>500'),'Continuar não deve reiniciar');
 await pause(1600);assert.ok(await run('document.querySelector("[data-player-status]").textContent.includes("Terminou")'));
 await click('#mute-all');assert.equal(await run('document.querySelector("[data-action=play]").disabled'),true);assert.match(await run('document.querySelector("[data-player-blocker]").textContent'),/silenciado/);await click('#mute-all');
 await click('#connections .edge-remove');assert.equal(await run('document.querySelector("[data-action=play]").disabled'),true);assert.match(await run('document.querySelector("[data-player-blocker]").textContent'),/Conecte/);
 await click('.node.plugin [data-port=source]');await click('.node.output [data-port=sink]');
 assert.equal(await run('document.querySelector("[data-action=play]").disabled'),false);
 console.log('PASS: motivos visíveis para arquivo ausente/inválido, mute e desconexão; recuperação para MP3 válido.');
 console.log('PASS: player no canvas, seletor de arquivo simulado, Play/pause, busca, retomada, medidor e EOF.');
};

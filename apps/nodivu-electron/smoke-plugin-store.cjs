const assert=require('node:assert/strict');
module.exports=async win=>{
 const run=code=>win.webContents.executeJavaScript(code,true);
 async function ready(){for(let i=0;i<100;i++){if(await run('document.getElementById("plugins-dialog").getAttribute("aria-busy")!=="true"'))return;await new Promise(r=>setTimeout(r,50));}throw new Error('Store UI timeout');}
 await run('document.getElementById("plugins-library").click()');await ready();
 const generation=await run('document.body.dataset.generation');
 await run('document.getElementById("plugins-explore").click()');await ready();
 assert.equal(await run('document.querySelectorAll(".catalog-plugin").length'),2);
 assert.equal(await run('document.querySelectorAll(".plugin-official").length'),1);
 await run('document.querySelector(".catalog-plugin button[data-action=store-details]").click()');
 for(let i=0;i<80;i++){if(await run('!document.getElementById("plugin-details").hidden && document.getElementById("plugin-readme").textContent.includes("Plugin de teste")'))break;await new Promise(r=>setTimeout(r,50));}
 assert.equal(await run('document.querySelectorAll("#plugin-readme table").length'),1);
 await run('document.getElementById("plugins-back").click()');
 await run('document.getElementById("plugins-search").value="community";document.getElementById("plugins-search").dispatchEvent(new Event("input"))');
 assert.equal(await run('document.querySelectorAll(".catalog-plugin").length'),1);
 // The harness only provides inert archives and uses a separate profile, never executable modules.
 await run('document.querySelector(".catalog-plugin button[data-action=store-install]").click()');await ready();
 const message=await run('document.getElementById("plugins-message").textContent');assert.ok(message.includes('mudanças pendentes'),message);
 const state=await run('window.nodivu.plugins("list")');assert.ok(state.entries.some(p=>p.id==='com.example.community'));
 assert.equal(await run('document.body.dataset.generation'),generation);
 await run('document.getElementById("plugins-search").value="";document.getElementById("plugins-explore").click()');await ready();
 await require('node:fs/promises').writeFile(require('node:path').resolve(__dirname,'../../plugin-store.png'),(await win.webContents.capturePage()).toPNG());
 await run('document.getElementById("plugins-close").click()');
 console.log('PASS: loja com catálogo TUF real de ensaio, dois pacotes, selo, busca, README e instalação pelo IPC sem reiniciar áudio.');
};

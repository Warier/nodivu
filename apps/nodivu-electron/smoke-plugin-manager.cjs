const assert=require('node:assert/strict');
module.exports=async (win,manager)=>{
 const run=code=>win.webContents.executeJavaScript(code,true);
 await run('document.getElementById("plugins-library").click()');
 for(let i=0;i<80;i++){
  if(await run('document.getElementById("plugins-dialog").getAttribute("aria-busy")!=="true"'))break;
  await new Promise(resolve=>setTimeout(resolve,50));
 }
 assert.equal(await run('document.getElementById("plugins-dialog").open'),true);
 assert.ok((await run('document.getElementById("plugins-message").textContent')).includes('Plugins locais'));
 const before=await run('document.body.dataset.generation');
 // Smoke profiles are isolated by main.cjs; exercise UI with its real bundled packages.
 const hasToggle=await run('Boolean(document.querySelector(".plugin-actions button[data-action=enable]"))');
 if(hasToggle){
  for(let turn=0;turn<2;turn++){
   await run('document.querySelector(".plugin-actions button[data-action=enable]").click()');
   for(let i=0;i<80;i++){
    if(await run('document.getElementById("plugins-dialog").getAttribute("aria-busy")!=="true"'))break;
    await new Promise(resolve=>setTimeout(resolve,50));
   }
   assert.ok((await run('document.getElementById("plugins-message").textContent')).includes(turn===0?'mudanças pendentes':'Plugins locais'));
  }
 }
 assert.equal(await run('document.body.dataset.generation'),before);
 const rows=(await manager.list()).entries;
 if(rows.length){
  const fs=require('node:fs/promises'),path=require('node:path'),folder=manager.folder(rows[0].key);
  await fs.writeFile(path.join(folder,'README.md'),'# Documentação de teste\n\n| Controle | Efeito |\n|---|---|\n| Ganho | Volume |\n\n![Exemplo](test-image.png)\n\n<script>window.readmeExecuted=true</script><img src="x" onerror="window.readmeExecuted=true"><a href="javascript:alert(1)" id="plugins-close">inseguro</a>');
  await fs.writeFile(path.join(folder,'test-image.png'),Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a0tQAAAAASUVORK5CYII=','base64'));
  await run('document.querySelector("button[data-action=details]").click()');
  for(let i=0;i<80;i++){
   if(await run('Boolean(document.querySelector("#plugin-readme img[src]"))'))break;
   await new Promise(resolve=>setTimeout(resolve,50));
  }
  assert.equal(await run('document.querySelectorAll("#plugin-readme table").length'),1);
  assert.equal(await run('document.querySelectorAll("#plugin-readme script,#plugin-readme [onerror],#plugin-readme [id],#plugin-readme a[href^=javascript]").length'),0);
  assert.equal(await run('Boolean(window.readmeExecuted)'),false);
  assert.ok(await run('document.querySelector("#plugin-readme img").src.startsWith("data:image/png")'));
  await fs.writeFile(path.resolve(__dirname,'../../plugin-readme.png'),(await win.webContents.capturePage()).toPNG());
  await run('document.getElementById("plugins-back").click()');
 }
 await require('node:fs/promises').writeFile(require('node:path').resolve(__dirname,'../../plugin-manager.png'),(await win.webContents.capturePage()).toPNG());
 await run('document.getElementById("plugins-close").click()');
 assert.equal(await run('document.getElementById("plugins-dialog").open'),false);
 console.log('PASS: gerenciador local abre/fecha, IPC real e ativação pendente sem alterar geração de áudio.');
};

const assert=require('node:assert/strict');
module.exports=async win=>{
 const run=code=>win.webContents.executeJavaScript(code);
 async function until(code){const end=Date.now()+9000;while(Date.now()<end){if(await run(code))return;await new Promise(r=>setTimeout(r,70));}throw new Error('Captura UI: '+await run('document.getElementById("error").textContent'));}
 for(let i=0;i<2;i++){
  await until('document.body.dataset.busy==="false"');
  await run('document.querySelector("#palette [data-plugin-id=\\"org.nodivu.windows-audio\\"]").click()');
  await until(`document.querySelectorAll('[data-capture-target]').length===${i+1}&&document.body.dataset.busy==='false'`);
 }
 await until("[...document.querySelectorAll('[data-capture-refresh]')].every(b=>!b.disabled)");
 assert.equal(await run("document.querySelectorAll('[data-capture-target] option[value=system]').length"),2);
 assert.ok(await run("[...document.querySelectorAll('[data-capture-status]')].every(e=>e.textContent.includes('Selecione'))"));
 if(process.env.NODIVU_CAPTURE_TEST_PID){
  const pid=Number(process.env.NODIVU_CAPTURE_TEST_PID);assert.ok(Number.isSafeInteger(pid)&&pid>0);
  const select="document.querySelector('[data-capture-target]')";
  await run(`{const s=${select};const option=[...s.options].find(o=>o.textContent.endsWith(' · '+${pid}));if(!option)throw new Error('Emissor não listado');s.value=option.value;s.dispatchEvent(new Event('change',{bubbles:true}));}`);
  await until("document.querySelector('[data-capture-status]').textContent.startsWith('Capturando')&&document.body.dataset.busy==='false'");
  await run(`{const s=${select};s.value='';s.dispatchEvent(new Event('change',{bubbles:true}));}`);
  await until("document.querySelector('[data-capture-status]').textContent.startsWith('Selecione')&&document.body.dataset.busy==='false'");
  console.log('PASS: seleção real pelo renderer iniciou captura; limpar a escolha encerrou a fonte.');
 }
 await run("document.getElementById('fit-view').click()");
 await new Promise(r=>setTimeout(r,200));
 if(process.env.NODIVU_SMOKE_SCREENSHOT)await require('node:fs/promises').writeFile(process.env.NODIVU_SMOKE_SCREENSHOT,(await win.webContents.capturePage()).toPNG());
 console.log('PASS: dois blocos de aplicativos com seleção independente e lista atualizada no Electron.');
};

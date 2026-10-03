// Real canvas and Python/WASAPI; no mocked worker or endpoint.
const assert=require('node:assert/strict');
module.exports=async win=>{
 const run=code=>win.webContents.executeJavaScript(code),pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function until(code,message){for(let i=0;i<70;i++){if(await run(code))return;await pause(100);}throw new Error(message);}
 async function click(selector){await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(300);}
 const plugin='.node.plugin',field=plugin+' [data-field="parameter:7"]';
 await click('#palette [data-plugin-id="org.nodivu.python.gain"]');
 assert.ok(await run('document.querySelector(".node.plugin").textContent.includes("+20 ms")'));
 await click('#connections .edge-remove');
 await click('.node.tone [data-port=source]');await click(plugin+' [data-port=sink]');
 await click(plugin+' [data-port=source]');await click('.node.output [data-port=sink]');
 await until('document.querySelector("[data-worker-status]").textContent.includes("Processando")','Worker não iniciou');
 const generation=await run('document.body.dataset.generation');
 await run(`{const e=document.querySelector(${JSON.stringify(field)});e.value='0.5';e.dispatchEvent(new Event('input',{bubbles:true}));}`);
 await until('Math.abs(parseFloat(document.getElementById("output-db").textContent)+42.02)<0.3','Ganho Python não chegou ao medidor');
 await click(plugin+' [data-field=bypass]');
 await until('Math.abs(parseFloat(document.getElementById("output-db").textContent)+36)<0.3','Bypass não preservou sinal');
 await click(plugin+' [data-action=restart]');
 await until('document.querySelector("[data-worker-status]").textContent.includes("Processando")','Restart não recuperou worker');
 assert.equal(await run('document.body.dataset.generation'),generation);
 console.log('PASS: worker visível, +20 ms, áudio Python, controle de ganho, bypass e restart sem reopen.');
};

const assert=require('node:assert/strict');
module.exports=async win=>{
 const run=code=>win.webContents.executeJavaScript(code),pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function idle(){for(let i=0;i<100;i++){if(await run('document.body.dataset.busy==="false"'))return;await pause(40);}throw new Error('UI ocupada');}
 async function click(selector){await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(160);await idle();}
 const voice='[data-plugin-id="org.nodivu.voice-cleanup"]',limiter='[data-plugin-id="org.nodivu.peak-limiter"]';
 const generation=await run('document.body.dataset.generation');
 await click('#palette '+voice);await click('#palette '+limiter);
 await click('.node'+voice+' .node-config');
 assert.match(await run('document.getElementById("inspector-content").textContent'),/Limpeza/);
 assert.match(await run(`document.querySelector('.node${voice}').textContent`),/30 ms/);
 await run(`{const c=document.querySelector('#inspector-content [data-field="parameter:0"]');c.value='0';c.dispatchEvent(new Event('input',{bubbles:true}));}`);await pause(200);await idle();
 await click('#connections .edge-remove');
 await click('.node.tone [data-port=source]');await click('.node'+voice+' [data-port=sink]');
 await click('.node'+voice+' [data-port=source]');await click('.node'+limiter+' [data-port=sink]');
 await click('.node'+limiter+' [data-port=source]');await click('.node.output [data-port=sink]');
 await pause(400);assert.equal(await run('document.body.dataset.state'),'running');
 assert.ok(Number.isFinite(parseFloat(await run('document.getElementById("output-db").textContent'))));
 await click('.node'+limiter+' .node-config');
 await run(`{const c=document.querySelector('#inspector-content [data-field="parameter:0"]');c.value='-12';c.dispatchEvent(new Event('input',{bubbles:true}));}`);await pause(200);await idle();
 assert.equal(await run('document.body.dataset.generation'),generation);
 assert.equal(await run('document.getElementById("error").textContent'),'');
 await require('node:fs/promises').writeFile(require('node:path').resolve(__dirname,'../../cleanup-studio.png'),(await win.webContents.capturePage()).toPNG());
 await click('.node'+voice+' .node-close');await click('.node'+limiter+' .node-close');
 await click('.node.tone [data-port=source]');await click('.node.output [data-port=sink]');
 console.log('PASS: RNNoise/limitador no Estúdio, controles reais e encadeamento sem reabrir áudio.');
};

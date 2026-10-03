// Run after base smoke: tone already feeds output. Exercise visible multi-port wiring.
const assert=require('node:assert/strict');
module.exports=async win=>{
 const run=code=>win.webContents.executeJavaScript(code),pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function until(code,label){for(let i=0;i<80;i++){if(await run(code))return;await pause(60);}throw new Error(label+': '+await run('document.getElementById("error").textContent'));}
 async function click(selector){await until('document.body.dataset.busy==="false"','busy');await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(140);await until('document.body.dataset.busy==="false"','busy');}
 const port=(kind,direction,index=0)=>`.node.${kind} [data-port="${direction}"][data-port-id="${index}"]`;
 async function wire(from,to,index=0){await click(port(from,'source'));await click(port(to,'sink',index));}
 const generation=await run('document.body.dataset.generation');
 for(const kind of['gain','mixer','meter'])await click(`#palette [data-kind="${kind}"]`);
 assert.equal(await run('document.querySelectorAll(".node.mixer [data-port=sink]").length'),4);
 await click('#connections .edge-remove');
 await wire('tone','mixer',0);await wire('tone','gain');await wire('gain','mixer',1);await wire('mixer','output');await wire('tone','meter');
 await until('Math.abs(parseFloat(document.getElementById("output-db").textContent)+29.98)<0.3','Mixer nao soma duas contribuicoes');
 await until('Math.abs(parseFloat(document.querySelector(".node.meter [data-node-level]").textContent)+24)<0.3','Medidor nao recebe ramo');
 await click('#palette [data-plugin-id="org.nodivu.fixture.consumer"]');
 assert.equal(await run('document.querySelectorAll(".node.plugin [data-port=source]").length'),0);
 await click(port('tone','source'));await click('.node.plugin [data-port=sink]');
 await until('Math.abs(parseFloat(document.querySelector(".node.plugin [data-node-level]").textContent)+24)<0.3','Consumidor externo nao recebe');
 // The two tone branches remain distinct. Right-click removes only the chosen connection.
 await run(`{const tone=document.querySelector('.node.tone').dataset.id,mix=document.querySelector('.node.mixer').dataset.id;const wire=[...document.querySelectorAll('.wire')].find(w=>{const e=JSON.parse(w.dataset.edge);return e.from===tone&&e.to===mix;});wire.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true}));}`);
 await until('Math.abs(parseFloat(document.getElementById("output-db").textContent)+36)<0.3','Remover um ramo afetou soma incorretamente');
 assert.equal(await run('document.querySelectorAll(".wire").length'),5);
 assert.equal(await run('document.body.dataset.generation'),generation);
 assert.equal(await run('document.getElementById("error").textContent'),'');
 await click('#center');
 await require('node:fs/promises').writeFile(require('node:path').resolve(process.cwd(),'.local/routing-canvas.png'),(await win.webContents.capturePage()).toPNG());
 console.log('PASS: canvas v2, Mixer quatro portas, fanout, medidores, plugin consumidor sem saida e exclusao individual de fio.');
};

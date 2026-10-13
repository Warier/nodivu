const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const path=require('node:path');
module.exports=async win=>{
 const run=async code=>{try{return await win.webContents.executeJavaScript(code);}catch(e){throw new Error('Studio script: '+code+'; '+e.message);}},pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function until(code,label){for(let i=0;i<100;i++){if(await run(code))return;await pause(40);}throw new Error(label+': '+await run('document.getElementById("error").textContent'));}
 const idle=()=>until('document.body.dataset.busy==="false"','operação pendente');
 async function click(selector){await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(140);await idle();}
 const initialGains=await run('[...document.querySelectorAll(".node.gain")].map(n=>n.dataset.id)');
 const generation=await run('document.body.dataset.generation');
 await click('#palette [data-kind=gain]');await run(`{const before=${JSON.stringify(initialGains)};[...document.querySelectorAll('.node.gain')].find(n=>!before.includes(n.dataset.id)).classList.add('studio-test');}`);await click('.node.studio-test .node-config');
 await until('!document.getElementById("block-inspector").hidden','inspirador ausente');
 await run(`{const c=document.querySelector('#inspector-content [data-field=gain_db]');c.value='6';c.dispatchEvent(new Event('input',{bubbles:true}));}`);
 await until('document.querySelector(".node.studio-test [data-field=gain_db]").value==="6"&&document.body.dataset.busy==="false"','ajuste do painel não chegou ao bloco');
 await run(`{const c=document.querySelector('.node.studio-test [data-field=gain_db]');c.value='-12';c.dispatchEvent(new Event('input',{bubbles:true}));}`);
 await until('document.querySelector("#inspector-content [data-field=gain_db]").value==="-12"&&document.body.dataset.busy==="false"','ajuste do bloco não chegou ao painel');
 assert.equal(await run('document.querySelector(".node.studio-test").offsetWidth'),218);
 assert.ok(await run('document.querySelector(".node.studio-test").offsetHeight<230'),'bloco compacto');
 await pause(220);await idle();
 const revision=await run('document.body.dataset.revision');
 await click('#library-toggle');assert.equal(await run('document.querySelector(".library").offsetWidth'),64);
 await click('#library-toggle');
 await run(`document.getElementById('library-resize').dispatchEvent(new KeyboardEvent('keydown',{key:'End',bubbles:true}));`);
 assert.equal(await run('document.querySelector(".library").offsetWidth'),320);
 await run(`document.getElementById('library-resize').dispatchEvent(new KeyboardEvent('keydown',{key:'Home',bubbles:true}));`);
 assert.equal(await run('document.querySelector(".library").offsetWidth'),184);
 const handle=await run(`{const r=document.getElementById('inspector-handle').getBoundingClientRect();({x:Math.round(r.left+30),y:Math.round(r.top+18)})}`);
 win.webContents.sendInputEvent({type:'mouseDown',button:'left',...handle,clickCount:1});
 win.webContents.sendInputEvent({type:'mouseMove',button:'left',x:10,y:10});win.webContents.sendInputEvent({type:'mouseUp',button:'left',x:10,y:10,clickCount:1});await pause(120);
 assert.ok(await run(`{const p=document.getElementById('block-inspector').getBoundingClientRect(),w=document.querySelector('main').getBoundingClientRect();p.left>=w.left&&p.top>=w.top&&p.right<=w.right&&p.bottom<=w.bottom}`),'painel contido no espaço de trabalho');
 await click('#inspector-collapse');assert.equal(await run('document.getElementById("inspector-content").offsetHeight'),0);
 await click('#inspector-collapse');await click('#inspector-close');await click('#inspect-selected');
 assert.equal(await run('document.getElementById("block-inspector").hidden'),false);
 assert.equal(await run('document.body.dataset.revision'),revision,'layout não deve editar o grafo');
 await click('#inspector-home');
 await click('#center');
 await fs.writeFile(path.resolve(__dirname,'../../studio-canvas.png'),(await win.webContents.capturePage()).toPNG());
 await click('#inspector-close');
 // Palette drop uses the current camera rather than screen coordinates.
 const count=await run('document.querySelectorAll(".node.gain").length');
 await run(`{const dt=new DataTransfer(),b=document.querySelector('#palette [data-kind=gain]'),v=document.getElementById('viewport'),r=v.getBoundingClientRect();b.dispatchEvent(new DragEvent('dragstart',{dataTransfer:dt,bubbles:true}));v.dispatchEvent(new DragEvent('drop',{dataTransfer:dt,bubbles:true,cancelable:true,clientX:r.left+110,clientY:r.top+120}));}`);
 await until(`document.querySelectorAll('.node.gain').length===${count+1}&&document.body.dataset.busy==='false'`,'arraste da biblioteca');
 assert.ok(await run(`{const r=document.querySelectorAll('.node.gain')[${count}].getBoundingClientRect(),v=document.getElementById('viewport').getBoundingClientRect(),z=Number(document.getElementById('viewport').dataset.zoom);Math.abs(r.left-(v.left+110-100*z))<2}`),'drop com zoom');
 for(const id of await run(`[...document.querySelectorAll('.node.gain')].map(n=>n.dataset.id).filter(id=>!${JSON.stringify(initialGains)}.includes(id))`))await click(`.node[data-id="${id}"] .node-close`);
 if(await run('document.body.dataset.demo==="true"')){
  await click('#palette [data-plugin-id="org.nodivu.demo.controls"]');await click('.node[data-plugin-id="org.nodivu.demo.controls"] .node-config');
  assert.equal(await run('document.querySelectorAll("#inspector-content [role=tab]").length'),3);
  await click('#inspector-content [role=tab]:nth-child(2)');
  assert.ok(await run(`Boolean(document.querySelector('#inspector-content [data-field="parameter:8"]').offsetParent)`));
  await run(`{const c=document.querySelector('#inspector-content [data-field="parameter:8"]');c.value='1';c.dispatchEvent(new Event('change',{bubbles:true}));}`);
  await until(`document.querySelector('.node.plugin [data-field="parameter:8"]').value==='1'&&document.body.dataset.busy==='false'`,'opção numérica do plugin');
  await click('#inspector-content [role=tab]:first-child');await click('#inspector-content details summary');
  assert.ok(await run(`Boolean(document.querySelector('#inspector-content [data-field="parameter:9"]').offsetParent)`));
  assert.equal(await run(`document.querySelector('#inspector-content [data-field="parameter:9"]').disabled`),true,'readonly preservado');
  await fs.writeFile(path.resolve(__dirname,'../../studio-plugin.png'),(await win.webContents.capturePage()).toPNG());
  await click('.node[data-plugin-id="org.nodivu.demo.controls"] .node-close');
 }
 if(process.argv.includes('--test-studio-plugin')){
  const selector='.node[data-plugin-id="org.nodivu.fixture.gain"]';
  const dry=parseFloat(await run('document.getElementById("output-db").textContent'));assert.ok(Number.isFinite(dry),'tom de referência');
  await click('#palette [data-plugin-id="org.nodivu.fixture.gain"]');await click(selector+' .node-config');
  assert.equal(await run('document.querySelector("#inspector-content [role=tab]").textContent'),'Som');
  await click('#connections .edge-remove');await click('.node.tone [data-port=source]');await click(selector+' [data-port=sink]');await click(selector+' [data-port=source]');await click('.node.output [data-port=sink]');
  await pause(450);const initial=parseFloat(await run('document.getElementById("output-db").textContent'));assert.ok(Math.abs(initial-(dry+20*Math.log10(0.5)))<0.3,'fixture real 0.5x');
  await run(`{const c=document.querySelector('#inspector-content [data-field="parameter:7"]');c.value='0.25';c.dispatchEvent(new Event('input',{bubbles:true}));}`);await pause(550);await idle();
  const changed=parseFloat(await run('document.getElementById("output-db").textContent'));assert.ok(Math.abs(changed-(initial+20*Math.log10(0.25)))<0.3,'parâmetro do painel chegou ao DSP');
  await fs.writeFile(path.resolve(__dirname,'../../studio-native-plugin.png'),(await win.webContents.capturePage()).toPNG());
  await click(selector+' .node-close');await click('.node.tone [data-port=source]');await click('.node.output [data-port=sink]');
  console.log(`PASS: manifesto CLAP com abas/ajuda, ajuste no inspetor chegou ao áudio real: ${initial} → ${changed} dBFS.`);
 }
 assert.equal(await run('document.body.dataset.generation'),generation,'UI não deve reabrir áudio');
 assert.equal(await run('document.getElementById("error").textContent'),'');
 console.log('PASS: Estúdio, configurações sincronizadas por UUID, biblioteca/arraste, painel móvel limitado, contexto e geração de áudio preservada.');
};

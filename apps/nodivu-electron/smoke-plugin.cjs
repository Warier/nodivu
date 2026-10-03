const assert=require('node:assert/strict');
const path=require('node:path');
const fs=require('node:fs/promises');
module.exports=async function(win){
  const run=code=>win.webContents.executeJavaScript(code);
  const pause=ms=>new Promise(r=>setTimeout(r,ms));
  async function idle(){for(let i=0;i<100;i++){if(await run('document.body.dataset.busy === "false"'))return;await pause(40);}throw new Error('Interface ocupada');}
  async function click(selector){await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(180);await idle();}
  async function field(key,value){await idle();await run(`{const c=document.querySelector('.node.plugin [data-field="${key}"]');if(c.type==='checkbox')c.checked=${JSON.stringify(value)};else c.value=${JSON.stringify(value)};c.dispatchEvent(new Event(c.type==='checkbox'?'change':'input',{bubbles:true}));}`);await pause(600);await idle();}
  async function wire(from,to){await click(`.node.${from} [data-port=source]`);await click(`.node.${to} [data-port=sink]`);}
  const generation=await run('document.body.dataset.generation');
  await click('#palette [data-kind=plugin]');
  assert.equal(await run('document.querySelectorAll(".node.plugin").length'),1);
  assert.ok(await run('document.querySelector(".node.plugin").textContent.includes("Studio Gain")'));
  assert.ok(await run('document.querySelector(".node.plugin").textContent.includes("Seu sinal")'));
  assert.equal(await run('getComputedStyle(document.querySelector(".node.plugin")).borderTopColor'),'rgb(230, 170, 104)');
  await click('#connections .edge-remove');await wire('tone','plugin');await wire('plugin','output');await pause(650);
  const db=async()=>parseFloat(await run('document.getElementById("output-db").textContent'));
  const initial=await db();assert.ok(Math.abs(initial+42)<0.2,`Nível com DLL: ${initial}`);
  await field('parameter:7',0.25);const changed=await db();assert.ok(Math.abs(changed+54)<0.2,`Parâmetro visual não chegou ao DSP: ${changed}`);
  await field('bypass',true);const bypass=await db();assert.ok(Math.abs(bypass+36)<0.2,`Bypass: ${bypass}`);
  await field('bypass',false);
  assert.equal(await run('document.body.dataset.generation'),generation);
  const image=await win.webContents.capturePage();
  await fs.writeFile(path.resolve(__dirname,'../../electron-plugin.png'),image.toPNG());
  // Troca de fonte pelos fios: a mesma DLL agora recebe o microfone já aberto.
  await click('#connections .edge-remove');await wire('capture','plugin');await pause(600);
  assert.equal(await run('document.body.dataset.signal'),'1');
  assert.ok(Number.isFinite(await db()),'Microfone sem sinal no plugin');
  assert.equal(await run('document.body.dataset.generation'),generation);
  assert.equal(await run('document.getElementById("error").textContent'),'');
  // Add another external instance and two native gains through the actual palette.
  await click('#palette [data-kind=plugin]');
  await click('#palette [data-kind=gain]');await click('#palette [data-kind=gain]');
  assert.equal(await run('document.querySelectorAll(".node.plugin").length'),2);
  assert.equal(await run('document.querySelector(".node.gain input[type=range]").max'),'24');
  // Name nodes for unambiguous test wiring (same title, different UUIDs).
  await run(`document.querySelectorAll('.node.plugin').forEach((n,i)=>n.classList.add('plugin'+i));document.querySelectorAll('.node.gain').forEach((n,i)=>n.classList.add('gain'+i));`);
  while(await run('document.querySelectorAll("#connections .edge-remove").length'))await click('#connections .edge-remove');
  await wire('tone','plugin0');await wire('plugin0','gain0');await wire('gain0','plugin1');await wire('plugin1','gain1');await wire('gain1','output');
  await run(`document.querySelectorAll('.node.gain input[type=range]').forEach(c=>{c.value='6';c.dispatchEvent(new Event('input',{bubbles:true}));});`);
  await pause(800);await idle();
  const composed=await db();assert.ok(Math.abs(composed+48.1)<0.25,`Cadeia mista com ganhos positivos: ${composed}`);
  assert.equal(await run('document.body.dataset.generation'),generation);
  assert.equal(await run('document.getElementById("error").textContent'),'');
  await click('#center');
  assert.ok(await run(`{const r=[...document.querySelectorAll('.node')].map(n=>n.getBoundingClientRect());r.every((a,i)=>r.slice(i+1).every(b=>a.right<=b.left||b.right<=a.left||a.bottom<=b.top||b.bottom<=a.top));}`),'Organização deve separar os blocos');
  await fs.writeFile(path.resolve(__dirname,'../../electron-plugin-chain.png'),(await win.webContents.capturePage()).toPNG());
  console.log(`PASS: plugin visual, manifesto/cores/grupos, parâmetro ${initial}→${changed} dBFS, bypass ${bypass} dBFS e microfone real; cadeia de dois plugins com ganhos positivos ${composed} dBFS e organização sem sobreposição.`);
};

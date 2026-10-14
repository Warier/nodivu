const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs/promises');
module.exports=async(win,manager)=>{
 const run=async code=>{try{return await win.webContents.executeJavaScript(code,true);}catch(e){throw Error(code+': '+e.message);}},pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function until(code,label){for(let i=0;i<200;i++){if(await run(code))return;await pause(75);}throw Error(label+': '+await run('document.getElementById("plugins-message").textContent'));}
 const ready=()=>until('document.getElementById("plugins-dialog").getAttribute("aria-busy")!=="true"','store busy');
 await until('document.body.dataset.ready==="true"','backend boot');
 await run('document.getElementById("plugins-library").click()');await ready();await run('document.getElementById("plugins-explore").click()');await ready();
 assert.equal(await run('document.querySelectorAll(".catalog-plugin").length'),2);assert.equal(await run('document.querySelectorAll(".plugin-official").length'),2);
 const generation=await run('document.body.dataset.generation');
 for(const id of ['org.nodivu.ducking','org.nodivu.talk-button']){
  const selector='.catalog-plugin[data-plugin-id="'+id+'"]';
  await run(`document.querySelector(${JSON.stringify(selector+' [data-action=store-details]')}).click()`);
  await until('!document.getElementById("plugin-details").hidden&&document.querySelectorAll("#plugin-readme table").length>0','README');
  assert.ok((await run('document.getElementById("plugin-readme").textContent')).includes('host_api 2'));
  await run('document.getElementById("plugins-back").click()');await ready();
  await run(`document.querySelector(${JSON.stringify(selector+' [data-action=store-install]')}).click()`);await ready();
  assert.ok((await manager.list()).entries.some(p=>p.id===id),'installed package '+id);
  await run('document.getElementById("plugins-explore").click()');await ready();
  await until(`document.querySelector(${JSON.stringify(selector+' [data-action=store-install]')}).textContent==='Instalado'`,'install');
 }
 const state=await manager.list();assert.ok(state.entries.some(p=>p.id==='org.nodivu.ducking'));assert.ok(state.entries.some(p=>p.id==='org.nodivu.talk-button'));
 assert.equal(await run('document.body.dataset.generation'),generation);
 // Launch a separate owner from the downloaded packages, like the next application startup.
 const {Engine}=require('./engine.cjs');const engine=new Engine(path.join(__dirname,'bin/nodivu-app-backend.exe'),{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.join(manager.root,'plugin.json')});
 try {const c=await engine.request('plugins.list');assert.deepEqual(c.plugins.find(p=>p.id==='org.nodivu.ducking').inputs,[0,1]);assert.ok(c.plugins.find(p=>p.id==='org.nodivu.talk-button'));}finally{await engine.stop();}
 await run('document.getElementById("plugins-close").click()');
 await run('document.getElementById("reconnect").click()');
 await until(`document.body.dataset.ready==="true"&&document.querySelector(${JSON.stringify('#palette [data-plugin-id="org.nodivu.ducking"]')})!==null`,'catalog reload');
 const idle=()=>until('document.body.dataset.busy==="false"','renderer busy');
 async function click(selector){await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(140);await idle();}
 for(const id of ['org.nodivu.ducking','org.nodivu.talk-button'])await click('#palette [data-plugin-id="'+id+'"]');
 const duck='.node[data-plugin-id="org.nodivu.ducking"]',talk='.node[data-plugin-id="org.nodivu.talk-button"]';
 assert.equal(await run(`document.querySelectorAll('${duck} [data-port=sink]').length`),2);
 await click(talk+' .node-config');
 const hold='#inspector-content [data-field="parameter:2"]';
 const revision=await run('document.body.dataset.revision');
 await run(`{const b=document.querySelector(${JSON.stringify(hold)});b.focus();b.dispatchEvent(new KeyboardEvent('keydown',{key:' ',bubbles:true}));}`);
 await pause(1200);
 assert.equal(await run(`document.querySelector(${JSON.stringify(hold)}).getAttribute('aria-pressed')`),'true','hold survives concurrent snapshot polling');
 await run(`document.querySelector(${JSON.stringify(hold)}).dispatchEvent(new KeyboardEvent('keyup',{key:' ',bubbles:true}))`);
 assert.equal(await run(`document.querySelector(${JSON.stringify(hold)}).getAttribute('aria-pressed')`),'false');
 assert.equal(await run('document.body.dataset.revision'),revision,'hold is transient');
 await run(`{const b=document.querySelector(${JSON.stringify(hold)});b.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));b.blur();}`);
 assert.equal(await run(`document.querySelector(${JSON.stringify(hold)}).getAttribute('aria-pressed')`),'false','blur releases');
 assert.equal(await run('document.getElementById("error").textContent'),'');
 try{await fs.writeFile(path.resolve(__dirname,'../../utility-store.png'),(await win.webContents.capturePage()).toPNG());}catch(e){console.log('Visual screenshot unavailable (functional assertions passed): '+e.message);}
 console.log('PASS: public HTTPS TUF store, both official packages, README tables, downloads, installation and native reload');
};

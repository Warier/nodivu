// Uses a real backend/player/output. Only the file chooser selects a generated WAV fixture.
// Registration is native; action dispatch is invoked explicitly, not claimed as a game test.
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path');
module.exports=async(win,manager)=>{
 const run=async code=>{try{return await win.webContents.executeJavaScript(code,true);}catch(e){throw Error(code+': '+e.message);}},pause=ms=>new Promise(r=>setTimeout(r,ms));
 async function until(code,label){for(let i=0;i<100;i++){if(await run(code))return;await pause(60);}throw Error(label+': '+await run('document.getElementById("error").textContent'));}
 const idle=()=>until('document.body.dataset.busy==="false"','busy');
 async function click(selector){await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await pause(180);await idle();}
 await until('document.body.dataset.ready==="true"','boot');
 assert.ok(require('electron').Menu.getApplicationMenu().items.some(i=>i.label==='Configurações'));
 await require('./smoke.cjs')(win,true);
 await click('#palette [data-kind="gain"]');await click('#palette [data-plugin-id="org.nodivu.mp3-player"]');
 const player='.node[data-plugin-id="org.nodivu.mp3-player"]';
 await click('#connections .edge-remove');await click(player+' [data-port=source]');await click('.node.output [data-port=sink]');
 const samples=48000*8,wav=Buffer.alloc(44+samples*2);wav.write('RIFF');wav.writeUInt32LE(wav.length-8,4);wav.write('WAVEfmt ',8);wav.writeUInt32LE(16,16);wav.writeUInt16LE(1,20);wav.writeUInt16LE(1,22);wav.writeUInt32LE(48000,24);wav.writeUInt32LE(96000,28);wav.writeUInt16LE(2,32);wav.writeUInt16LE(16,34);wav.write('data',36);wav.writeUInt32LE(samples*2,40);for(let i=0;i<samples;i++)wav.writeInt16LE(Math.round(600*Math.sin(2*Math.PI*440*i/48000)),44+i*2);
 const fixture=path.join(path.dirname(manager.file),'hotkey-test.wav');await fs.writeFile(fixture,wav);
 const dialog=require('electron').dialog,original=dialog.showOpenDialog;
 try{dialog.showOpenDialog=async()=>({canceled:false,filePaths:[fixture]});await click(player+' [data-action=file]');}finally{dialog.showOpenDialog=original;}
 await until(`!document.querySelector('${player} [data-action=play]').disabled`,'player ready');
 await click(player+' .node-config');await click('#inspector-content [data-action=hotkeys]');
 await until('document.querySelectorAll("#hotkey-action option").length>2','action list');
 assert.ok(await run(`Array.from(document.querySelectorAll('#hotkey-action option')).every(o=>o.textContent.includes('Player'))`));
 await click('#hotkeys-all');
 const playerId=await run(`document.querySelector('${player}').dataset.id`);
 const action=[...manager.actions.values()].find(a=>a.nodeId===playerId&&a.id.endsWith('/player.toggle'));assert.ok(action);
 await run(`document.getElementById('hotkey-action').value=${JSON.stringify(action.id)};document.getElementById('hotkey-action').dispatchEvent(new Event('change'));document.getElementById('hotkey-key').value='Control+Shift+F9';document.getElementById('hotkey-form').requestSubmit();`);
 await until('document.querySelectorAll("#hotkeys-list tr").length===1','saved shortcut');
 await click('#hotkeys-close');await pause(100);
 const b=manager.config.bindings[0];assert.equal(manager.states.get(b.id),'Global ativo');assert.equal(require('electron').globalShortcut.isRegistered(b.accelerator),true);
 const generation=await run('document.body.dataset.generation');
 win.minimize();await pause(150);assert.equal(await manager.fire(b.id),true);await pause(400);
 assert.equal(await manager.fire(b.id,{test:true}),true);await pause(1100);
 assert.match(await run(`document.querySelector('${player} [data-action=play]').textContent`),/Continuar/);
 const position=await run(`Number(document.querySelector('${player} [data-action=seek]').value)`);assert.ok(position>0);await pause(250);assert.equal(await run(`Number(document.querySelector('${player} [data-action=seek]').value)`),position);
 win.restore();win.show();win.focus();await pause(150);
 const gain=[...manager.actions.values()].find(a=>a.id.endsWith('/gain/gain_db.set'));assert.ok(gain);
 await manager.save({...manager.config,bindings:[...manager.config.bindings,{id:'gain',action:gain.id,label:gain.label,enabled:true,scope:'local',accelerator:'Control+Shift+F10',value:6}]});
 win.webContents.sendInputEvent({type:'keyDown',keyCode:'F10',modifiers:['control','shift']});win.webContents.sendInputEvent({type:'keyUp',keyCode:'F10',modifiers:['control','shift']});
 await until('document.querySelector(".node.gain [data-field=gain_db]").value==="6"','local shortcut real input');
 await click(player+' .node-close');await pause(100);assert.match(manager.states.get(b.id),/ausente/);assert.equal(require('electron').globalShortcut.isRegistered(b.accelerator),false);
 assert.equal(await run('document.body.dataset.generation'),generation,'shortcuts must not reopen audio');
 console.log('PASS: native global registration/release, preferences form, block filter, real player pause/resume while minimized, local keyboard -> gain, absent UUID and stable audio generation.');
 await click('#hotkeys-open');await until('document.querySelectorAll("#hotkeys-list tr").length===2','preferences reopened');
 try{await fs.writeFile(path.resolve(__dirname,'../../hotkeys.png'),(await win.webContents.capturePage()).toPNG());}catch(e){console.log('Visual capture unavailable: '+e.message);}
};

const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {Hotkeys,accelerator,validate}=require('../hotkeys.cjs');
const binding=(overrides={})=>({id:'one',scope:'global',enabled:true,action:'node/play',label:'Player',accelerator:'ctrl+shift+F8',...overrides});
const config=bindings=>({version:1,enabled:true,bindings});
async function fixture(t,execute=async()=>{}){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-hotkeys-'));t.after(()=>fs.rm(root,{recursive:true,force:true}));
 const callbacks=new Map(),occupied=new Set();
 const shortcuts={register:(key,fn)=>{if(occupied.has(key))return false;callbacks.set(key,fn);return true;},unregister:key=>callbacks.delete(key)};
 const manager=new Hotkeys(path.join(root,'hotkeys.json'),shortcuts,execute);await manager.init();manager.setActions([{id:'node/play',label:'Player'},{id:'app.show',label:'Mostrar'}]);t.after(()=>manager.close());return {manager,callbacks,occupied,shortcuts};
}
test('normalizes chords, rejects reserved keys, typing and conflicting scopes',()=>{
 assert.equal(accelerator('shift+CTRL+f8'),'Control+Shift+F8');assert.equal(accelerator('F24'),'F24');
 for(const key of ['win+r','Alt+F4','Alt+Shift+F4','Control+Alt+Tab','F12','Ctrl+Alt+Delete','ctrl+s','ctrl+ctrl+x','a','shift+a','Mouse4'])assert.throws(()=>accelerator(key));
 assert.throws(()=>validate(config([binding(),binding({id:'two',scope:'local',accelerator:'SHIFT+CONTROL+F8'})])),/duplicada/);
 assert.throws(()=>validate(config([binding({value:NaN})])),/Valor/);
});
test('persists bindings, reports conflict and releases registrations on suspend, orphaning and shutdown',async t=>{
 const {manager:m,callbacks,occupied,shortcuts}=await fixture(t);
 await m.save(config([binding()]));assert.equal(callbacks.size,1);
 m.suspend(true);assert.equal(callbacks.size,0);m.suspend(false);assert.equal(callbacks.size,1);
 m.setActions([{id:'app.show',label:'Mostrar'}]);assert.equal(callbacks.size,0);assert.match(m.states.get('one'),/ausente/);
 m.setActions([{id:'node/play',label:'Player'}]);assert.equal(callbacks.size,1);
 m.close();assert.equal(callbacks.size,0);
 const restored=new Hotkeys(m.file,shortcuts,async()=>{});await restored.init();assert.equal(restored.config.bindings[0].action,'node/play');
 occupied.add('Control+Shift+F8');restored.setActions([{id:'node/play',label:'Player'}]);assert.match(restored.states.get('one'),/Conflito/);restored.suspend(true);assert.match(restored.states.get('one'),/Conflito/);restored.close();
});
test('does not queue delayed actions, reports errors and ignores local autorepeat',async t=>{
 let finish,count=0;const {manager:m}=await fixture(t,()=>{count++;return new Promise(resolve=>{finish=resolve;});});
 await m.save(config([binding({scope:'local'})]));
 assert.equal(m.local({type:'keyDown',key:'F8',control:true,shift:true,isAutoRepeat:true}),false);
 assert.equal(m.local({type:'keyDown',key:'F8',control:true,shift:true}),true);
 await m.fire('one',{test:true});assert.equal(count,1);assert.match(m.lastResult.message,/andamento/);finish();await new Promise(setImmediate);
 m.execute=async()=>{throw Error('Arquivo ausente');};assert.equal(await m.fire('one',{test:true}),false);assert.match(m.lastResult.message,/Arquivo ausente/);
});
test('failed write keeps old active bindings and corrupt preferences are preserved',async t=>{
 const {manager:m,callbacks}=await fixture(t);await m.save(config([binding()]));
 await fs.mkdir(m.file+'.tmp');await assert.rejects(m.save(config([])));assert.equal(callbacks.size,1);assert.equal(m.config.bindings.length,1);await fs.rmdir(m.file+'.tmp');
 m.close();await fs.writeFile(m.file,'invalid json');await m.init();assert.match(m.error,/preservado/);await m.save(config([]));
 const files=await fs.readdir(path.dirname(m.file));assert.ok(files.some(f=>f.startsWith('hotkeys.json.preserved-')));
});
test('actions preserve UUID/plugin identity; exclude momentary/read-only; clamp gain and validate selections',async()=>{
 const {actionsFor,changeField}=await import('../ui/hotkey-actions.mjs');
 const node={id:'a-uuid',block:{kind:'plugin',plugin_id:'org.test',parameters:{'0':0,'1':0,'2':0},bypass:false}};
 const descriptor=()=>({title:'Test',filePlayer:true,fileTransport:true,fields:[{key:'parameter:0',label:'Hold',type:'momentary'},{key:'parameter:1',label:'Readonly',type:'number',readonly:true},{key:'parameter:2',label:'Mode',type:'select',min:0,max:2,step:1,options:[{value:0},{value:2}]},{key:'bypass',label:'Bypass',type:'checkbox'}]});
 const actions=actionsFor({graph:{nodes:[node]}},descriptor);assert.ok(actions.some(a=>a.id==='a-uuid/org.test/player.toggle'));assert.ok(!actions.some(a=>['parameter:0','parameter:1'].includes(a.field)));
 const set=actions.find(a=>a.operation==='set');assert.throws(()=>changeField(node,set,1));changeField(node,set,2);assert.equal(node.block.parameters['2'],2);
 const gain={block:{gain_db:23}};changeField(gain,{field:'gain_db',operation:'adjust',min:-60,max:24,step:0.1},3);assert.equal(gain.block.gain_db,24);
 // A visual slider step of 1 is not a CLAP stepped/integer contract.
 changeField(node,{field:'parameter:2',operation:'set',min:0,max:1000,step:1,stepped:false},1.25);assert.equal(node.block.parameters['2'],1.25);
 assert.throws(()=>changeField(node,{field:'parameter:2',operation:'set',min:0,max:1000,stepped:true},1.25));
 assert.equal(actionsFor({graph:{nodes:[node]},missing_plugins:[{node_id:node.id}]},descriptor).some(a=>a.nodeId),false);
});

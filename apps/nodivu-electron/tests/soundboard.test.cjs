const {test}=require('node:test');
const assert=require('node:assert/strict');
const {pathToFileURL}=require('node:url');const path=require('node:path');
const modulePath=pathToFileURL(path.join(__dirname,'../ui/soundboard.mjs')).href;
const actionsPath=pathToFileURL(path.join(__dirname,'../ui/hotkey-actions.mjs')).href;
const {validate,Hotkeys}=require('../hotkeys.cjs');
test('bank resources preserve missing plugins and explicit clears, with separate slot identities',async()=>{
 const {bankResources,resourceKey}=await import(modulePath),node={id:'bank'},saved=new Map([['bank:0','one.wav'],['bank:1','two.wav']]);
 assert.equal(bankResources(node,{},saved).length,2);
 assert.deepEqual(bankResources(node,{bank:[{assigned:true,file:''},{assigned:true,file:'new.wav'}]},saved),[{node_id:'bank',slot:1,path:'new.wav'}]);
 assert.equal(resourceKey({node_id:'bank',slot:0}),'bank:0');assert.equal(resourceKey({node_id:'bank'}),'bank');
});
test('slot readiness is independent; clear does not resurrect saved files and errors explain limits',async()=>{
 const {bankPresentation:show}=await import(modulePath),node={id:'bank',block:{kind:'plugin',source:true}},devices=[{endpoint_id:'out',flow:'render',state:'active'}];
 const snapshot={engine_state:'running',graph:{nodes:[node,{id:'out',block:{kind:'output',endpoint_id:'out'}}],edges:[{from:'bank',to:'out'}]},plugin_states:{bank:{bank:[{assigned:true,file:'one.wav',status:2},{assigned:true,file:'two.wav',status:1},{assigned:true,file:'',status:0}]}}};
 const context={snapshot,node,devices,online:true,busy:false};
 assert.equal(show({...context,slot:0}).canPlay,true);assert.equal(show({...context,slot:1}).canPlay,false);
 assert.equal(show({...context,slot:2,savedPath:'old.wav'}).needsLoad,false);
 snapshot.plugin_states.bank.bank[0].status=-1;assert.match(show({...context,slot:0}).reason,/60 segundos/);
});
test('ten stable trigger actions and preset 1 through 0 preserve other bindings and reject conflicts',async()=>{
 const {actionsFor}=await import(actionsPath),{bankBindings}=await import(modulePath);
 const actions=actionsFor({graph:{nodes:[{id:'bank',block:{kind:'plugin',plugin_id:'org.nodivu.soundboard'}}]}},()=>({title:'Soundboard',fileBankSlots:10,fields:[]}));
 const triggers=actions.filter(a=>a.operation==='bank.trigger');assert.equal(triggers.length,10);assert.equal(new Set(triggers.map(a=>a.id)).size,10);
 let id=0;const next=bankBindings({version:1,enabled:true,bindings:[]},triggers.map(({id,label,nodeId})=>({id,label,nodeId})),'bank',()=>String(++id));
 validate(next);assert.equal(next.bindings[9].accelerator,'Control+Shift+0');assert.equal(bankBindings(next,actions,'bank',()=>String(++id)).bindings.length,10);
 const conflict={...next.bindings[0],id:'other',action:'app.show'};
 assert.throws(()=>validate(bankBindings({...next,bindings:[conflict]},actions,'bank',()=>String(++id))),/duplicada/);
});
test('near-simultaneous soundboard hotkeys are serialized, not dropped',async()=>{
 const seen=[];let release;const gate=new Promise(resolve=>release=resolve);
 const service=new Hotkeys('unused',{register:()=>true,unregister(){}},async b=>{seen.push(b.id);if(b.id==='a')await gate;});
 service.setActions(['a','b'].map(id=>({id:`node/plugin/bank.${id==='a'?0:1}.trigger`,label:id})));
 service.config={version:1,enabled:true,bindings:['a','b'].map((id,i)=>({id,action:`node/plugin/bank.${i}.trigger`,label:id,enabled:true,scope:'global',accelerator:'F'+(23+i)}))};
 const first=service.fire('a');await service.fire('b');release();await first;assert.deepEqual(seen,['a','b']);service.close();
});

test('queued soundboard gesture expires instead of playing after a stall',async()=>{
 const seen=[];let release;const gate=new Promise(resolve=>release=resolve);
 const service=new Hotkeys('unused',{register:()=>true,unregister(){}},async b=>{seen.push(b.id);if(b.id==='a')await gate;});
 service.setActions(['a','b'].map((id,i)=>({id:`node/plugin/bank.${i}.trigger`,label:id})));
 service.config={version:1,enabled:true,bindings:['a','b'].map((id,i)=>({id,action:`node/plugin/bank.${i}.trigger`,label:id,enabled:true,scope:'global',accelerator:'F'+(23+i)}))};
 const first=service.fire('a');await service.fire('b');await new Promise(r=>setTimeout(r,280));release();await first;assert.deepEqual(seen,['a']);service.close();
});

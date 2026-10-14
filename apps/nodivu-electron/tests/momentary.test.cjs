const {test}=require('node:test'),assert=require('node:assert/strict');
test('hold renews without document edits, releases once and recovers after failed IPC',async()=>{
 const {createHold}=await import('../ui/momentary.mjs');const calls=[];let tick,cancelled=0;
 const hold=createHold(async value=>calls.push(value),{schedule:fn=>(tick=fn,1),cancel:()=>cancelled++});
 hold.press();hold.press();await new Promise(setImmediate);await tick();hold.release();hold.release();
 assert.deepEqual(calls,[true,true,false]);assert.equal(cancelled,1);assert.equal(hold.pressed,false);
 const failure=createHold(async()=>{throw Error('offline')},{schedule:()=>1,cancel:()=>{}});failure.press();await new Promise(setImmediate);assert.equal(failure.pressed,false);
});
test('sidechain labels, two sockets and transient widget are exposed by the descriptor',async()=>{
 const {pluginDescriptor,missingPluginDescriptor}=await import('../ui/blocks/plugin.js');
 const d=pluginDescriptor({id:'test',inputs:[0,1],input_labels:['Áudio','Referência'],parameters:[{id:2,min:0,max:1,default:0,stepped:true}],appearance:{title:'Teste',controls:[{parameter:2,widget:'momentary',label:'Segurar'}]}});
 assert.equal(d.inputs,2);assert.equal(d.inputLabels[1],'Referência');assert.equal(d.fields[0].type,'momentary');assert.equal(missingPluginDescriptor({block:{sidechain:true}}).inputs,2);
});

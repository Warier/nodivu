const {test}=require('node:test');
const assert=require('node:assert/strict');
const {assertExecution}=require('../hotkey-execution.cjs');
test('delayed and compound shortcut commands cannot dispatch after expiry or replacement',async()=>{
 let pending={token:'original',deadline:1000},now=500,mutations=0;
 const execution={...pending};
 const dispatch=()=>{assertExecution(execution,pending,now);mutations++;};
 dispatch();assert.equal(mutations,1);
 now=1000;assert.throws(dispatch,/Etapas já iniciadas/);assert.equal(mutations,1);
 now=500;pending={token:'next',deadline:2000};assert.throws(dispatch,/expirou/);
 pending=null;assert.throws(dispatch,/expirou/);
 assert.doesNotThrow(()=>assertExecution(undefined,null,500),'ordinary UI is independent');
});

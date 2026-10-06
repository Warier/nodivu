const {test}=require('node:test');const assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {ProjectRecovery}=require('../project-recovery.cjs');
const {writeProject}=require('../project-files.cjs');
async function fixture(fn){const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-recovery-'));try{await fn(dir);}finally{await fs.rm(dir,{recursive:true,force:true});}}
const document=n=>({format:'nodivu-project',version:1,graph:{nodes:[],edges:[]},positions:[],camera:{x:n,y:0,scale:1},resources:[]});
test('a new process protects the draft, restore retains it until explicit save',()=>fixture(async dir=>{
 const first=new ProjectRecovery(dir);await first.init();await first.save(document(1));
 const next=new ProjectRecovery(dir);assert.equal((await next.init()).pending,true);
 await assert.rejects(next.save(document(2)),/anterior/);await next.clear();
 assert.deepEqual(JSON.parse(await next.read()),document(1));next.accept();
 await next.save(document(3));assert.deepEqual(JSON.parse(await next.read()),document(3));
 await next.clear();assert.equal((await new ProjectRecovery(dir).init()).available,false);
}));
test('failed replacement preserves last complete copy and subsequent writes recover',()=>fixture(async dir=>{
 let fail=false;const store=new ProjectRecovery(dir,async(file,data)=>{if(fail)throw new Error('disk failure');await writeProject(file,data);});
 await store.save(document(1));fail=true;await assert.rejects(store.save(document(2)),/disk failure/);
 assert.deepEqual(JSON.parse(await store.read()),document(1));fail=false;await store.save(document(3));assert.equal(store.status().error,'');
}));
test('queued clear cannot be overtaken by an older autosave',()=>fixture(async dir=>{
 let release;const gate=new Promise(r=>release=r);const store=new ProjectRecovery(dir,async(f,p)=>{await gate;await writeProject(f,p);});
 const saving=store.save(document(1));const clearing=store.clear();release();await saving;await clearing;
 assert.equal(store.status().available,false);await assert.rejects(fs.stat(store.file),{code:'ENOENT'});
}));
test('corrupt or oversized recovery is preserved and requires explicit discard',()=>fixture(async dir=>{
 for(const data of ['{broken','x'.repeat(65537)]){await fs.writeFile(path.join(dir,'recovery.nodivu.json'),data);const store=new ProjectRecovery(dir);const state=await store.init();assert.equal(state.pending,true);assert.ok(state.error);await assert.rejects(store.save(document(1)));assert.equal(await fs.readFile(store.file,'utf8'),data);await store.clear(true);}
}));

test('backend fault protects the current checkpoint against blank reconnect state',()=>fixture(async dir=>{
 const store=new ProjectRecovery(dir);await store.save(document(9));assert.equal(store.protect().pending,true);
 await store.clear();await assert.rejects(store.save(document(0)));assert.deepEqual(JSON.parse(await store.read()),document(9));
 store.accept();await store.clear();assert.equal(store.status().available,false);
}));

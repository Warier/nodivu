const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),http=require('node:http');
const {PluginCatalog,validateCatalog}=require('../plugin-catalog.cjs');
const {PluginManager}=require('../plugin-manager.cjs');
const {TestRepository}=require('./helpers/catalog-repo.cjs');
async function fixture(t){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-catalog-')),repo=new TestRepository();
 const server=http.createServer((req,res)=>repo.handler(req,res));await new Promise(r=>server.listen(0,'127.0.0.1',r));
 t.after(async()=>{server.closeAllConnections();await new Promise(r=>server.close(r));await fs.rm(root,{recursive:true,force:true});});
 const source=repo.source('http://127.0.0.1:'+server.address().port);
 const catalog=new PluginCatalog(root,source,{allowLoopback:true});await catalog.init();
 const manager=new PluginManager(path.join(root,'plugins'),path.join(root,'archive'));await manager.init();
 return {root,repo,server,source,catalog,manager};
}

test('cancel interrupts package transfer and never installs a partial archive',async t=>{
 const {repo,server,catalog,manager}=await fixture(t);const item=repo.package('org.nodivu.cancel');repo.publish();
 const original=repo.handler.bind(repo);let pendingResponse;
 repo.handler=(req,res)=>{if(req.url==='/targets/'+item.archive){const data=repo.files.get(req.url);res.writeHead(200,{'Content-Length':data.length});res.write(data.subarray(0,64));pendingResponse=res;}else original(req,res);};
 catalog.beginOperation();const prepared=catalog.prepare(item.id,item.version);const rejected=assert.rejects(prepared,/cancel|abort/i);
 for(let i=0;i<200&&!pendingResponse;i++)await new Promise(r=>setTimeout(r,10));
 assert.ok(pendingResponse,'download deve começar');
 catalog.cancel();await rejected;catalog.finishOperation();server.closeAllConnections();
 assert.equal((await manager.list()).entries.length,0);assert.equal(catalog.operationStatus().active,false);
});

test('cache cleanup retains TUF trust, pending versions and rollback archives',async t=>{
 const {repo,catalog,manager}=await fixture(t);const item=repo.package('org.nodivu.cache');repo.publish();
 let p=await catalog.prepare(item.id,item.version);try{await manager.install(p.folder);}finally{await p.cleanup();}
 const key=(await manager.list()).entries[0].key;
 repo.package(item.id,'2.0.0');repo.publish();p=await catalog.prepare(item.id,'2.0.0');try{await manager.install(p.folder);}finally{await p.cleanup();}
 const j=(await manager.versions.journals())[0],root=await fs.readFile(path.join(catalog.metadataDir,'root.json'));
 const orphan='local-11111111-1111-1111-1111-111111111111';await fs.mkdir(path.join(manager.versions.staging,orphan));
 await fs.mkdir(path.join(manager.archive,'preserved'),{recursive:true});
 const removed=await require('../plugin-cache.cjs').clearPluginCache(catalog,manager);assert.ok(removed>0);
 assert.deepEqual(await fs.readFile(path.join(catalog.metadataDir,'root.json')),root);
 await fs.access(path.join(manager.versions.staging,j.stage));await fs.access(path.join(manager.archive,'preserved'));
 await assert.rejects(fs.access(path.join(manager.versions.staging,orphan)));
 assert.equal((await manager.list()).entries[0].metadata.version,'1.0.0');await manager.init();assert.equal((await manager.list()).entries[0].metadata.version,'2.0.0');
 assert.equal(catalog.snapshot().online,false);assert.equal((await catalog.refresh()).online,true);
 assert.equal((await manager.list()).entries[0].key,key);
});
test('signed catalog discovers two plugins, verifies docs and updates without rebuilding app',async t=>{
 const {repo,catalog,manager}=await fixture(t);repo.package('org.nodivu.test');repo.package('com.example.test');repo.publish();
 let state=await catalog.refresh();assert.equal(state.online,true,state.error);assert.equal(state.entries.length,2);assert.equal(state.entries[0].official,true);assert.equal(state.entries[1].official,false);
 assert.match((await catalog.details('org.nodivu.test')).markdown,/# Plugin/);assert.match((await catalog.image('org.nodivu.test','logo.png')).url,/^data:image\/png/);
 let prepared=await catalog.prepare('org.nodivu.test','1.0.0');try{await manager.install(prepared.folder);}finally{await prepared.cleanup();}
 repo.package('org.nodivu.test','2.0.0');repo.publish();state=await catalog.refresh();assert.equal(state.entries.find(e=>e.id==='org.nodivu.test').version,'2.0.0');
 await assert.rejects(catalog.prepare('org.nodivu.test','1.0.0'),/versão.*mudou/);
 prepared=await catalog.prepare('org.nodivu.test','2.0.0');try{await manager.install(prepared.folder);}finally{await prepared.cleanup();}
 assert.equal((await manager.list()).entries[0].metadata.version,'1.0.0');await manager.init();assert.equal((await manager.list()).entries[0].metadata.version,'2.0.0');
});
test('tampered binaries fail hash verification; offline cache only permits browsing',async t=>{
 const {repo,catalog,root,source,server}=await fixture(t);const item=repo.package('com.example.test');repo.publish();assert.equal((await catalog.refresh()).online,true);
 await catalog.details(item.id);
 const archive='/targets/'+item.archive,corrupt=Buffer.from(repo.files.get(archive));corrupt[50]^=1;repo.files.set(archive,corrupt);
 await assert.rejects(catalog.prepare(item.id,item.version),/hash|Hash|SHA|verify/i);
 server.closeAllConnections();await new Promise(r=>server.close(r));
 const offline=new PluginCatalog(root,source,{allowLoopback:true});await offline.init();assert.equal(offline.snapshot().entries.length,1);assert.equal(offline.snapshot().online,false);
 assert.match((await offline.details(item.id)).markdown,/# Plugin/);await assert.rejects(offline.prepare(item.id,item.version),/verificar/);
});
test('TUF rejects bad signatures, expired metadata and rollback',async t=>{
 const {repo,catalog}=await fixture(t);repo.package('com.example.test');repo.publish();const old=repo.files.get('/metadata/timestamp.json');assert.equal((await catalog.refresh()).online,true);
 repo.package('com.example.test','2.0.0');repo.publish();assert.equal((await catalog.refresh()).online,true);
 repo.files.set('/metadata/timestamp.json',old);assert.equal((await catalog.refresh()).online,false);
 repo.publish();const metadata=JSON.parse(repo.files.get('/metadata/timestamp.json'));metadata.signed.version+=1;repo.files.set('/metadata/timestamp.json',Buffer.from(JSON.stringify(metadata)));assert.equal((await catalog.refresh()).online,false);
 repo.publish('2000-01-01T00:00:00Z');assert.equal((await catalog.refresh()).online,false);
});
test('catalog schema bounds paths and identities; HTTP requires explicit test injection',async t=>{
 const {repo,root,source}=await fixture(t);const item=repo.package('com.example.test');
 assert.throws(()=>new PluginCatalog(root,source),/HTTPS/);
 for(const changed of [{archive:'../x.zip'},{id:'x/y'},{download_bytes:300*1024*1024},{images:{'../x':'x.png'}},{logo:'x.svg'}])assert.throws(()=>validateCatalog({schema_version:1,plugins:[{...item,...changed}]}));
 assert.throws(()=>validateCatalog({schema_version:1,plugins:[item,item]}),/duplicado/);
 assert.equal(validateCatalog({schema_version:1,plugins:[{...item,official:true}]} )[0].official,false);
});

test('verified archive with the real bundled player loads through the native backend',async t=>{
 const {repo,catalog,manager}=await fixture(t);
 const library=await fs.readFile(path.resolve(__dirname,'../../../.local/plugins/mp3/nodivu-mp3.clap'));
 repo.package('org.nodivu.mp3-player','1.0.0',library);repo.publish();
 const prepared=await catalog.prepare('org.nodivu.mp3-player','1.0.0');try{await manager.install(prepared.folder);}finally{await prepared.cleanup();}
 await manager.init();
 const {Engine}=require('../engine.cjs');const engine=new Engine(path.resolve(__dirname,'../../../target/release/nodivu-app-backend.exe'),{...process.env,NODIVU_PLUGIN_MANIFEST:path.join(manager.root,'plugin.json'),NODIVU_SCAN_PLUGINS:'1'});
 try{await engine.request('system.hello');const plugins=await engine.request('plugins.list');assert.ok(JSON.stringify(plugins).includes('org.nodivu.mp3-player'),engine.stderrTail);assert.equal((await engine.request('session.snapshot')).engine_state,'idle');}
 finally{await engine.stop();}
});

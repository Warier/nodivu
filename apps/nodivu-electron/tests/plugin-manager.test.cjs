const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const os=require('node:os');
const path=require('node:path');
const {PluginManager,inspectFolder}=require('../plugin-manager.cjs');
async function fixture(t){
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-manager-'));
 t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const source=path.join(dir,'source');await fs.mkdir(source);
 await fs.writeFile(path.join(source,'effect.clap'),'test fixture, never execute');
 await fs.writeFile(path.join(source,'plugin.json'),JSON.stringify({schema_version:1,plugin_id:'com.example.test',title:'Teste',library:'effect.clap'}));
 const manager=new PluginManager(path.join(dir,'plugins'),path.join(dir,'archive'));await manager.init();
 return {dir,source,manager};
}
test('install is copied, duplicate rejected; enable changes are pending until restart',async t=>{
 const {source,manager,dir}=await fixture(t);
 let state=await manager.install(source);const entry=state.entries[0];assert.equal(entry.pending,true);assert.equal(entry.enabled,true);
 await assert.rejects(manager.install(source),/já está instalado/);
 assert.equal(await fs.readFile(path.join(source,'effect.clap'),'utf8'),'test fixture, never execute');
 const reopened=new PluginManager(manager.root,path.join(dir,'archive'));await reopened.init();
 assert.equal((await reopened.list()).entries[0].pending,false);
 state=await reopened.setEnabled(entry.key,false);assert.equal(state.entries[0].pending,true);
 state=await reopened.setEnabled(entry.key,true);assert.equal(state.entries[0].pending,false);
});
test('removal is deferred, cancellable and archived without deleting files or projects',async t=>{
 const {source,manager,dir}=await fixture(t);const project=path.join(dir,'project.nodivu');await fs.writeFile(project,'saved project');
 const key=(await manager.install(source)).entries[0].key;
 await manager.remove(key);assert.equal((await manager.list()).entries[0].removed,true);
 await manager.cancelRemoval(key);assert.equal((await manager.list()).entries[0].enabled,true);
 await manager.remove(key);await fs.access(path.join(manager.folder(key),'effect.clap'));
 const reopened=new PluginManager(manager.root,path.join(dir,'archive'));await reopened.init();
 assert.equal((await reopened.list()).entries.length,0);
 const archive=await fs.readdir(path.join(dir,'archive'));assert.equal(archive.length,1);
 assert.equal(await fs.readFile(path.join(dir,'archive',archive[0],'effect.clap'),'utf8'),'test fixture, never execute');
 assert.equal(await fs.readFile(project,'utf8'),'saved project');
});
test('invalid manifests remain manageable; bundled packages can only be disabled',async t=>{
 const {manager}=await fixture(t);await fs.writeFile(path.join(manager.root,'plugin.json'),'{broken');
 let row=(await manager.list()).entries[0];assert.ok(row.error);assert.equal(row.protected,true);
 await manager.setEnabled('.',false);await assert.rejects(manager.setEnabled('.',true),/Corrija/);
 await assert.rejects(manager.remove('.'),/acompanha/);
 await fs.writeFile(path.join(manager.root,'plugin.json'),'null');
 assert.match((await manager.list()).entries[0].error,/ID ausente/);
 await assert.rejects(inspectFolder(manager.root),/Manifesto de plugin inválido/);
});

test('archive failure preserves pending removal and does not prevent startup',async t=>{
 const {manager,source,dir}=await fixture(t);const key=(await manager.install(source)).entries[0].key;
 await manager.remove(key);const blocked=path.join(dir,'not-a-directory');await fs.writeFile(blocked,'occupied');
 const reopened=new PluginManager(manager.root,blocked);await reopened.init();
 const state=await reopened.list();assert.equal(state.entries[0].removed,true);assert.equal(state.entries[0].enabled,false);assert.ok(state.warnings.length>=1);
});

test('version update is deferred, preserves disabled state and supports rollback',async t=>{
 const {manager,source}=await fixture(t);
 const meta=v=>fs.writeFile(path.join(source,'nodivu-package.json'),JSON.stringify({schema_version:1,plugin_id:'com.example.test',version:v,author:'Example',resource_tier:'light'}));
 await meta('1.0.0');const key=(await manager.install(source)).entries[0].key;await manager.setEnabled(key,false);
 await meta('2.0.0');await fs.writeFile(path.join(source,'effect.clap'),'version two');
 let state=await manager.install(source);assert.equal(state.entries[0].metadata.version,'1.0.0');assert.equal(state.entries[0].update,'2.0.0');
 await manager.init();state=await manager.list();assert.equal(state.entries[0].metadata.version,'2.0.0');assert.equal(state.entries[0].enabled,false);assert.equal(state.entries[0].previous[0].version,'1.0.0');
 await manager.rollback(key,state.entries[0].previous[0].key);await manager.init();
 assert.equal((await manager.list()).entries[0].metadata.version,'1.0.0');
 assert.equal(await fs.readFile(path.join(manager.folder(key),'effect.clap'),'utf8'),'test fixture, never execute');
});

test('interrupted activation resumes from the journal and canceled update never replaces files',async t=>{
 const {manager,source}=await fixture(t);
 const meta=v=>fs.writeFile(path.join(source,'nodivu-package.json'),JSON.stringify({schema_version:1,plugin_id:'com.example.test',version:v,author:'Example'}));
 await meta('1.0.0');const key=(await manager.install(source)).entries[0].key;
 await meta('2.0.0');await manager.install(source);await manager.cancelUpdate(key);await manager.init();
 assert.equal((await manager.list()).entries[0].metadata.version,'1.0.0');
 await manager.install(source);const j=(await manager.versions.journals())[0];
 await fs.mkdir(manager.archive,{recursive:true});await fs.rename(manager.folder(key),path.join(manager.archive,j.backup));
 await manager.init();assert.equal((await manager.list()).entries[0].metadata.version,'2.0.0');assert.equal((await manager.versions.journals()).length,0);
});

test('README is bounded text and image access stays within the package',async t=>{
 const {manager,source}=await fixture(t);await fs.writeFile(path.join(source,'README.md'),'# Example\n\n| A | B |\n|---|---|\n|1|2|');
 const key=(await manager.install(source)).entries[0].key;assert.match((await manager.details(key)).markdown,/# Example/);
 await assert.rejects(manager.image(key,'../secret.png'),/dentro/);
 await assert.rejects(manager.image(key,'https://example.com/x.png'),/dentro/);
 await assert.rejects(manager.image(key,'effect.clap'),/Imagem inválida/);
});

test('real backend ignores disabled root, disabled children and pending removal before parsing',async t=>{
 const {dir,manager}=await fixture(t);
 for(const key of ['.','broken','worker']){
  const folder=key==='.'?manager.root:path.join(manager.root,key);await fs.mkdir(folder,{recursive:true});
  await fs.writeFile(path.join(folder,key==='worker'?'worker.json':'plugin.json'),'{invalid');
  await fs.writeFile(path.join(folder,key==='worker'?'.nodivu-removed':'.nodivu-disabled'),'');
 }
 const {Engine}=require('../engine.cjs');
 const executable=path.resolve(__dirname,'../../../target/release/nodivu-app-backend.exe');
 const env={...process.env,NODIVU_PLUGIN_MANIFEST:path.join(manager.root,'plugin.json'),NODIVU_SCAN_PLUGINS:'1'};
 const engine=new Engine(executable,env);
 try {assert.equal((await engine.request('system.hello')).protocol_version,1);assert.equal((await engine.request('session.snapshot')).engine_state,'idle');}
 finally {await engine.stop();}
 await fs.unlink(path.join(manager.root,'broken','.nodivu-disabled'));
 const invalid=new Engine(executable,env);
 try {assert.equal((await invalid.request('system.hello')).protocol_version,1); assert.match(invalid.stderrTail,/Plugin indisponível/);}
 finally{await invalid.stop();}
});
test('unsafe library paths and links are rejected before publication',async t=>{
 const {source,manager,dir}=await fixture(t);
 for(const library of ['../outside.clap','C:\\outside.clap','effect.clap:stream']){
  await fs.writeFile(path.join(source,'plugin.json'),JSON.stringify({schema_version:1,plugin_id:'com.example.test',title:'Teste',library}));
  await assert.rejects(manager.install(source),/biblioteca/);
 }
 await fs.writeFile(path.join(source,'plugin.json'),JSON.stringify({schema_version:1,plugin_id:'com.example.test',title:'Teste',library:'effect.clap'}));
 await fs.mkdir(path.join(dir,'outside'));await fs.symlink(path.join(dir,'outside'),path.join(source,'escape'),'junction');
 await assert.rejects(inspectFolder(source),/Links/);
 assert.equal((await manager.list()).entries.length,0);
 await assert.rejects(manager.setEnabled('../outside',false),/não encontrado/);
});
test('ninth package installs disabled and cannot exceed the active type limit',async t=>{
 const {source,manager}=await fixture(t);
 for(let i=0;i<9;i++){
  await fs.writeFile(path.join(source,'plugin.json'),JSON.stringify({schema_version:1,plugin_id:'com.example.test'+i,title:'Teste '+i,library:'effect.clap'}));
  await manager.install(source);
 }
 const rows=(await manager.list()).entries;assert.equal(rows.filter(e=>e.enabled).length,8);
 const disabled=rows.find(e=>!e.enabled);await assert.rejects(manager.setEnabled(disabled.key,true),/oito/);
 await manager.setEnabled(rows.find(e=>e.enabled).key,false);await manager.setEnabled(disabled.key,true);
 assert.equal((await manager.list()).entries.filter(e=>e.enabled).length,8);
});

test('changed staged version is rejected while the current package stays usable',async t=>{
 const {manager,source}=await fixture(t);
 const meta=v=>JSON.stringify({schema_version:1,plugin_id:'com.example.test',version:v,author:'Example'});
 await fs.writeFile(path.join(source,'nodivu-package.json'),meta('1.0.0'));
 const key=(await manager.install(source)).entries[0].key;
 await fs.writeFile(path.join(source,'nodivu-package.json'),meta('2.0.0'));await manager.install(source);
 const j=(await manager.versions.journals())[0];
 await fs.writeFile(path.join(manager.versions.staging,j.stage,'nodivu-package.json'),meta('3.0.0'));
 await manager.init();const state=await manager.list();
 assert.equal(state.entries[0].metadata.version,'1.0.0');assert.match(state.warnings.join(' '),/versão da atualização mudou/);
 await manager.cancelUpdate(key);
});

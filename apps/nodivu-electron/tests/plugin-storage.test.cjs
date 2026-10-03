const {test}=require('node:test');const assert=require('node:assert/strict');
const fs=require('node:fs/promises');const os=require('node:os');const path=require('node:path');
const {preparePlugins}=require('../plugin-storage.cjs');
test('updates replace bundled plugins and preserve external user changes',async()=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-plugin-storage-'));
 try{
  const source=path.join(dir,'app/plugins'),user=path.join(dir,'profile/plugins');
  for(const name of ['mp3','external']){await fs.mkdir(path.join(source,name),{recursive:true});await fs.writeFile(path.join(source,name,'plugin.json'),'v1');}
  await preparePlugins(source,user);await fs.writeFile(path.join(user,'external/plugin.json'),'user edit');
  await fs.writeFile(path.join(source,'mp3/plugin.json'),'v2');await preparePlugins(source,user);
  assert.equal(await fs.readFile(path.join(user,'external/plugin.json'),'utf8'),'user edit');
  assert.equal(await fs.readFile(path.join(user,'mp3/plugin.json'),'utf8'),'v2');
 }finally{await fs.rm(dir,{recursive:true,force:true});}
});

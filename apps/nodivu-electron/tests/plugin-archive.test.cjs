const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const {zip}=require('./helpers/zip.cjs');
const {extractArchive}=require('../plugin-archive.cjs');

test('ZIP imports root or enclosing folder and rejects traversal, aliases, symlinks and corruption',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-zip-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));let i=0;
 async function extract(entries){const n=String(i++),file=path.join(dir,n+'.zip');await fs.writeFile(file,zip(entries));return extractArchive(file,path.join(dir,n));}
 const folder=await extract([{name:'package/plugin.json',data:'{}'},{name:'package/effect.clap',data:'fixture'}]);assert.equal(await fs.readFile(path.join(folder,'effect.clap'),'utf8'),'fixture');
 for(const name of ['../outside','/outside','C:/outside','file:stream','con.txt','folder./x'])await assert.rejects(extract([{name,data:'bad'}]));
 await assert.rejects(extract([{name:'plugin.json',data:'{}'},{name:'PLUGIN.JSON',data:'{}'}]),/duplicado/);
 await assert.rejects(extract([{name:'plugin.json',data:'{}',symlink:true}]),/link/);
 await assert.rejects(extract([{name:'plugin.json',data:'{}',badCRC:true}]),/checksum/);
});

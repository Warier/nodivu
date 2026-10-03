const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const {RecentProjects} = require('../recent-projects.cjs');

test('recentes: limite, ordem, duplicatas, persistência e caminhos escolhidos explicitamente', async () => {
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-recents-'));
  try {
    const file=path.join(dir,'profile','recent-projects.json'),store=new RecentProjects(file);
    assert.deepEqual(await store.list(),[]);
    for(let i=0;i<12;i++)await store.remember(path.join(dir,`project-${i}.json`));
    const list=await new RecentProjects(file).list();assert.equal(list.length,10);assert.equal(list[0],path.join(dir,'project-11.json'));
    const reordered=await store.remember(list[3]);assert.equal(reordered.length,10);assert.equal(reordered[0],list[3]);
    assert.equal(await store.resolve(list[3]),list[3]);
    // Missing project files stay visible; opening, not metadata, reports absence.
    assert.equal(await store.resolve(list[0]),list[0]);
    await assert.rejects(store.resolve(path.join(dir,'unlisted.json')),/não consta/);
    await assert.rejects(store.remember('relative.json'),/inválido/);
    if(process.platform==='win32') {const duplicate=await store.remember(list[3].toUpperCase());assert.equal(duplicate.length,10);}
  } finally {await fs.rm(dir,{recursive:true,force:true});}
});

test('recentes: corrupção e falha de escrita são explícitas e preservam os dados', async () => {
  const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-recents-'));
  try {
    const file=path.join(dir,'recent.json'),store=new RecentProjects(file);
    await fs.writeFile(file,'broken');await assert.rejects(store.list());
    await assert.rejects(store.remember(path.join(dir,'ok.json')));assert.equal(await fs.readFile(file,'utf8'),'broken');
    await fs.writeFile(file,JSON.stringify({version:1,files:['relative.json']}));await assert.rejects(store.list(),/inválida/);
    const blocked=path.join(dir,'blocked');await fs.mkdir(blocked);
    await assert.rejects(new RecentProjects(blocked).remember(path.join(dir,'ok.json')));
  } finally {await fs.rm(dir,{recursive:true,force:true});}
});

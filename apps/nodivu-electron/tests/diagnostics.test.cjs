const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { Diagnostics } = require('../diagnostics.cjs');

test('diagnostics rotates bounded JSONL, deduplicates failures and drains on exit', async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'nodivu-logs-'));
  try {
    const log = new Diagnostics(dir, { maxBytes: 512 });
    for (let i=0;i<12;i++) { log.record('failure', { message: 'erro 0xE000020B', attempt:i }); await log.flush(); }
    log.record('recovered', {message:'MMDevice disponível'});
    log.record('recovered', {message:'MMDevice disponível'});
    await log.flush();
    assert.equal(log.error, null);
    assert.equal(log.pending, 0);
    const files = await fs.readdir(dir);
    assert.deepEqual(files.sort(), ['nodivu.log','nodivu.previous.log']);
    let recovered=0;
    for(const file of files){
      const data=await fs.readFile(path.join(dir,file),'utf8');assert.ok(Buffer.byteLength(data)<=512);
      for(const line of data.trim().split('\n'))if(JSON.parse(line).event==='recovered')recovered++;
    }
    assert.equal(recovered,1);
  } finally { await fs.rm(dir, {recursive:true,force:true}); }
});

test('diagnostics reports unwritable destination and bounds bursts without unhandled rejection', async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'nodivu-logs-'));
  try {
    const file=path.join(dir,'occupied');await fs.writeFile(file,'keep');
    const log=new Diagnostics(file,{maxPending:2});
    for(let i=0;i<100;i++)log.record('failure',{attempt:i});
    assert.ok(log.pending<=2);assert.ok(log.dropped>0);
    await log.flush();assert.ok(log.error);assert.equal(await fs.readFile(file,'utf8'),'keep');
  } finally { await fs.rm(dir,{recursive:true,force:true}); }
});

const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {Diagnostics}=require('../diagnostics.cjs');
const {redactor,exportDiagnostics}=require('../diagnostic-export.cjs');
test('export removes personal paths and aliases IDs consistently while keeping error causes',()=>{
 const redact=redactor(['C:\\Users\\Test Person']);
 const id='f1fb64a2-2110-4a33-a7de-112200003344';
 const result=redact({message:`E_ACCESSDENIED 0x80070005: open 'D:\\private\\speech.wav'; device ${id}`,other:`c:/users/test person/Nodivu, ${id.toUpperCase()}`,count:12});
 assert.doesNotMatch(JSON.stringify(result),/private|speech|Test Person|test person|f1fb64a2/i);
 assert.match(result.message,/E_ACCESSDENIED 0x80070005/);assert.match(result.other,/<id:1>/);assert.equal(result.count,12);
});
test('export is bounded and preserves original logs, including on export failure',async t=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-export-'));t.after(()=>fs.rm(dir,{recursive:true,force:true}));
 const log=new Diagnostics(dir,{maxBytes:512});log.record('error',{message:"ENOENT: open 'C:\\Users\\private\\voice.wav'"});await log.flush();
 const before=await fs.readFile(log.current,'utf8');
 const report=JSON.parse(await exportDiagnostics(log,[]));assert.equal(report.records.length,1);assert.doesNotMatch(JSON.stringify(report),/voice.wav/);
 assert.equal(await fs.readFile(log.current,'utf8'),before);
 await fs.writeFile(log.current,'x'.repeat(513));await assert.rejects(exportDiagnostics(log,[]),/limite/);assert.equal((await fs.stat(log.current)).size,513);
});

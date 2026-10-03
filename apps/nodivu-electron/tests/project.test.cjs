const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {Engine} = require('../engine.cjs');
const {readProject,writeProject,resourcesForFile} = require('../project-files.cjs');
const document = graph => ({format:'nodivu-project',version:1,graph,positions:graph.nodes.map((n,i)=>({node_id:n.id,x:-400+i*320,y:55})),camera:{x:200,y:-100,scale:0.5},resources:[]});
test('projeto real: validação sem mutação, abertura suspensa, IDs ausentes e rejeição atômica',async()=>{
 const e=new Engine(path.resolve(__dirname,'../../../target/release/nodivu-app-backend.exe'));
 try {
  let s=await e.request('node.add',{expected_revision:0,kind:'gain'});
  s=await e.request('node.add',{expected_revision:s.revision,kind:'output'});
  const project=document(s.graph);
  project.graph.nodes[0].block.gain_db=6;
  project.graph.nodes[1].block.endpoint_id='missing-output-preserved';
  project.graph.edges=[{from:project.graph.nodes[0].id,to:project.graph.nodes[1].id}];
  const validate=contents=>e.request('project.validate',{expected_revision:s.revision,contents});
  await validate(JSON.stringify(project));
  assert.equal((await e.request('session.snapshot')).graph.nodes[0].block.gain_db,0);
  await assert.rejects(validate(JSON.stringify(project).replace('"version":1','"version":1,"version":1')),/duplicada/);
  const bad=structuredClone(project);bad.positions[1].node_id=bad.positions[0].node_id;
  await assert.rejects(validate(JSON.stringify(bad)),/duplicada/);
  bad.positions=project.positions;bad.graph.edges.push({from:bad.graph.nodes[0].id,to:bad.graph.nodes[0].id});
  await assert.rejects(validate(JSON.stringify(bad)));
  assert.equal((await e.request('session.snapshot')).revision,s.revision);
  const loaded=await e.request('project.open',{expected_revision:s.revision,contents:JSON.stringify(project)});
  s=loaded.snapshot;
  assert.equal(s.suspended,true);assert.equal(s.generation,0);assert.equal(s.engine_state,'idle');
  assert.equal(s.graph.nodes[1].block.endpoint_id,'missing-output-preserved');
  s.graph.nodes[0].block.gain_db=-6;
  s=await e.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
  s=await e.request('audio.mute',{muted:false});
  assert.equal(s.suspended,true);assert.equal(s.generation,0);
  const incompatible=structuredClone(project);incompatible.version=999;
  await assert.rejects(e.request('project.open',{expected_revision:s.revision,contents:JSON.stringify(incompatible)}));
  assert.deepEqual((await e.request('session.snapshot')).graph,s.graph);
 } finally {await e.stop();}
});
test('arquivo: round-trip, substituição, UTF-8/limite e caminhos relativos',async()=>{
 const dir=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-project-'));
 try {
  const file=path.join(dir,'test.json'), project=document({schema_version:2,nodes:[],edges:[]});
  await writeProject(file,project);assert.deepEqual(JSON.parse(await readProject(file)),project);
  project.camera.scale=2;await writeProject(file,project);assert.equal(JSON.parse(await readProject(file)).camera.scale,2);
  const original=await readProject(file);
  await assert.rejects(writeProject(file,{huge:'x'.repeat(65536)}),/64 KiB/);assert.equal(await readProject(file),original);
  const blocked=path.join(dir,'occupied');await fs.mkdir(blocked);
  await assert.rejects(writeProject(blocked,project));assert.deepEqual((await fs.readdir(dir)).sort(),['occupied','test.json']);
  const bad=path.join(dir,'bad');await fs.writeFile(bad,Buffer.from([0xff]));await assert.rejects(readProject(bad));
  await fs.writeFile(bad,'x'.repeat(65537));await assert.rejects(readProject(bad),/64 KiB/);
  project.resources=[{node_id:'example',path:path.join(dir,'audio','voice.mp3')}];
  const portable=resourcesForFile(project,file,false);assert.equal(portable.resources[0].path,path.join('audio','voice.mp3'));
  assert.deepEqual(resourcesForFile(portable,file,true),project);
 } finally {await fs.rm(dir,{recursive:true,force:true});}
});

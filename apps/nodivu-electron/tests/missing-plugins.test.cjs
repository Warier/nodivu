const {test}=require('node:test'),assert=require('node:assert/strict'),path=require('node:path');
const {Engine}=require('../engine.cjs');
test('real backend preserves missing plugin document/resources and edits surviving nodes',async()=>{
 const env={...process.env};delete env.NODIVU_PLUGIN_MANIFEST;delete env.NODIVU_SCAN_PLUGINS;
 const engine=new Engine(path.resolve(__dirname,'../../../target/release/nodivu-app-backend.exe'),env);
 const id=n=>`00000000-0000-0000-0000-${String(n).padStart(12,'0')}`;
 try{
  const project={format:'nodivu-project',version:1,graph:{schema_version:2,nodes:[
   {id:id(1),block:{kind:'plugin',source:true,consumer:false,plugin_id:'org.test.uninstalled',parameters:{'7':0.42},bypass:false}},
   {id:id(2),block:{kind:'meter'}}],edges:[{from:id(1),to:id(2),from_port:0,to_port:0}]},
   positions:[{node_id:id(1),x:0,y:0},{node_id:id(2),x:300,y:0}],camera:{x:0,y:0,scale:1},resources:[{node_id:id(1),path:'C:/does-not-exist.mp3'}]};
  const opened=await engine.request('project.open',{contents:JSON.stringify(project),expected_revision:0});
  assert.deepEqual(opened.project,project);assert.deepEqual(opened.snapshot.graph,project.graph);
  assert.deepEqual(opened.snapshot.missing_plugins,[{node_id:id(1),plugin_id:'org.test.uninstalled'}]);
  const running=await engine.request('audio.retry');assert.equal(running.last_error,null);assert.equal(running.signal,0);
  const edited=await engine.request('node.add',{kind:'gain',expected_revision:running.revision});
  assert.equal(edited.graph.nodes[0].block.parameters['7'],0.42);assert.deepEqual(edited.graph.edges,project.graph.edges);
  await assert.rejects(engine.request('node.add',{kind:'plugin',plugin_id:'org.test.uninstalled',expected_revision:edited.revision}));
  const graph=structuredClone(edited.graph);graph.nodes=graph.nodes.filter(n=>n.id!==id(1));graph.edges=[];
  const removed=await engine.request('graph.apply',{graph,expected_revision:edited.revision});assert.deepEqual(removed.missing_plugins,[]);
 }finally{await engine.stop();}
});

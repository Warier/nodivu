const {test}=require('node:test');const assert=require('node:assert/strict');const {DemoEngine}=require('../demo-engine.cjs');
test('frontend demo is explicit, silent, independent and validates graph references',async()=>{
 const a=new DemoEngine(),b=new DemoEngine();assert.equal((await a.request('system.hello')).demo,true);
 let s=await a.request('node.add',{kind:'gain',expected_revision:0});s=await a.request('node.add',{kind:'gain',expected_revision:s.revision});
 assert.equal((await b.request('session.snapshot')).graph.nodes.length,0);
 const graph=structuredClone(s.graph);graph.edges=[{from:graph.nodes[0].id,to:graph.nodes[1].id},{from:graph.nodes[1].id,to:graph.nodes[0].id}];
 await assert.rejects(a.request('graph.apply',{graph,expected_revision:s.revision}),/Ciclo/);
 assert.equal((await a.request('session.snapshot')).revision,s.revision);assert.equal(s.metrics,null);assert.equal(s.engine_state,'idle');await a.stop();await assert.rejects(a.request('session.snapshot'));
});
test('demo project roundtrip preserves capture intent without starting a device',async()=>{
 const e=new DemoEngine();let s=await e.request('node.add',{kind:'plugin',plugin_id:'org.nodivu.windows-audio'});
 const node_id=s.graph.nodes[0].id,selection={mode:'system'};s=await e.request('capture.configure',{node_id,selection});
 const project={format:'nodivu-project',version:1,graph:s.graph,positions:[{node_id,x:0,y:0}],camera:{x:0,y:0,scale:1},resources:[],captures:[{node_id,selection}]};
 const result=await e.request('project.open',{contents:JSON.stringify(project),expected_revision:s.revision});assert.deepEqual(result.snapshot.plugin_states[node_id].capture_selection,selection);assert.equal(result.snapshot.engine_state,'idle');
});
test('capture status exposes waiting, ambiguity and native errors',async()=>{
 const {captureMessage}=await import('../ui/capture-source.mjs');const selected={mode:'system'};
 assert.match(captureMessage({status:3},selected),/selecione novamente/);assert.match(captureMessage({status:4},selected),/várias/);assert.match(captureMessage({status:5,error:-2147024891},selected),/80070005/);assert.equal(captureMessage({control_error:'Falha de controle'},selected),'Falha de controle');
});

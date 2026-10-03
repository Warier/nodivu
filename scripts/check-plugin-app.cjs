// Opt-in: saída WASAPI real e DLL local confiável. Não altera configuração do Windows.
const assert=require('node:assert/strict');
const path=require('node:path');
const fs=require('node:fs');
const {Engine}=require('../apps/nodivu-electron/engine.cjs');
const root=path.resolve(__dirname,'..');
const manifest=process.env.NODIVU_PLUGIN_MANIFEST||path.join(root,'.local/plugins/plugin.json');
const scale=Number(process.env.NODIVU_PLUGIN_TEST_SCALE||0.5);assert.ok([0.5,0.25].includes(scale));
const engine=new Engine(path.join(root,'target/release/nodivu-app-backend.exe'),{
  ...process.env,NODIVU_PLUGIN_MANIFEST:manifest
});
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
  const catalog=await engine.request('plugins.list');assert.equal(catalog.plugins.length,1);
  assert.equal(catalog.plugins[0].appearance.title,JSON.parse(fs.readFileSync(manifest,'utf8')).title);
  let s=await engine.request('session.snapshot');
  async function add(kind){s=await engine.request('node.add',{expected_revision:s.revision,kind,...(kind==='plugin'?{plugin_id:catalog.plugins[0].id}:{})});return s.graph.nodes.at(-1).id;}
  async function edit(fn){const graph=structuredClone(s.graph);fn(graph);s=await engine.request('graph.apply',{expected_revision:s.revision,graph});}
  async function settled(){await wait(450);s=await engine.request('session.snapshot');assert.equal(s.engine_state,'running');assert.equal(s.last_error,null);return s;}
  const tone=await add('tone'),gain=await add('gain'),plugin=await add('plugin'),output=await add('output');
  const devices=(await engine.request('devices.list')).devices;
  const selected=devices.find(d=>d.flow==='render'&&d.state==='active'&&d.is_default);
  assert.ok(selected,'Saída padrão ativa exigida');
  await edit(g=>{g.nodes.find(n=>n.id===output).block.endpoint_id=selected.endpoint_id;g.nodes.find(n=>n.id===gain).block.gain_db=-6;g.edges=[{from:tone,to:gain},{from:gain,to:plugin},{from:plugin,to:output}];});
  await settled();const generation=s.generation;const samples=[];
  function checkRatio(expected){const m=s.metrics.latency_diagnostics.external_effect;assert.ok(m.input_peak>0);assert.ok(Math.abs(m.output_peak/m.input_peak-expected)<0.001);assert.equal(s.generation,generation);assert.equal(s.metrics.processor_errors,0);samples.push({expected,snapshot:s});}
  checkRatio(scale);
  await edit(g=>g.nodes.find(n=>n.id===plugin).block.parameters['7']=0.25);await settled();checkRatio(scale*0.25);
  await edit(g=>g.nodes.find(n=>n.id===plugin).block.bypass=true);await settled();checkRatio(1);
  const revision=s.revision;const bad=structuredClone(s.graph);bad.nodes.find(n=>n.id===plugin).block.parameters['7']=2;
  await assert.rejects(engine.request('graph.apply',{expected_revision:revision,graph:bad}),/parâmetro/);
  assert.equal((await engine.request('session.snapshot')).revision,revision);
  bad.nodes.find(n=>n.id===plugin).block.parameters['7']=0.25;bad.edges=[{from:tone,to:plugin},{from:plugin,to:gain},{from:gain,to:output}];
  s=await engine.request('graph.apply',{expected_revision:revision,graph:bad});await settled();checkRatio(1);
  await edit(g=>{g.edges=g.edges.filter(e=>e.from!==plugin);});await settled();assert.equal(s.metrics.output_peak,0);assert.equal(s.generation,generation);
  await edit(g=>{g.nodes=g.nodes.filter(n=>n.id!==plugin);g.edges=[{from:tone,to:gain},{from:gain,to:output}];});await settled();assert.ok(s.metrics.output_peak>0);assert.equal(s.generation,generation);
  const fresh=await add('plugin');await edit(g=>{g.edges=[{from:tone,to:gain},{from:gain,to:fresh},{from:fresh,to:output}];});await settled();checkRatio(scale);
  // Mixed chain: plugin -> gain -> plugin -> gain, with independent settings.
  const second=await add('plugin'),gain2=await add('gain');
  const dbHalf=20*Math.log10(0.5), dbDouble=20*Math.log10(2);
  await edit(g=>{
    g.nodes.find(n=>n.id===gain).block.gain_db=dbHalf;
    g.nodes.find(n=>n.id===gain2).block.gain_db=dbHalf;
    g.edges=[{from:tone,to:fresh},{from:fresh,to:gain},{from:gain,to:second},{from:second,to:gain2},{from:gain2,to:output}];
  });
  async function checkOutput(factor,label){await settled();const actual=s.metrics.output_peak;const expected=Math.pow(10,-36/20)*factor;
    assert.ok(Math.abs(actual/expected-1)<0.015,`${label}: ${actual} expected ${expected}`);
    assert.equal(s.generation,generation);assert.equal(s.metrics.processor_errors,0);samples.push({label,factor,snapshot:s});}
  await checkOutput(scale*scale*0.25,'two half gains, two plugins');
  await edit(g=>g.nodes.find(n=>n.id===second).block.parameters['7']=0.25);
  await checkOutput(scale*scale*0.25*0.25,'independent parameter');
  await edit(g=>{for(const id of[gain,gain2])g.nodes.find(n=>n.id===id).block.gain_db=dbDouble;});
  await checkOutput(scale*scale*0.25*4,'two double gains');
  await edit(g=>g.nodes.find(n=>n.id===fresh).block.bypass=true);
  await checkOutput(scale*0.25*4,'independent bypass');
  await edit(g=>g.nodes.reverse());await checkOutput(scale*0.25*4,'document reorder preserves identity');
  const many=[fresh,second];for(let i=2;i<8;i++)many.push(await add('plugin'));
  await edit(g=>{for(const n of g.nodes)if(n.block.kind==='plugin'){n.block.bypass=false;n.block.parameters['7']=1;}
    const chain=[tone,...many,output];g.edges=chain.slice(1).map((to,i)=>({from:chain[i],to}));});
  await checkOutput(Math.pow(scale,8),'eight independent instances');
  await assert.rejects(add('plugin'),/oito/);
  await edit(g=>{g.nodes=g.nodes.filter(n=>n.id!==second);const chain=[tone,...many.filter(id=>id!==second),output];g.edges=chain.slice(1).map((to,i)=>({from:chain[i],to}));});
  const reused=await add('plugin');
  await edit(g=>{const chain=[tone,...many.filter(id=>id!==second),reused,output];g.edges=chain.slice(1).map((to,i)=>({from:chain[i],to}));});
  await checkOutput(Math.pow(scale,8),'removed slot reused with fresh state');
  fs.writeFileSync(path.join(root,'.local/plugin-app-evidence.json'),JSON.stringify({catalog,output:selected,generation,samples},null,2));
  console.log('PASS: oito plugins independentes, ordem mista, ganho 50% × 50% = 25% e 200% × 200% = 400%, bypass, remoção/recriação, geração estável.');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(()=>engine.stop());

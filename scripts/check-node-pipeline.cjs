// Opt-in hardware: tom baixo, dois ganhos independentes e edição sem reabrir saída.
const assert = require('node:assert/strict');
const path = require('node:path');
const { Engine } = require('../apps/nodivu-electron/engine.cjs');
async function main() {
  const engine = new Engine(path.resolve(__dirname, '../target/release/nodivu-app-backend.exe'));
  let s;
  const read = async () => s = await engine.request('session.snapshot');
  const nodeMetrics = id => s.metrics?.latency_diagnostics.nodes.find(n => n.node_id === id);
  async function until(check, label) {
    const end = Date.now() + 7000;
    while (Date.now() < end) { await new Promise(r => setTimeout(r, 100)); await read(); if (s.last_error) throw new Error(JSON.stringify(s.last_error)); if (check()) return; }
    throw new Error(label + ': ' + JSON.stringify(s.metrics));
  }
  const edit = async fn => { fn(s.graph); s = await engine.request('graph.apply', { expected_revision:s.revision, graph:s.graph }); };
  try {
    const devices = (await engine.request('devices.list')).devices;
    const output = devices.find(d => d.flow === 'render' && d.state === 'active' && d.is_default);
    assert.ok(output, 'Saída padrão ativa necessária');
    s = await read();
    for (const kind of ['tone','gain','gain','output']) s = await engine.request('node.add', {expected_revision:s.revision,kind});
    const [tone,a,b,out] = s.graph.nodes.map(n=>n.id);
    await edit(g => { g.nodes[1].block.gain_db=-6;g.nodes[2].block.gain_db=-12;g.nodes[3].block.endpoint_id=output.endpoint_id;
      g.edges=[{from:tone,to:a},{from:a,to:b},{from:b,to:out}]; });
    await until(()=>nodeMetrics(a)?.cpu.calls>5 && nodeMetrics(b)?.cpu.calls>5 && s.metrics.output_peak>0, 'Instâncias não processaram');
    const generation=s.generation;
    await until(()=>Math.abs(nodeMetrics(a).output_peak_before_monitor / nodeMetrics(b).output_peak_before_monitor - 10**(12/20))<0.05,'Saídas dos nós não refletem ganhos separados');
    const initial = s.metrics.latency_diagnostics.nodes;
    const callsA=nodeMetrics(a).cpu.calls,callsB=nodeMetrics(b).cpu.calls;
    await edit(g=>g.nodes.reverse());
    await until(()=>nodeMetrics(a)?.cpu.calls>callsA+5 && nodeMetrics(b)?.cpu.calls>callsB+5,'Reordenação reiniciou estado');
    await edit(g=>{g.nodes.find(n=>n.id===b).block.bypass=true;});
    await until(()=>nodeMetrics(b)?.bypass && Math.abs(nodeMetrics(a).output_peak_before_monitor-nodeMetrics(b).output_peak_before_monitor)<0.001,'Bypass não ficou transparente');
    await edit(g=>{g.edges=[{from:tone,to:b},{from:b,to:a},{from:a,to:out}];});
    await until(()=>nodeMetrics(b)?.output_peak_before_monitor > nodeMetrics(a)?.output_peak_before_monitor*1.8,'Ordem dos fios não foi aplicada');
    await edit(g=>{g.edges=[{from:tone,to:a},{from:a,to:out}];});
    await until(()=>nodeMetrics(b)?.connected===false,'Nó desconectado continua na rota');
    const paused=nodeMetrics(b).cpu.calls;
    await new Promise(r=>setTimeout(r,300));await read();assert.equal(nodeMetrics(b).cpu.calls,paused);
    await edit(g=>{g.nodes=g.nodes.filter(n=>n.id!==b);});
    await until(()=>!nodeMetrics(b),'Métrica de nó removido não foi retirada');
    assert.equal(s.generation,generation);assert.equal(s.metrics.processor_errors,0);
    s=await engine.request('audio.mute',{muted:true});
    await until(()=>s.metrics.output_peak===0,'Mudo não funcionou');
    console.log(JSON.stringify({result:'PASS',output:output.name,generation,initial_nodes:initial,final_nodes:s.metrics.latency_diagnostics.nodes,rendered_frames:s.metrics.rendered_frames},null,2));
  } finally { await engine.stop(); }
}
main().catch(e=>{console.error(e);process.exitCode=1;});

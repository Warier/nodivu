const { test } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { Engine } = require('../engine.cjs');
const { spawn } = require('node:child_process');
const executable = path.resolve(__dirname, '../../../target/release/nodivu-app-backend.exe');
// API real, sem abrir streams. Não utiliza backend falso.
test('hello, erro recuperável, snapshot, fila limitada e shutdown', async () => {
  const engine = new Engine(executable);
  try {
    const hello = await engine.request('system.hello');
    assert.equal(hello.protocol_version, 1);
    await assert.rejects(engine.request('session.start'), /desconhecido/);
    const pending = engine.request('session.snapshot');
    await assert.rejects(engine.request('session.snapshot'), /Aguarde/);
    assert.equal((await pending).engine_state, 'idle');
  } finally { await engine.stop(); }
  assert.equal(engine.closed, true);
});
test('executável ausente falha e pode ser encerrado', async () => {
  const engine = new Engine(path.join(__dirname, 'missing-engine.exe'));
  await assert.rejects(engine.request('system.hello'));
  await engine.stop();
  assert.equal(engine.closed, true);
});
test('grafo rejeita ciclos, IDs inválidos e conflitos sem alterar documento', async () => {
  const engine = new Engine(executable);
  try {
    const hello = await engine.request('system.hello');
    assert.equal(hello.backend, 'nodivu-app-backend');
    let s = await engine.request('node.add', { expected_revision: 0, kind: 'gain' });
    s = await engine.request('node.add', { expected_revision: s.revision, kind: 'gain' });
    const before = structuredClone(s.graph);
    s.graph.edges = [{ from: s.graph.nodes[0].id, to: s.graph.nodes[1].id }, { from: s.graph.nodes[1].id, to: s.graph.nodes[0].id }];
    await assert.rejects(engine.request('graph.apply', { expected_revision: s.revision, graph: s.graph }), error=>error.code==='invalid_request');
    assert.deepEqual((await engine.request('session.snapshot')).graph, before);
    await assert.rejects(engine.request('graph.apply', { expected_revision: 0, graph: before }), /mudou/);
    before.nodes[0].id = 'not-an-id';
    await assert.rejects(engine.request('graph.apply', { expected_revision: s.revision, graph: before }));
    assert.equal((await engine.request('session.snapshot')).revision, s.revision);
  } finally { await engine.stop(); }
});
test('limites de blocos e desconexão mantêm backend acessível sem dispositivos', async () => {
  const engine = new Engine(executable);
  try {
    let s = await engine.request('node.add', { expected_revision: 0, kind: 'tone' });
    await assert.rejects(engine.request('node.add', { expected_revision: s.revision, kind: 'tone' }), /Uma instância/);
    s = await engine.request('node.add', { expected_revision: s.revision, kind: 'output' });
    s.graph.edges.push({ from: s.graph.nodes[0].id, to: s.graph.nodes[1].id });
    s = await engine.request('graph.apply', { expected_revision: s.revision, graph: s.graph });
    assert.equal(s.engine_state, 'idle'); assert.equal(s.signal, 0);
    s.graph.edges = [];
    s = await engine.request('graph.apply', { expected_revision: s.revision, graph: s.graph });
    assert.equal(s.generation, 0);
    const bad = structuredClone(s.graph);bad.nodes[1].block.endpoint_id = 'missing-device';
    await assert.rejects(engine.request('graph.apply', { expected_revision: s.revision, graph: bad }), /ativo/);
    assert.equal((await engine.request('session.snapshot')).revision, s.revision);
  } finally { await engine.stop(); }
});
test('morte do proprietário fecha o pipe e encerra seu motor', async () => {
  const parent = spawn(process.execPath, [path.join(__dirname, 'parent-helper.cjs')], { windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'] });
  let pid;
  const alive = () => { try { process.kill(pid, 0); return true; } catch (error) { if (error.code === 'ESRCH') return false; throw error; } };
  try {
    pid = await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('Helper sem resposta')), 5000);
      parent.stdout.once('data', bytes => { clearTimeout(timeout); resolve(Number(bytes.toString().trim())); });
      parent.once('error', reject);
    });
    assert.ok(Number.isSafeInteger(pid) && pid > 0);
    parent.kill();
    const deadline = Date.now() + 3000;
    while (alive() && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 30));
    assert.equal(alive(), false, 'Motor órfão após morte do proprietário');
  } finally { parent.kill(); if (pid && alive()) process.kill(pid); }
});

test('documento legado migra para v2; ramificacoes preservam portas e rejeitam entrada ocupada', async () => {
  const engine=new Engine(executable);
  try {
    let s=await engine.request('session.snapshot');
    for(const kind of ['tone','gain','mixer','meter'])s=await engine.request('node.add',{expected_revision:s.revision,kind});
    const [source,gain,mixer,meter]=s.graph.nodes.map(n=>n.id);
    s.graph.schema_version=1;
    s.graph.edges=[{from:source,to:gain},{from:gain,to:mixer},{from:source,to:mixer,to_port:3},{from:mixer,to:meter}];
    s=await engine.request('graph.apply',{expected_revision:s.revision,graph:s.graph});
    assert.equal(s.graph.schema_version,2);
    assert.deepEqual(s.graph.edges.map(e=>e.to_port),[0,0,3,0]);
    const accepted=structuredClone(s.graph);
    for(const port of [0,4]){
      const graph=structuredClone(accepted);graph.edges.push({from:source,to:mixer,to_port:port});
      await assert.rejects(engine.request('graph.apply',{expected_revision:s.revision,graph}),e=>e.code==='invalid_request');
      assert.deepEqual((await engine.request('session.snapshot')).graph,accepted);
    }
  } finally { await engine.stop(); }
});

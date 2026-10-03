// Ensaio explícito de hardware. Não mede atraso acústico; --capture monitora fila/clocks.
// Mudo ativo por padrão. --audible gera tom ou monitora microfone: usar fones.
const path = require('node:path');
const { Engine } = require('../apps/nodivu-electron/engine.cjs');
const args = process.argv.slice(2);
function value(flag, fallback) { const i = args.indexOf(flag); return i < 0 ? fallback : args[i + 1]; }
async function main() {
  const seconds = Number(value('--seconds', '10'));
  const target = value('--target-ms', '20');
  const period = value('--capture-period', 'default');
  if (!Number.isFinite(seconds) || seconds < 2 || seconds > 1800 || !['10', '15', '20'].includes(target) || !['default','minimum'].includes(period)) throw new Error('Use --seconds entre 2 e 1800, --target-ms 10/15/20 e --capture-period default/minimum');
  // Só o processo de ensaio e seu filho recebem essa configuração.
  process.env.NODIVU_CAPTURE_TARGET_MS = target;
  process.env.NODIVU_CAPTURE_PERIOD = period;
  const engine = new Engine(path.resolve(__dirname, '../target/release/nodivu-app-backend.exe'));
  const capture = args.includes('--capture');
  const audible = args.includes('--audible');
  const samples = [];
  try {
    const devices = (await engine.request('devices.list')).devices;
    const pick = (flow, flag) => {
      const id = value(flag, null);
      const active = devices.filter(d => d.flow === flow && d.state === 'active');
      const device = id ? active.find(d => d.endpoint_id === id) : active.find(d => d.is_default) || active[0];
      if (!device) throw new Error('Dispositivo ativo não encontrado para ' + flow);
      return device;
    };
    const output = pick('render', '--output-id');
    const input = capture ? pick('capture', '--input-id') : null;
    let s = await engine.request('session.snapshot');
    s = await engine.request('audio.mute', { muted: !audible });
    for (const kind of [capture ? 'capture' : 'tone', 'gain', 'output']) s = await engine.request('node.add', { expected_revision: s.revision, kind });
    if (input) s.graph.nodes[0].block.endpoint_id = input.endpoint_id;
    s.graph.nodes[2].block.endpoint_id = output.endpoint_id;
    s.graph.edges = [{ from: s.graph.nodes[0].id, to: s.graph.nodes[1].id }, { from: s.graph.nodes[1].id, to: s.graph.nodes[2].id }];
    s = await engine.request('graph.apply', { expected_revision: s.revision, graph: s.graph });
    const opened = Date.now();
    while (s.engine_state !== 'running') {
      if (s.last_error || Date.now() - opened > 7000) throw new Error('Abertura falhou: ' + JSON.stringify({ input: input?.name, output: output.name, error: s.last_error, metrics: s.metrics }));
      await new Promise(r => setTimeout(r, 100)); s = await engine.request('session.snapshot');
    }
    const generation = s.generation;
    const end = Date.now() + seconds * 1000;
    while (Date.now() < end) {
      await new Promise(r => setTimeout(r, 200));
      s = await engine.request('session.snapshot');
      if (s.engine_state !== 'running' || s.capture_error || s.generation !== generation) throw new Error('Áudio interrompido: ' + JSON.stringify(s));
      samples.push(s.metrics);
    }
    if (!samples.length || !s.metrics.rendered_frames || (capture && !s.metrics.captured_frames) || s.metrics.processor_errors) throw new Error('Ensaio sem frames válidos ou com erro de processador');
    const queue = samples.map(m => m.latency_diagnostics.capture_queue_ms).filter(v => v !== null);
    console.log(JSON.stringify({ result: 'PASS_STREAMING', scope: 'telemetry_not_physical_latency', seconds, audible,
      capture_target_ms: Number(target), capture_period: period, input: input?.name ?? null, output: output.name, generation,
      queue_ms: queue.length ? { min: Math.min(...queue), mean: queue.reduce((a,b)=>a+b,0)/queue.length, max: Math.max(...queue) } : null,
      final: s.metrics }, null, 2));
  } finally { await engine.stop(); }
}
main().catch(e => { console.error(e); process.exitCode = 1; });

// Hardware opt-in: two independent backend processes, never an internal loopback.
// Sender: tone -> gain -> render endpoint. Receiver: capture endpoint -> meter.
// The receiver opens the SAME render endpoint as its clock, but writes silence:
// no edge reaches its output. No microphone/physical speaker/default is changed.
const assert = require('node:assert/strict');
const path = require('node:path');
const {Engine} = require('../apps/nodivu-electron/engine.cjs');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const executable = path.resolve(__dirname,'../target/release/nodivu-app-backend.exe');

async function main() {
  const args=process.argv.slice(2);
  if (!(args.length===1 && args[0]==='--list') && !([4,6].includes(args.length) && args[0]==='--render-id' && args[2]==='--capture-id' && (args.length===4 || args[4]==='--mp3')))
    throw new Error('Uso: node scripts/check-vb-cable.cjs --list OU --render-id "ID de CABLE Input" --capture-id "ID de CABLE Output". Opcional: --mp3 "arquivo.mp3". IDs explícitos obrigatórios; não há fallback.');
  const sender=new Engine(executable,{...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_PLUGIN_MANIFEST:path.resolve(__dirname,'../.local/plugins/plugin.json')});
  let receiver;
  try {
    const devices=(await sender.request('devices.list')).devices;
    if (args[0]==='--list') {
      const {cableInventory}=await import('../apps/nodivu-electron/ui/virtual-cable.mjs');
      console.log(JSON.stringify({diagnostic:cableInventory(devices),devices:devices.filter(d=>d.state==='active').map(({endpoint_id,name,flow,is_default})=>({endpoint_id,name,flow,is_default}))},null,2));
      return;
    }
    const output=devices.find(d=>d.endpoint_id===args[1]&&d.flow==='render'&&d.state==='active');
    const input=devices.find(d=>d.endpoint_id===args[3]&&d.flow==='capture'&&d.state==='active');
    assert.ok(output,'ID de reprodução ausente/inativo: instale/ative CABLE Input e atualize a lista.');
    assert.ok(input,'ID de captura ausente/inativo: instale/ative CABLE Output e atualize a lista.');
    receiver=new Engine(executable);
    const add=async(engine,kinds)=>{let s=await engine.request('session.snapshot');for(const kind of kinds)s=await engine.request('node.add',{expected_revision:s.revision,kind});return s;};
    let tx=await add(sender,['tone','gain','output']);
    let rx=await add(receiver,['capture','meter','output']);
    rx.graph.nodes[0].block.endpoint_id=input.endpoint_id;
    rx.graph.nodes[2].block.endpoint_id=output.endpoint_id;
    rx.graph.edges=[{from:rx.graph.nodes[0].id,to:rx.graph.nodes[1].id}];
    rx=await receiver.request('graph.apply',{expected_revision:rx.revision,graph:rx.graph});
    tx.graph.nodes[2].block.endpoint_id=output.endpoint_id;
    const tone=tx.graph.nodes[0].id,gain=tx.graph.nodes[1].id,out=tx.graph.nodes[2].id;
    // Start with silence, so unrelated cable users fail the baseline instead of
    // being mistaken for the test signal. No PCM or microphone recording is saved.
    tx=await sender.request('graph.apply',{expected_revision:tx.revision,graph:tx.graph});
    async function readRunning(engine) {
      const s=await engine.request('session.snapshot');
      if(s.last_error||s.capture_error)throw new Error(JSON.stringify(s.last_error||s.capture_error));
      assert.equal(s.engine_state,'running','Stream não está em execução');
      return s;
    }
    const deadline=Date.now()+7000;
    while (true) {
      tx=await sender.request('session.snapshot');rx=await receiver.request('session.snapshot');
      if(tx.last_error||rx.last_error||rx.capture_error)throw new Error(JSON.stringify([tx.last_error,rx.last_error,rx.capture_error]));
      if(tx.engine_state==='running'&&rx.engine_state==='running')break;
      if(Date.now()>deadline)throw new Error('Abertura dos dois clientes excedeu sete segundos.');
      await pause(100);
    }
    const generations=[tx.generation,rx.generation];
    async function measure(label) {
      await pause(900); // let bounded driver/host queues and topology fades drain
      const peaks=[];
      for(let i=0;i<6;i++) {
        tx=await readRunning(sender);rx=await readRunning(receiver);
        assert.deepEqual([tx.generation,rx.generation],generations,'Edição reabriu dispositivo');
        assert.equal(rx.metrics.output_peak,0,'Receptor deve escrever apenas silêncio');
        peaks.push(rx.metrics.input_peak);await pause(100);
      }
      peaks.sort((a,b)=>a-b);
      const peak=peaks[3];
      console.log(JSON.stringify({phase:label,received_peak:peak,sent_peak:tx.metrics.output_peak,captured_frames:rx.metrics.captured_frames}));
      return peak;
    }
    const baseline=await measure('baseline');assert.ok(baseline<0.0001,'Cabo já recebe outro áudio; pare os outros emissores e repita.');
    const edit=async fn=>{fn(tx.graph);tx=await sender.request('graph.apply',{expected_revision:tx.revision,graph:tx.graph});};
    await edit(g=>{g.edges=[{from:tone,to:gain},{from:gain,to:out}];});
    const full=await measure('tone');assert.ok(full>0.001,'Tom não chegou ao endpoint de captura: confira o par correto, mute/volume do cabo e formato.');
    await edit(g=>{g.nodes[1].block.gain_db=-6.0206;});
    const half=await measure('gain-half');assert.ok(half/full>0.42&&half/full<0.58,'Ganho no emissor não se refletiu na captura');
    tx=await sender.request('audio.mute',{muted:true});
    assert.ok(await measure('mute')<full*.03,'Silêncio não chegou ao receptor');
    tx=await sender.request('audio.mute',{muted:false});
    assert.ok(await measure('unmute')>full*.42,'Áudio não retornou');
    await edit(g=>{g.edges=[];});assert.ok(await measure('disconnect')<full*.03,'Desconectar não silenciou o cabo');
    tx=await sender.request('node.add',{expected_revision:tx.revision,kind:'mixer'});
    const mixer=tx.graph.nodes.at(-1).id;
    await edit(g=>{g.edges=[{from:tone,to:gain},{from:gain,to:mixer,to_port:0},{from:tone,to:mixer,to_port:1},{from:mixer,to:out}];});
    const mixed=await measure('mixer-fanout');assert.ok(mixed/full>1.42&&mixed/full<1.58,'Soma de 0,5 + 1 no Mixer não chegou ao cabo');
    await edit(g=>{g.edges=[];});assert.ok(await measure('mixer-disconnect')<full*.03);
    if(args[4]==='--mp3') {
      tx=await sender.request('node.add',{expected_revision:tx.revision,kind:'plugin',plugin_id:'org.nodivu.mp3-player'});
      const player=tx.graph.nodes.at(-1).id;
      await edit(g=>{g.edges=[{from:player,to:gain},{from:gain,to:mixer},{from:mixer,to:out}];});
      await sender.request('plugin.command',{node_id:player,action:'load',path:path.resolve(args[5])});
      async function until(predicate,label) {
        const end=Date.now()+10000;
        while(Date.now()<end){await pause(80);tx=await readRunning(sender);rx=await readRunning(receiver);
          assert.deepEqual([tx.generation,rx.generation],generations);assert.equal(rx.metrics.output_peak,0);
          if(tx.plugin_states[player]?.status<0)throw new Error(JSON.stringify(tx.plugin_states[player]));
          if(predicate())return;}
        throw new Error(label);
      }
      await until(()=>tx.plugin_states[player].status===2,'MP3 não carregou');
      for(let play=1;play<=2;play++) {
        await sender.request('plugin.command',{node_id:player,action:'play'});
        await until(()=>rx.metrics.input_peak>0.0001,'MP3 não chegou ao receptor');
        const received=rx.metrics.input_peak;
        await until(()=>tx.plugin_states[player].status===4 && rx.metrics.input_peak<0.0001,'MP3 não terminou em silêncio (use fixture curta)');
        console.log(JSON.stringify({phase:'mp3-through-gain-mixer',play,received_peak:received,ended_silent:true}));
      }
    }
    console.log(JSON.stringify({result:'PASS',render:output.name,capture:input.name,generations,ratio:half/full,description:'Tom, ganho, mute, desconexão e Mixer observados em outro processo. Não mede latência ponta a ponta.'}));
  } finally {
    // Release sender first even if receiver initialization or an assertion fails.
    try {await sender.stop();} finally {if(receiver)await receiver.stop();}
  }
}
main().catch(error=>{console.error(error);process.exitCode=1;});

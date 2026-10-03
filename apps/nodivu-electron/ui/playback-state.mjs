// Presentation of existing engine states. No DSP, file access or implicit start.
export function audioPresentation(snapshot, devices, online, deviceIssue='') {
  if(snapshot?.demo)return {title:'Demonstração visual — sem áudio',message:'Edite blocos, fios e projetos. Dispositivos, captura e reprodução reais exigem o backend Windows.',action:null};
  const output = snapshot?.graph.nodes.find(n => n.block.kind === 'output');
  const endpoint = devices.find(d => d.endpoint_id === output?.block.endpoint_id && d.flow === 'render');
  if (!online) return {title:'Backend desconectado',message:'Reconecte o backend para controlar o áudio. Reconectar inicia um projeto vazio.',action:'reconnect',label:'Reconectar backend'};
  if (deviceIssue) return {title:'Falha ao detectar dispositivos de áudio',message:'Você pode continuar editando e salvando o projeto. Atualize os dispositivos; se acabou de instalar um driver, reinicie o Windows. Detalhes: '+deviceIssue,action:'refresh',label:'Tentar detectar novamente',ready:snapshot?.engine_state==='running'&&!snapshot.muted};
  if (!output?.block.endpoint_id) return {title:'Escolha uma saída',message:'Selecione seus fones ou CABLE Input no bloco Saída de áudio.',action:'output',label:'Selecionar saída'};
  if (!endpoint || endpoint.state !== 'active') return {title:'Saída indisponível',message:'O dispositivo salvo não está ativo. Atualize a lista ou escolha outra saída; não trocamos automaticamente.',action:'output',label:'Revisar saída'};
  if (snapshot.suspended) return {title:'Preparando áudio do projeto…',message:'O áudio e os áudio salvos são preparados automaticamente. Se a preparação falhar, tente novamente.',action:'activate',label:'Tentar preparar novamente'};
  if (snapshot.engine_state === 'starting') return {title:'Abrindo dispositivos…',message:'Aguarde a abertura da saída selecionada.',action:null};
  if (snapshot.engine_state === 'error' || snapshot.last_error) return {title:'Não foi possível abrir o áudio',message:snapshot.last_error?.message || 'Confira os dispositivos e tente novamente.',action:'activate',label:'Tentar ativar novamente'};
  if (snapshot.engine_state !== 'running') return {title:'Áudio indisponível',message:'Não foi possível manter os dispositivos abertos. Confira a seleção e tente novamente.',action:'activate',label:'Tentar abrir novamente'};
  if (snapshot.muted) return {title:'Tudo silenciado',message:'O áudio está ativo, mas o envio foi silenciado.',action:'unmute',label:'Restaurar som'};
  if(snapshot.capture_error)return {title:'Saída ativa · microfone indisponível',message:snapshot.capture_error.message+' O player ainda pode tocar. Confira a entrada e tente reabrir os dispositivos.',action:'activate',label:'Reabrir dispositivos',ready:true};
  if(snapshot.signal===0)return {title:'Saída pronta · conecte os blocos',message:'O dispositivo está aberto. Conecte uma fonte à saída ou a um consumidor; o áudio passa automaticamente quando o caminho estiver pronto.',action:null,ready:true};
  return {title:'Áudio ativo',message:'Destino: '+endpoint.name,action:null,ready:true};
}

function reaches(graph, id, accepts) {
  const pending=[id],seen=new Set();
  while(pending.length){const next=pending.pop();if(seen.has(next))continue;seen.add(next);
    const node=graph.nodes.find(n=>n.id===next);if(!node)continue;
    if(next!==id && accepts(node))return true;
    for(const edge of graph.edges)if(edge.from===next)pending.push(edge.to);
  }
  return false;
}

export function decodeFailure(status) {
  const code=(status>>>0).toString(16).toUpperCase().padStart(8,'0');
  if(code==='800700DF')return 'Este player aceita áudio de até 10 minutos. Escolha um trecho mais curto.';
  if(code==='80070002'||code==='80070003')return 'Arquivo não encontrado. Escolha o áudio novamente.';
  if(code==='80070005')return 'Sem acesso ao arquivo. Escolha um áudio local que possa ser lido.';
  return 'Não foi possível decodificar este áudio (código 0x'+code+'). Tente outro arquivo: até 10 minutos, 256 MiB, mono ou estéreo.';
}

export function playerPresentation({snapshot,node,devices,online,busy,savedPath,error}) {
  const runtime=snapshot.plugin_states?.[node.id];
  const status=runtime?.status??0;
  const file=runtime?.file||savedPath||'';
  const name=file.split(/[\\/]/).pop()||'Nenhum áudio escolhido';
  const labels=['Arquivo ainda não carregado','Carregando áudio…','Arquivo pronto','Reproduzindo','Terminou','Pausado'];
  const fileState=status<0?'Falha ao carregar':labels[status]||'Estado do player desconhecido';
  let reason='';
  if(!online)reason='Backend desconectado. Reconecte para usar o player.';
  else if(error)reason=error;
  else if(status<0)reason=decodeFailure(status);
  else if(status===1)reason='Aguarde a decodificação. Selecionar o arquivo ainda não significa que ele está pronto.';
  else if(![2,3,4,5].includes(status))reason=file?'áudio salvo, ainda não carregado. A preparação começa ao abrir o projeto; se falhar, use Carregar áudio salvo.':'Escolha MP3, WAV ou M4A/AAC local para habilitar o Play.';
  // A ready file alone is insufficient: a Play before the audio worker starts
  // may be reset on stream startup. Explain this instead of accepting a lost Play.
  if(!reason){const audio=audioPresentation(snapshot,devices,online);if(!audio.ready)reason=audio.message;}
  if(!reason && node.block.bypass)reason='O player está em bypass e não gera som. Desmarque Bypass para tocar.';
  const connected=reaches(snapshot.graph,node.id,n=>n.block.kind==='output'||n.block.kind==='meter'||(n.block.kind==='plugin'&&n.block.consumer&&!n.block.bypass));
  if(!reason && !connected)reason='Conecte a saída deste player à saída de áudio, ao Mixer ou a um consumidor conectado.';
  if(!reason && busy)reason='Aguarde a operação atual.';
  const audible=reaches(snapshot.graph,node.id,n=>n.block.kind==='output');
  return {name,file,fileState:status===3&&reason?'Reprodução indisponível':fileState,reason,canPlay:!reason,
    warning:connected&&!audible?'Este caminho chega a um consumidor, mas não à saída de áudio; você não ouvirá nos fones.':'',
    // Transient backend states are never treated as permission to read a path.
    needsLoad:Boolean(file)&&(!runtime?.file||status===0),
  };
}

export function formatTime(ms){const seconds=Math.max(0,Math.floor((Number.isFinite(ms)?ms:0)/1000));return Math.floor(seconds/60)+':'+String(seconds%60).padStart(2,'0');}

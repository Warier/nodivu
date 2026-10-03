// Presentation hints only. Friendly names are editable, so they never prove driver
// identity and never select/persist an endpoint. The user selects an opaque ID.
export const cableNames = Object.freeze({
  render: 'CABLE Input (VB-Audio Virtual Cable)',
  capture: 'CABLE Output (VB-Audio Virtual Cable)',
});

export function cableInventory(devices) {
  const endpoints = devices.filter(d => d.name === cableNames[d.flow]);
  const active = flow => endpoints.filter(d => d.flow === flow && d.state === 'active');
  const render = active('render'), capture = active('capture');
  const state = render.length && capture.length ? 'available' : endpoints.length ? 'incomplete' : 'missing';
  const message = state === 'available'
    ? 'Par com os nomes padrão disponível. Selecione CABLE Input no bloco de saída; disponibilidade não comprova passagem de áudio.'
    : state === 'incomplete'
      ? 'Par incompleto ou inativo. Verifique a instalação e se ambos os dispositivos estão habilitados; depois atualize a lista.'
      : 'Par padrão do VB-CABLE não encontrado. Voicemeeter é outro produto. Veja Instalação e teste abaixo.';
  return {state, message, render, capture};
}

export function outputPresentation(endpoint) {
  const cable = endpoint?.flow === 'render' && endpoint.name === cableNames.render;
  return cable ? {
    title: 'Microfone virtual', subtitle: 'VB-CABLE · envio para outros apps',
    note: 'No outro app, escolha CABLE Output como microfone. Esta barra mede o envio; confirme a recepção no app de destino.',
  } : {
    title: 'Saída de áudio', subtitle: 'Fones ou microfone virtual',
    note: 'Para enviar a outros apps, selecione CABLE Input. Para ouvir localmente, selecione seus fones. Uma saída por projeto.',
  };
}

export function cableRouteMessage(snapshot, devices) {
  const output = snapshot?.graph.nodes.find(n => n.block.kind === 'output');
  const endpoint = devices.find(d => d.endpoint_id === output?.block.endpoint_id);
  if (!endpoint) return 'Destino: selecione CABLE Input no bloco de saída.';
  if (endpoint.name !== cableNames.render) return 'Destino atual: ' + endpoint.name + '. Para VB-CABLE, escolha CABLE Input; cabos renomeados exigem identificação manual.';
  if (endpoint.state !== 'active') return 'Destino salvo indisponível. Nenhum outro dispositivo será escolhido automaticamente.';
  const capture = snapshot.graph.nodes.find(n => n.block.kind === 'capture');
  const input = devices.find(d => d.endpoint_id === capture?.block.endpoint_id);
  if (input?.name === cableNames.capture) return 'Atenção: capturar CABLE Output e devolver ao CABLE Input pode criar retorno. Use seu microfone real como entrada.';
  if (snapshot.suspended) return 'Preparando o áudio do projeto automaticamente. Se houver falha, consulte o aviso acima do canvas.';
  if (snapshot.engine_state !== 'running') return 'CABLE Input selecionado; aguarde a abertura ou confira o erro de dispositivo.';
  if (snapshot.muted) return 'Envio silenciado. Use Restaurar som.';
  return snapshot.signal ? 'Envio ao cabo ativo. Confirme CABLE Output no aplicativo que vai receber.' : 'Cabo aberto em silêncio. Conecte o microfone, player ou Mixer à saída.';
}

const {test} = require('node:test');
const assert = require('node:assert/strict');
const modulePath = require('node:url').pathToFileURL(require('node:path').join(__dirname,'../ui/virtual-cable.mjs')).href;

test('cabo: ausência, par inativo, renomeado e seleção explícita, sem confundir Voicemeeter', async () => {
  const {cableNames, cableInventory, outputPresentation, cableRouteMessage} = await import(modulePath);
  const render={endpoint_id:'render-cable',flow:'render',name:cableNames.render,state:'active'};
  const capture={endpoint_id:'capture-cable',flow:'capture',name:cableNames.capture,state:'active'};
  const vm={...render,name:'Voicemeeter Input (VB-Audio Voicemeeter VAIO)'};
  assert.equal(cableInventory([vm]).state,'missing');
  assert.equal(cableInventory([render,{...capture,state:'disabled'}]).state,'incomplete');
  assert.equal(cableInventory([render,capture]).state,'available');
  assert.equal(cableInventory([{...render,name:'Meu cabo'}]).state,'missing');
  assert.equal(outputPresentation(render).title,'Microfone virtual');
  assert.equal(outputPresentation(vm).title,'Saída de áudio');
  const s={graph:{nodes:[{block:{kind:'output',endpoint_id:'render-cable'}}]},engine_state:'running',signal:2};
  const original=JSON.stringify(s);
  assert.match(cableRouteMessage(s,[render,capture]),/Envio ao cabo ativo/);
  assert.match(cableRouteMessage({...s,suspended:true},[render,capture]),/automaticamente/);
  assert.match(cableRouteMessage(s,[{...render,state:'disabled'}]),/Nenhum outro dispositivo/);
  assert.equal(JSON.stringify(s),original,'diagnóstico não pode escolher/trocar o endpoint');
  s.graph.nodes.push({block:{kind:'capture',endpoint_id:capture.endpoint_id}});
  assert.match(cableRouteMessage(s,[render,capture]),/retorno/);
});

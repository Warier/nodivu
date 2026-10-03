const {test}=require('node:test');
const assert=require('node:assert/strict');
const modulePath=require('node:url').pathToFileURL(require('node:path').join(__dirname,'../ui/playback-state.mjs')).href;
const device={endpoint_id:'phones',name:'Fones',flow:'render',state:'active'};
test('discovery failure is independent of backend connection and offers recovery',async()=>{
 const {audioPresentation:show}=await import(modulePath);
 assert.equal(show(null,[],false).action,'reconnect');
 const f=fixture();const result=show(f.snapshot,[],true,'Enumerar entradas: 0xE000020B');
 assert.equal(result.action,'refresh');assert.match(result.message,/0xE000020B/);assert.match(result.message,/editando/);
 assert.equal(show(f.snapshot,f.devices,true,'').ready,true);
});
function fixture(){
 const node={id:'player',block:{kind:'plugin',source:true,bypass:false}};
 const snapshot={engine_state:'running',suspended:false,muted:false,graph:{nodes:[node,{id:'out',block:{kind:'output',endpoint_id:'phones'}}],edges:[{from:'player',to:'out'}]},plugin_states:{player:{file:'C:/clip.mp3',status:2}}};
 return {snapshot,node,devices:[device],online:true,busy:false};
}
test('Play exige arquivo, dispositivo ativo e caminho; preparação transitória não autoriza Play',async()=>{
 const {playerPresentation:show,audioPresentation:audio}=await import(modulePath);
 const f=fixture();assert.equal(show(f).canPlay,true);
 f.snapshot.suspended=true;assert.equal(show(f).canPlay,false);assert.equal(audio(f.snapshot,f.devices,true).action,'activate');
 f.snapshot.plugin_states={};f.savedPath='C:/clip.mp3';assert.equal(show(f).needsLoad,true);assert.match(show(f).reason,/ainda não carregado/);
 f.snapshot.suspended=false;f.snapshot.plugin_states.player={file:f.savedPath,status:2};
 f.snapshot.graph.edges=[];f.snapshot.signal=0;assert.match(show(f).reason,/Conecte/);assert.equal(audio(f.snapshot,f.devices,true).action,null,'Conectar deve bastar, sem botão de ligar');
 f.snapshot.graph.nodes.push({id:'meter',block:{kind:'meter'}});f.snapshot.graph.edges=[{from:'player',to:'meter'}];
 assert.equal(show(f).canPlay,true);assert.match(show(f).warning,/não à saída/);
 f.node.block.bypass=true;assert.match(show(f).reason,/bypass/);
});
test('Falhas de arquivo têm causa legível; referência e arquivo pronto são distintos',async()=>{
 const {playerPresentation:show}=await import(modulePath);const f=fixture();
 f.snapshot.plugin_states.player.status=0;assert.equal(show(f).needsLoad,true);
 f.snapshot.plugin_states.player.status=1;assert.match(show(f).reason,/decodificação/);
 f.snapshot.plugin_states.player.status=0x800700DF|0;assert.match(show(f).reason,/10 minutos/);
 f.snapshot.plugin_states.player.status=0x80070002|0;assert.match(show(f).reason,/não encontrado/);
 f.snapshot.plugin_states.player.status=0x80004005|0;assert.match(show(f).reason,/0x80004005/);
 f.error='Escolha MP3 local de até 32 MiB';assert.equal(show(f).reason,f.error);
});
test('Estado global distingue indisponível, falha, abertura e mute sem bloquear MP3 por falha só de captura',async()=>{
 const {audioPresentation:show,playerPresentation:player}=await import(modulePath);const f=fixture();
 assert.equal(show(f.snapshot,[],true).action,'output');
 assert.equal(show(f.snapshot,f.devices,false).ready,undefined);
 f.snapshot.engine_state='starting';assert.equal(show(f.snapshot,f.devices,true).action,null);
 f.snapshot.engine_state='idle';f.snapshot.last_error={message:'WASAPI falhou'};assert.equal(show(f.snapshot,f.devices,true).message,'WASAPI falhou');
 f.snapshot.last_error=null;f.snapshot.engine_state='running';f.snapshot.muted=true;assert.equal(show(f.snapshot,f.devices,true).action,'unmute');assert.equal(player(f).canPlay,false);
 f.snapshot.muted=false;f.snapshot.capture_error={message:'Microfone removido'};assert.match(show(f.snapshot,f.devices,true).message,/Microfone removido/);assert.equal(player(f).canPlay,true);
});

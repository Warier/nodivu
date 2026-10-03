const assert = require('node:assert/strict');
module.exports = async function smoke(win, audio) {
 const run = code => win.webContents.executeJavaScript(code);
 async function until(code, label) { const end=Date.now()+7000;while(Date.now()<end){if(await run(code))return;await new Promise(r=>setTimeout(r,40));}throw new Error(label+': '+await run('document.getElementById("error").textContent')); }
 const idle=()=>until('document.body.dataset.busy === "false"','operação pendente');
 const click=async selector=>{await idle();await run(`document.querySelector(${JSON.stringify(selector)}).click()`);await new Promise(r=>setTimeout(r,80));await idle();};
 const count=(selector,n)=>until(`document.querySelectorAll(${JSON.stringify(selector)}).length === ${n}`,'quantidade de elementos');
 const node=kind=>`.node[data-kind="${kind}"]`;
 const port=(kind,flow)=>`${node(kind)} [data-port="${flow}"]`;
 async function wire(from,to){await click('#fit-view');await idle();const points=await run(`(${JSON.stringify([port(from,'source'),port(to,'sink')])}).map(s=>{const r=document.querySelector(s).getBoundingClientRect();return {x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2)}})`);
  win.webContents.sendInputEvent({type:'mouseDown',button:'left',...points[0],clickCount:1});win.webContents.sendInputEvent({type:'mouseMove',button:'left',...points[1]});win.webContents.sendInputEvent({type:'mouseUp',button:'left',...points[1],clickCount:1});await new Promise(r=>setTimeout(r,120));await idle();}
 async function value(selector,value,type='change'){await run(`{const e=document.querySelector(${JSON.stringify(selector)});if(e.type==='checkbox')e.checked=${JSON.stringify(value)};else e.value=${JSON.stringify(value)};e.dispatchEvent(new Event(${JSON.stringify(type)},{bubbles:true}));}`);await new Promise(r=>setTimeout(r,180));await idle();}
 await until('document.body.dataset.ready === "true"','backend não iniciou');
 assert.equal(await run('typeof window.require'),'undefined');
 await click('#quick-start');await count('.node',2);
 await click('#palette [data-kind="gain"]');await click('#palette [data-kind="tone"]');await count('.node',4);
 if(audio){
  for(const kind of ['capture','output']){const selector=node(kind)+' select';const device=await run(`Array.from(document.querySelector(${JSON.stringify(selector)}).options).find(o=>o.value)?.value`);assert.ok(device,'Dispositivo ativo '+kind);await value(selector,device);}
  await until('document.body.dataset.state === "running"','dispositivos não abriram');
 }
 await wire('capture','gain');await count('.wire',1);await wire('gain','output');await count('.wire',2);
 const generation=await run('document.body.dataset.generation');
 const gain=node('gain')+' [data-field="gain_db"]';await value(gain,-24,'input');
 await until(`document.querySelector(${JSON.stringify(gain)}).value === '-24'`,'ganho não mudou');
 // Valores reais retornados pelo backend; inspeção não disputa a requisição do renderer.
 if(audio){await until('document.body.dataset.signal === "1"','captura não conectou');await new Promise(r=>setTimeout(r,550));console.log('Microfone:',await run('document.getElementById("input-db").textContent+" → "+document.getElementById("output-db").textContent'));}
 const bypass=node('gain')+' [data-field="bypass"]';await value(bypass,true);await value(bypass,false);
 await click('#mute-all');if(audio)await until('document.body.dataset.signal === "0"','mute não aplicado');await click('#mute-all');
 // Context menu nativo sobre o meio do fio ganho -> saída, fora dos blocos.
 const mid=await run(`{const p=document.querySelectorAll('.wire')[1];const point=p.getPointAtLength(p.getTotalLength()/2);const r=document.getElementById('wires').getBoundingClientRect();({x:Math.round(r.left+point.x*r.width/parseFloat(document.getElementById('canvas').style.width)),y:Math.round(r.top+point.y*r.height/parseFloat(document.getElementById('canvas').style.height))})}`);
 win.webContents.sendInputEvent({type:'mouseDown',button:'right',...mid,clickCount:1});win.webContents.sendInputEvent({type:'mouseUp',button:'right',...mid,clickCount:1});await count('.wire',1);await idle();
 assert.equal(await run('document.body.dataset.generation'),generation,'Excluir fio reabriu dispositivo');
 await wire('gain','output');await count('.wire',2);
 await click('#connections .edge-remove');await count('.wire',1);
 await wire('tone','gain');await count('.wire',2);
 if(audio){await until('document.body.dataset.signal === "2"','gerador não conectado');await new Promise(r=>setTimeout(r,600));const db=await run('document.getElementById("output-db").textContent');assert.ok(Number.isFinite(parseFloat(db)),'Tom não produziu sinal');console.log('Tom 440 Hz:',db);}
 assert.equal(await run('document.body.dataset.generation'),generation,'Editar cadeia reabriu dispositivo');
 // Remover ganho também remove seus fios; criar caminho direto durante execução.
 await click(node('gain')+' .node-close');await count('.wire',0);await wire('tone','output');await count('.wire',1);
 if(audio){await until('document.body.dataset.signal === "2"','tom direto não conectado');assert.equal(await run('document.body.dataset.state'),'running');}
 console.log('PASS: backend dedicado, ganho/bypass, fios ao vivo, botão direito, gerador e geração estável.');
 // O chamador fecha a janela com áudio ativo e aguarda teardown do filho.
};

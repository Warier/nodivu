// Real Chromium pointer coordinates: detect double-scaling, bad hit targets and camera jumps.
const assert = require('node:assert/strict');
module.exports = async win => {
  const run = code => win.webContents.executeJavaScript(code);
  const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
  async function click(selector) {await run(`document.querySelector(${JSON.stringify(selector)}).click()`); await pause(100);}
  const revision = await run('document.body.dataset.revision');
  const generation = await run('document.body.dataset.generation');
  const state = () => run(`({zoom:Number(document.getElementById('viewport').dataset.zoom),nodes:[...document.querySelectorAll('.node')].map(n=>({id:n.dataset.id,x:parseFloat(n.style.left),y:parseFloat(n.style.top)}))})`);
  const initial = await state();
  await click('#zoom-out'); assert.ok((await state()).zoom < initial.zoom);
  await click('#zoom-in'); assert.ok(Math.abs((await state()).zoom - initial.zoom) < 0.00001);
  await click('#zoom-reset'); assert.equal((await state()).zoom, 1);
  await click('#fit-view');
  const fitted = await state();assert.deepEqual(fitted.nodes, initial.nodes);
  assert.ok(await run(`{const v=document.getElementById('viewport').getBoundingClientRect();[...document.querySelectorAll('.node')].every(n=>{const r=n.getBoundingClientRect();return r.left>=v.left&&r.top>=v.top&&r.right<=v.right&&r.bottom<=v.bottom;})}`));
  // Ctrl+wheel must keep the world point under the cursor fixed, not zoom the whole page.
  const anchor = await run(`{const v=document.getElementById('viewport').getBoundingClientRect();({x:Math.round(v.left+v.width*.4),y:Math.round(v.top+v.height*.4)})}`);
  const world = () => run(`{const c=document.getElementById('canvas').getBoundingClientRect(),s=Number(document.getElementById('viewport').dataset.zoom);({x:(${anchor.x}-c.left)/s,y:(${anchor.y}-c.top)/s})}`);
  const before = await world();
  await run(`document.getElementById('viewport').dispatchEvent(new WheelEvent('wheel',{bubbles:true,cancelable:true,ctrlKey:true,deltaY:-75,clientX:${anchor.x},clientY:${anchor.y}}))`);
  const after = await world();assert.ok(Math.abs(after.x-before.x)<0.001&&Math.abs(after.y-before.y)<0.001,JSON.stringify({before,after}));
  await click('#fit-view');
  // Drag background with the real left mouse button.
  const start=await run(`{const r=document.getElementById('viewport').getBoundingClientRect();({x:Math.round(r.left+8),y:Math.round(r.top+8)})}`);
  const rect=()=>run(`{const r=document.querySelector('.node.tone').getBoundingClientRect();({x:r.left,y:r.top})}`);
  const old=await rect();
  win.webContents.sendInputEvent({type:'mouseDown',button:'left',...start,clickCount:1});
  win.webContents.sendInputEvent({type:'mouseMove',button:'left',x:start.x+50,y:start.y+30});
  win.webContents.sendInputEvent({type:'mouseUp',button:'left',x:start.x+50,y:start.y+30,clickCount:1});await pause(120);
  const moved=await rect();assert.ok(Math.abs(moved.x-old.x-50)<1&&Math.abs(moved.y-old.y-30)<1);
  assert.deepEqual((await state()).nodes,initial.nodes);
  await click('#fit-view');
  // Node drag uses screen delta / zoom; it does not pan the scene or alter graph revision.
  const node=await run(`{const n=document.querySelector('.node.tone'),r=n.querySelector('.node-title').getBoundingClientRect();({id:n.dataset.id,x:parseFloat(n.style.left),y:parseFloat(n.style.top),clientX:Math.round(r.left+r.width*.4),clientY:Math.round(r.top+r.height/2)})}`);
  const zoom=(await state()).zoom;
  win.webContents.sendInputEvent({type:'mouseDown',button:'left',x:node.clientX,y:node.clientY,clickCount:1});
  win.webContents.sendInputEvent({type:'mouseMove',button:'left',x:node.clientX+24,y:node.clientY+16});
  win.webContents.sendInputEvent({type:'mouseUp',button:'left',x:node.clientX+24,y:node.clientY+16,clickCount:1});await pause(120);
  const dragged=(await state()).nodes.find(n=>n.id===node.id);
  assert.ok(Math.abs(dragged.x-node.x-24/zoom)<0.01&&Math.abs(dragged.y-node.y-16/zoom)<0.01);
  // SVG endpoints must still coincide with the actual port centres after pan/zoom/drag.
  assert.ok(await run(`{const c=document.getElementById('canvas').getBoundingClientRect(),z=Number(document.getElementById('viewport').dataset.zoom);[...document.querySelectorAll('.wire')].every(w=>{const e=JSON.parse(w.dataset.edge);return [[e.from,'source',e.from_port||0,0],[e.to,'sink',e.to_port||0,w.getTotalLength()]].every(([id,port,index,length])=>{const r=document.querySelector('[data-id="'+id+'"] [data-port="'+port+'"][data-port-id="'+index+'"]').getBoundingClientRect(),p=w.getPointAtLength(length);return Math.abs(c.left+p.x*z-r.left-r.width/2)<1&&Math.abs(c.top+p.y*z-r.top-r.height/2)<1;});})}`));
  assert.equal(await run('document.body.dataset.revision'),revision);
  assert.equal(await run('document.body.dataset.generation'),generation);
  await click('#fit-view');
  // After the routing smoke, Mixer input 0 is free. Connect it with an actual scaled drag.
  if (await run('Boolean(document.querySelector(".node.mixer"))')) {
    const count=await run('document.querySelectorAll(".wire").length');
    const points=await run(`['.node.tone [data-port=source]','.node.mixer [data-port=sink][data-port-id="0"]'].map(s=>{const r=document.querySelector(s).getBoundingClientRect();return {x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2)};})`);
    win.webContents.sendInputEvent({type:'mouseDown',button:'left',...points[0],clickCount:1});
    win.webContents.sendInputEvent({type:'mouseMove',button:'left',...points[1]});
    win.webContents.sendInputEvent({type:'mouseUp',button:'left',...points[1],clickCount:1});
    for(let i=0;i<40;i++){await pause(50);if(await run(`document.querySelectorAll('.wire').length===${count+1}`))break;}
    assert.equal(await run('document.querySelectorAll(".wire").length'),count+1);
    assert.equal(await run('document.body.dataset.generation'),generation);
    assert.equal(await run('document.getElementById("error").textContent'),'');
  }
  await require('node:fs/promises').writeFile(require('node:path').resolve(process.cwd(),'.local/viewport-canvas.png'),(await win.webContents.capturePage()).toPNG());
  console.log('PASS: zoom ancorado, pan livre, enquadramento sem mover blocos, arraste escalado e alinhamento dos fios; revisao/audio preservados.');
};

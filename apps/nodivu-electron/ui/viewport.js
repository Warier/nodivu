// View coordinates stay local to the renderer; navigating never changes the audio graph.
export function createViewport({viewport, canvas, onChange, bounds}) {
  const minimum = 0.1, maximum = 2;
  let scale = 1, x = 0, y = 0, pan = null, space = false;
  const editable = target => target.closest('input,select,textarea,button,[contenteditable=true]');
  function paint() {
    canvas.style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
    viewport.style.backgroundPosition = `${x}px ${y}px`;
    viewport.style.backgroundSize = `${22 * scale}px ${22 * scale}px`;
    viewport.dataset.zoom = String(scale);
    document.getElementById('zoom-reset').textContent = Math.round(scale * 100) + '%';
    document.getElementById('zoom-in').disabled = scale >= maximum;
    document.getElementById('zoom-out').disabled = scale <= minimum;
    onChange();
  }
  function world(clientX, clientY) {
    const rect = viewport.getBoundingClientRect();
    return {x: (clientX - rect.left - x) / scale, y: (clientY - rect.top - y) / scale};
  }
  function zoom(value, clientX, clientY) {
    const rect = viewport.getBoundingClientRect();
    clientX ??= rect.left + viewport.clientWidth / 2;
    clientY ??= rect.top + viewport.clientHeight / 2;
    const anchor = world(clientX, clientY);
    scale = Math.max(minimum, Math.min(maximum, value));
    x = clientX - rect.left - anchor.x * scale;
    y = clientY - rect.top - anchor.y * scale;
    paint();
  }
  function fit() {
    const box = bounds();
    if (!box) {scale = 1; x = 0; y = 0; paint(); return;}
    const width = Math.max(1, box.right - box.left), height = Math.max(1, box.bottom - box.top);
    scale = Math.max(minimum, Math.min(1, (viewport.clientWidth - 64) / width, (viewport.clientHeight - 64) / height));
    x = (viewport.clientWidth - width * scale) / 2 - box.left * scale;
    y = (viewport.clientHeight - height * scale) / 2 - box.top * scale;
    paint();
  }
  function cancel() {
    if (pan && viewport.hasPointerCapture(pan.pointer)) viewport.releasePointerCapture(pan.pointer);
    pan = null; viewport.classList.remove('panning');
  }
  viewport.addEventListener('pointerdown', event => {
    const blank = !event.target.closest('.node,.wire,#empty');
    if (event.button !== 1 && !(event.button === 0 && (blank || (space && !editable(event.target))))) return;
    event.preventDefault(); event.stopPropagation();
    pan = {pointer:event.pointerId, clientX:event.clientX, clientY:event.clientY, x, y};
    viewport.setPointerCapture(event.pointerId); viewport.classList.add('panning');
  }, true);
  viewport.addEventListener('pointermove', event => {
    if (!pan || event.pointerId !== pan.pointer) return;
    event.preventDefault(); event.stopPropagation();
    x = pan.x + event.clientX - pan.clientX;
    y = pan.y + event.clientY - pan.clientY;
    paint();
  }, true);
  viewport.addEventListener('pointerup', event => {
    if (!pan || event.pointerId !== pan.pointer) return;
    event.preventDefault(); event.stopPropagation(); cancel();
  }, true);
  viewport.addEventListener('pointercancel', cancel);
  viewport.addEventListener('lostpointercapture', () => {pan = null; viewport.classList.remove('panning');});
  viewport.addEventListener('auxclick', event => {if (event.button === 1) event.preventDefault();});
  viewport.addEventListener('wheel', event => {
    if (editable(event.target) && !event.ctrlKey && !event.metaKey) return;
    event.preventDefault();
    // Do not change the camera during a node/wire gesture; its world anchor must stay stable.
    if (viewport.dataset.editing === 'true' || pan) return;
    const unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? viewport.clientHeight : 1;
    if (event.ctrlKey || event.metaKey) zoom(scale * Math.exp(-event.deltaY * unit * 0.002), event.clientX, event.clientY);
    else {x -= (event.shiftKey ? event.deltaY : event.deltaX) * unit; y -= (event.shiftKey ? 0 : event.deltaY) * unit; paint();}
  }, {passive:false});
  document.addEventListener('keydown', event => {
    if (editable(event.target)) return;
    if (event.code === 'Space') {space = true; viewport.classList.add('pan-ready'); event.preventDefault();}
    if (event.code === 'Escape') cancel();
  });
  document.addEventListener('keyup', event => {if (event.code === 'Space') {space = false; viewport.classList.remove('pan-ready');}});
  window.addEventListener('blur', () => {space = false; viewport.classList.remove('pan-ready'); cancel();});
  document.getElementById('zoom-in').onclick = () => zoom(scale * 1.2);
  document.getElementById('zoom-out').onclick = () => zoom(scale / 1.2);
  document.getElementById('zoom-reset').onclick = () => zoom(1);
  document.getElementById('fit-view').onclick = fit;
  // Resize preserves the world point in the centre instead of jumping to the origin.
  let previousWidth = viewport.clientWidth, previousHeight = viewport.clientHeight;
  new ResizeObserver(() => {
    x += (viewport.clientWidth - previousWidth) / 2;
    y += (viewport.clientHeight - previousHeight) / 2;
    previousWidth = viewport.clientWidth; previousHeight = viewport.clientHeight; paint();
  }).observe(viewport);
  return {world, fit, save:()=>({x,y,scale}), restore:state=>{({x,y,scale}=state);paint();}, get scale() {return scale;}};
}

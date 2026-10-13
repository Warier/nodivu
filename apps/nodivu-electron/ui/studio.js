// Layout and selection are renderer state. No audio commands or project mutations here.
export function createStudio({select, add, world, disconnect}) {
  const $ = id => document.getElementById(id);
  const panel = $('block-inspector'), workspace = document.querySelector('main');
  $('inspect-selected').disabled=true;
  let gesture = null, edge = null, libraryWidth = 228, positioned = false;
  const clamp = (v, min, max) => Math.max(min, Math.min(Math.max(min, max), v));
  function constrain(x = panel.offsetLeft, y = panel.offsetTop) {
    if(panel.hidden)return;
    panel.style.left = clamp(x, 8, workspace.clientWidth - panel.offsetWidth - 8) + 'px';
    panel.style.top = clamp(y, 8, workspace.clientHeight - panel.offsetHeight - 8) + 'px';
  }
  function settings(section) {
    $('app-settings').hidden = false;
    $('settings-toggle').setAttribute('aria-expanded', 'true');
    if (section) { const target = $(section); if (target.tagName === 'DETAILS') target.open = true; target.scrollIntoView({block:'nearest'}); }
  }
  $('settings-toggle').onclick = () => {if ($('app-settings').hidden) settings(); else closeSettings();};
  function closeSettings() { $('app-settings').hidden = true; $('settings-toggle').setAttribute('aria-expanded','false'); }
  $('settings-close').onclick = closeSettings;
  $('listen-shortcut').onclick = () => settings('monitor-panel');
  $('library-toggle').onclick = () => {
    const collapsed = document.body.classList.toggle('library-collapsed');
    $('library-toggle').setAttribute('aria-expanded', String(!collapsed));
    $('library-toggle').title = collapsed ? 'Abrir biblioteca' : 'Recolher biblioteca';
    document.documentElement.style.setProperty('--library-width', collapsed ? '64px' : libraryWidth + 'px');
    for(const group of $('palette').children){if(collapsed){group.dataset.expanded=String(group.open);group.open=true;}else group.open=group.dataset.expanded!=='false';}
  };
  const resize = $('library-resize');
  function width(value) { libraryWidth = clamp(value,184,320); document.documentElement.style.setProperty('--library-width',libraryWidth+'px'); resize.setAttribute('aria-valuenow',String(libraryWidth)); }
  resize.onpointerdown = e => {if(e.button!==0)return;gesture={type:'library',pointer:e.pointerId};resize.setPointerCapture(e.pointerId);e.preventDefault();};
  resize.onkeydown = e => {if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key)){e.preventDefault();width(e.key==='Home'?184:e.key==='End'?320:libraryWidth+(e.key==='ArrowLeft'?-12:12));}};
  $('inspector-handle').onpointerdown = e => {
    if(e.button!==0||e.target.closest('button'))return;
    gesture={type:'panel',pointer:e.pointerId,x:e.clientX,y:e.clientY,left:panel.offsetLeft,top:panel.offsetTop};
    e.currentTarget.setPointerCapture(e.pointerId); e.preventDefault();
  };
  document.addEventListener('pointermove', e => {
    if(!gesture||gesture.pointer!==e.pointerId)return;
    if(gesture.type==='library')width(e.clientX);
    else constrain(gesture.left+e.clientX-gesture.x,gesture.top+e.clientY-gesture.y);
  });
  const cancel = () => {gesture=null;};
  document.addEventListener('pointerup',cancel);document.addEventListener('pointercancel',cancel);window.addEventListener('blur',cancel);
  $('inspector-close').onclick = () => {panel.hidden=true;};
  $('inspector-collapse').onclick = () => {const collapsed=panel.classList.toggle('collapsed');$('inspector-collapse').setAttribute('aria-expanded',String(!collapsed));constrain();};
  $('inspector-home').onclick = () => constrain(workspace.clientWidth-panel.offsetWidth-16,$('viewport').offsetTop+12);
  $('inspect-selected').onclick = () => {panel.hidden=false;constrain();};
  new ResizeObserver(()=>constrain()).observe(workspace);
  new ResizeObserver(()=>constrain()).observe(panel);
  const viewport=$('viewport');
  viewport.addEventListener('dragover',e=>{if(e.dataTransfer.types.includes('application/x-nodivu-block')){e.preventDefault();e.dataTransfer.dropEffect='copy';viewport.classList.add('drop-target');}});
  viewport.addEventListener('dragleave',()=>viewport.classList.remove('drop-target'));
  viewport.addEventListener('drop',e=>{
    viewport.classList.remove('drop-target'); const raw=e.dataTransfer.getData('application/x-nodivu-block');if(!raw)return;e.preventDefault();
    try {const d=JSON.parse(raw);add(d.kind,d.pluginId,world(e.clientX,e.clientY));}catch{/* Only internal catalog IDs are accepted by the renderer/backend. */}
  });
  function hideMenu() {$('wire-menu').hidden=true;}
  document.addEventListener('pointerdown',e=>{if(!e.target.closest('#wire-menu'))hideMenu();});
  document.addEventListener('keydown',e=>{if(e.key==='Escape'){hideMenu();closeSettings();}});
  $('wire-delete').onclick=()=>{if(edge)disconnect(edge);hideMenu();};
  return {
    settings,
    select(id) {closeSettings();select(id);panel.hidden=false;constrain();},
    show(title,subtitle,content) {
      $('inspector-title').textContent=title;$('inspector-subtitle').textContent=subtitle;
      $('inspector-content').replaceChildren(content);panel.classList.remove('collapsed');$('inspector-collapse').setAttribute('aria-expanded','true');
      $('inspect-selected').disabled=false;panel.hidden=false;
      if(!positioned){constrain(workspace.clientWidth-panel.offsetWidth-16,$('viewport').offsetTop+12);positioned=true;}
      requestAnimationFrame(()=>constrain());
    },
    clear() {$('inspector-content').replaceChildren();panel.hidden=true;$('inspect-selected').disabled=true;},
    context(event,value) {
      edge=value;const menu=$('wire-menu');menu.hidden=false;
      menu.style.left=clamp(event.clientX,8,innerWidth-menu.offsetWidth-8)+'px';menu.style.top=clamp(event.clientY,8,innerHeight-menu.offsetHeight-8)+'px';$('wire-delete').focus();
    }
  };
}

// Tabs/groups only change visibility; controls remain mounted while a value is edited.
export function arrangeSettings(content, fields, layout) {
  const pages=new Map();
  for(const [key,{field,control}] of fields) {
    const name=field.page||'Ajustes';
    if(!pages.has(name))pages.set(name,new Map());
    const groups=pages.get(name),group=field.group||'';
    if(!groups.has(group))groups.set(group,[]);
    groups.get(group).push({key,field,label:control.closest('label')});
  }
  content.querySelectorAll('.plugin-group').forEach(e=>e.remove());
  const tabs=document.createElement('div');tabs.className='settings-tabs';tabs.setAttribute('role','tablist');tabs.setAttribute('aria-label','Páginas de configurações');
  const sections=[];
  for(const [name,groups] of pages) {
    const section=document.createElement('section');section.className='settings-page';section.setAttribute('role','tabpanel');
    section.id='settings-page-'+sections.length;section.setAttribute('aria-label',name);section.dataset.layout=layout||'stack';
    for(const [group,items] of groups) {
      const box=document.createElement('fieldset'),legend=document.createElement('legend');legend.textContent=group||'Controles';box.append(legend);
      let advanced=null;
      for(const {field,label} of items) {
        if(field.advanced) {if(!advanced){advanced=document.createElement('details');const summary=document.createElement('summary');summary.textContent='Avançado';advanced.append(summary);box.append(advanced);}advanced.append(label);} else box.append(label);
      }
      section.append(box);
    }
    const button=document.createElement('button');button.textContent=name;button.setAttribute('role','tab');button.setAttribute('aria-controls',section.id);
    const activate=()=>{for(const s of sections)s.section.hidden=s.section!==section;for(const b of tabs.children){b.setAttribute('aria-selected',String(b===button));b.tabIndex=b===button?0:-1;}};
    button.onclick=activate;button.onkeydown=e=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key)){e.preventDefault();const list=[...tabs.children],i=list.indexOf(button);const b=list[e.key==='Home'?0:e.key==='End'?list.length-1:(i+(e.key==='ArrowLeft'?-1:1)+list.length)%list.length];b.click();b.focus();}};
    tabs.append(button);sections.push({section,button});
  }
  if(sections.length){content.prepend(tabs,...sections.map(s=>s.section));sections[0].button.click();tabs.hidden=sections.length<2;}
}

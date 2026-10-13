import {marked} from './vendor/marked.js';
import DOMPurify from './vendor/purify.js';
const dialog=document.querySelector('#plugins-dialog');
const list=document.querySelector('#plugins-list');
const message=document.querySelector('#plugins-message');
let busy=false,view='installed',storeState=null;
function setView(next){view=next;document.getElementById('plugins-search').hidden=next==='installed';for(const [id,v] of [['installed','installed'],['explore','explore'],['updates','updates']])document.getElementById('plugins-'+id).setAttribute('aria-pressed',String(next===v));}
const element=(tag,text,cls)=>{const el=document.createElement(tag);el.textContent=text;if(cls)el.className=cls;return el;};
async function run(action,payload){
  if(busy)return;
  ++detailGeneration;busy=true;dialog.setAttribute('aria-busy','true');
  let progressTimer;
  if(action==='store-install'){progressBox.hidden=false;progressTimer=setInterval(updateProgress,250);}
  try{
    const state=await window.nodivu.plugins(action,payload);
    if(state.error)throw new Error(state.error);
    setView('installed');list.replaceChildren();
    document.querySelector('#plugin-details').hidden=true;list.hidden=false;
    message.textContent=state.entries.some(e=>e.pending)?'Há mudanças pendentes. Salve seu projeto e feche e abra o Nodivu para aplicá-las. O áudio atual continua como estava.':'Plugins locais. Ativar, desativar e remover serão aplicados na próxima abertura.';
    if(state.warnings?.length)message.textContent+=' '+state.warnings.join(' ');
    for(const item of state.entries){
      const card=element('article','', 'installed-plugin');
      const body=element('div','');
      body.append(element('h3',item.title),element('p',item.id||'ID inválido','small'));
      if(item.metadata)body.append(element('p',`${item.metadata.version} · ${item.metadata.author} · Consumo: ${{light:'leve',moderate:'moderado',heavy:'exigente',unknown:'não medido'}[item.metadata.resourceTier]} (declarado pelo autor)`,'small'));
      if(item.update)body.append(element('p','Versão '+item.update+' agendada para a próxima abertura.','small'));
      body.append(element('p',`${item.kind} · ${item.protected?'Pacote do app':'Instalação local'} · ${item.removed?'Remoção pendente':item.enabled?'Habilitado':'Desativado'}${item.pending?' · Aguardando reinício':''}`,'small'));
      if(item.error)body.append(element('p','Falha no manifesto: '+item.error,'plugin-error'));
      if(item.loadError)body.append(element('p','Indisponível na última abertura: '+item.loadError,'plugin-error'));
      const actions=element('div','','plugin-actions');
      const button=(label,action,payload)=>{const b=element('button',label);b.dataset.action=action;b.onclick=()=>run(action,payload);actions.append(b);return b;};
      const details=element('button','Detalhes');details.dataset.action='details';details.onclick=()=>showDetails(item.key);actions.append(details);
      if(item.update)button('Cancelar atualização','cancel-update',{key:item.key});
      else if(item.previous?.length){const previous=item.previous.find(p=>p.version!==item.metadata?.version);if(previous)button('Voltar para '+previous.version,'rollback',{key:item.key,previous:previous.key});}
      if(item.removed)button('Cancelar remoção','cancel-removal',{key:item.key});
      else{
        button(item.enabled?'Desativar':'Ativar','enable',{key:item.key,enabled:!item.enabled}).disabled=Boolean(item.error&&!item.enabled);
        if(!item.protected)button('Remover…','remove',{key:item.key});
      }
      card.append(body,actions);list.append(card);
    }
    if(!state.entries.length)list.append(element('p','Nenhum pacote local instalado. Os blocos básicos do Nodivu continuam disponíveis.'));
    document.querySelector('#plugins-count').textContent=`${state.entries.filter(e=>e.enabled).length}/${state.limit} tipos habilitados`;
  }catch(e){message.textContent=e.message;}
  finally{clearInterval(progressTimer);progressBox.hidden=true;busy=false;dialog.removeAttribute('aria-busy');}
}
for(const id of ['plugins-open','plugins-library'])document.getElementById(id).onclick=()=>{dialog.showModal();void run('list');};
document.querySelector('#plugins-close').onclick=()=>{++detailGeneration;dialog.close();};
document.querySelector('#plugins-add').onclick=()=>run('install');
document.querySelector('#plugins-zip').onclick=()=>run('install-zip');
const clearCache=element('button','Limpar downloads');clearCache.id='plugins-clear-cache';clearCache.title='Remove downloads e atualizações canceladas; preserva plugins, projetos e versões para retorno.';clearCache.onclick=()=>run('clear-cache');document.querySelector('.plugins-toolbar').append(clearCache);
document.querySelector('#plugins-refresh').onclick=()=>view==='installed'?run('list'):loadStore(true);
document.querySelector('#plugins-installed').onclick=()=>run('list');
for(const kind of ['explore','updates'])document.querySelector('#plugins-'+kind).onclick=()=>{if(!busy){setView(kind);void loadStore();}};
document.querySelector('#plugins-search').oninput=()=>renderStore();
document.querySelector('#plugins-back').onclick=()=>{++detailGeneration;document.querySelector('#plugin-details').hidden=true;list.hidden=false;};
let detailGeneration=0;
async function showDetails(key,remote=false){
 const generation=++detailGeneration;
 try{
  const info=await window.nodivu.plugins(remote?'store-details':'details',{key});if(info.error)throw new Error(info.error);
  if(generation!==detailGeneration)return;
  document.querySelector('#plugin-details').hidden=false;list.hidden=true;document.querySelector('#plugins-back').textContent=remote?'← Catálogo':'← Instalados';
  document.querySelector('#plugin-detail-title').textContent=info.entry.title;
  document.querySelector('#plugin-detail-meta').textContent=info.entry.metadata?`${info.entry.metadata.author} · ${info.entry.metadata.version} · ${info.entry.metadata.license}`:'Pacote local sem metadados de versão.';
  const article=document.querySelector('#plugin-readme');
  const fragment=DOMPurify.sanitize(marked.parse(info.markdown,{gfm:true}),{RETURN_DOM_FRAGMENT:true,ALLOWED_TAGS:['h1','h2','h3','h4','h5','h6','p','ul','ol','li','strong','em','del','pre','code','blockquote','table','thead','tbody','tr','th','td','hr','br','a','img'],ALLOWED_ATTR:['href','src','alt','title'],ALLOW_DATA_ATTR:false});
  for(const link of fragment.querySelectorAll('a')){
    const href=link.getAttribute('href');link.removeAttribute('href');
    try{const url=new URL(href);if(url.protocol==='https:'){link.href=url.href;link.onclick=event=>{event.preventDefault();void window.nodivu.plugins('link',{url:url.href});};}}catch{/* Relative document links remain plain text until navigation is supported. */}
  }
  const images=[...fragment.querySelectorAll('img')];
  const pending=images.slice(0,12).map(img=>({img,relative:img.getAttribute('src')}));
  for(const img of images)img.removeAttribute('src');
  article.replaceChildren(fragment);
  for(const item of pending){
   const result=await window.nodivu.plugins(remote?'store-image':'image',{key,relative:item.relative});
   if(generation!==detailGeneration)return;
   if(result.url)item.img.src=result.url;
   else item.img.title='Imagem externa ou indisponível. Consulte o repositório do autor.';
  }
 }catch(error){message.textContent=error.message;}
}
// Do not let editor shortcuts delete/mute nodes while focus is in the manager.
dialog.addEventListener('keydown',event=>event.stopPropagation());

async function loadStore(force=false){
 if(busy)return;busy=true;++detailGeneration;dialog.setAttribute('aria-busy','true');message.textContent='Verificando catálogo…';
 try{storeState=await window.nodivu.plugins(force?'store-refresh':'store-list');if(!Array.isArray(storeState.entries))throw new Error(storeState.error||'Catálogo indisponível.');renderStore();}
 catch(e){message.textContent=e.message;}
 finally{busy=false;dialog.removeAttribute('aria-busy');}
}
function renderStore(){
 if(!storeState)return;
 const generation=++detailGeneration;list.replaceChildren();list.hidden=false;document.getElementById('plugin-details').hidden=true;
 message.textContent=!storeState.configured?'O catálogo público ainda não foi publicado nesta versão. Você já pode adicionar pacotes por arquivo ou pasta.':storeState.online?'Catálogo verificado. Instalações e atualizações entram em vigor ao reabrir o Nodivu.':'Offline: mostrando a última lista conhecida. Conecte-se para instalar ou verificar atualizações.';
 if(storeState.error)message.textContent+=' '+storeState.error;
 const query=document.getElementById('plugins-search').value.trim().toLocaleLowerCase();
 const rows=storeState.entries.filter(p=>(view!=='updates'||p.updateAvailable)&&[p.title,p.author,p.description].join(' ').toLocaleLowerCase().includes(query));
 document.getElementById('plugins-count').textContent=rows.length+' plugins';
 const logos=[];
 for(const p of rows.slice(0,50)){
  const card=element('article','','installed-plugin catalog-plugin');card.dataset.pluginId=p.id;
  const icon=element('div',p.title.slice(0,1).toUpperCase(),'plugin-logo');icon.setAttribute('aria-hidden','true');
  const body=element('div','','plugin-info');body.append(element('h3',p.title),element('p',p.author+' · '+p.version,'small'),element('p',p.description,'small'));
  const badges=element('div','','plugin-badges');if(p.official)badges.append(element('span','Oficial Nodivu','plugin-official'));
  const tier={light:'Leve',moderate:'Moderado',heavy:'Exigente',unknown:'Não medido'}[p.resourceTier];badges.append(element('span',tier,'plugin-tier tier-'+p.resourceTier));badges.append(element('span',p.downloadBytes<1048576?Math.ceil(p.downloadBytes/1024)+' KiB':(p.downloadBytes/1048576).toFixed(1)+' MiB','small'));
  body.append(badges);body.append(element('p','Consumo declarado pelo publicador; pode variar neste computador.','plugins-note'));
  if(!p.compatible)body.append(element('p','Requer '+p.platform+' / contrato '+p.hostApi,'plugin-error'));
  if(p.pendingVersion)body.append(element('p','Versão '+p.pendingVersion+' já agendada.','small'));
  const actions=element('div','','plugin-actions');const detail=element('button','Detalhes');detail.dataset.action='store-details';detail.onclick=()=>showDetails(p.id,true);actions.append(detail);
  const install=element('button',p.pendingVersion?'Agendado':p.updateAvailable?'Atualizar para '+p.version:p.installed?'Instalado':'Instalar');install.dataset.action='store-install';install.disabled=!storeState.online||!p.compatible||Boolean(p.pendingVersion)||(p.installed&&!p.updateAvailable);install.onclick=()=>run('store-install',{key:p.id,version:p.version});actions.append(install);
  card.append(icon,body,actions);list.append(card);if(p.logo&&logos.length<12)logos.push({id:p.id,icon});
 }
 if(rows.length>50)list.append(element('p','Mostrando os primeiros 50 resultados. Use a busca para encontrar outros plugins.'));
 if(!rows.length)list.append(element('p',view==='updates'?'Nenhuma atualização encontrada na lista disponível.':'Nenhum plugin para exibir nesta busca.'));
 void (async()=>{for(const {id,icon} of logos){if(generation!==detailGeneration)return;const result=await window.nodivu.plugins('store-image',{key:id,relative:null});if(generation!==detailGeneration)return;if(result.url){const image=document.createElement('img');image.alt='';image.src=result.url;icon.replaceChildren(image);}}})();
}

const progressBox=element('div','','plugin-download');progressBox.hidden=true;
const progressText=element('span','Preparando download…'),progressBar=document.createElement('progress'),cancelDownload=element('button','Cancelar download');
cancelDownload.onclick=()=>void window.nodivu.plugins('store-cancel');progressBox.append(progressText,progressBar,cancelDownload);message.after(progressBox);
let progressPolling=false;
async function updateProgress(){
 if(progressPolling)return;progressPolling=true;
 try{const p=await window.nodivu.plugins('store-status');if(!p.active)return;
  cancelDownload.disabled=!p.cancelable;
  progressText.textContent=p.phase==='download'?`Baixando ${p.file} · ${(p.received/1048576).toFixed(1)} MiB`:p.phase==='confirm'?'Aguardando confirmação de instalação…':p.phase==='install'?'Salvando pacote…':'Verificando integridade…';
  if(p.total){progressBar.max=p.total;progressBar.value=p.received;}else progressBar.removeAttribute('value');
 }finally{progressPolling=false;}
}

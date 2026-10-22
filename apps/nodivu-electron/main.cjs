const { app, dialog, BrowserWindow, ipcMain, protocol, session, shell, Menu, globalShortcut } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const { Engine } = require('./engine.cjs');
const { Diagnostics } = require('./diagnostics.cjs');
const os = require('node:os');
const {RecentProjects} = require('./recent-projects.cjs');
let recentProjects, projectsDirectory, installedManifest, updates, recovery;
let projectOperation=false;
let pluginQueue=Promise.resolve(),pluginQueued=0;
const demoMode=process.argv.includes('--frontend-demo');
const {readProject, writeProject, resourcesForFile} = require('./project-files.cjs');
let projectPath = null, projectDirty = false, confirmingClose = false;
const PAGE = 'nodivu://app/index.html';
const commands = new Set(['system.hello','plugins.list','capture.targets','capture.configure','plugin.command','plugin.gesture','devices.list','session.snapshot','node.add','graph.apply','audio.mute','audio.retry','audio.monitor']);
const executable = app.isPackaged ? path.join(__dirname, 'bin/nodivu-app-backend.exe') : path.resolve(__dirname, '../../target/release/nodivu-app-backend.exe');
let hotkeys, hotkeyRequest=null;
let win, engine, pluginManager, pluginCatalog, pluginOperation=false, quitting = false, exitCode = 0;
const recoveryTest=process.argv.includes('--smoke-test')&&process.argv.includes('--test-recovery');
const recoveryTestId=process.env.NODIVU_RECOVERY_TEST_ID;
if(recoveryTest&&!/^[a-zA-Z0-9-]{1,80}$/.test(recoveryTestId||''))throw new Error('ID de ensaio de recuperação inválido.');
const storeTest=process.argv.includes('--smoke-test') && process.argv.includes('--test-store');
const updateTest=process.argv.includes('--smoke-test') && process.argv.includes('--test-update');

app.on('second-instance',()=>{if(win){win.restore();win.focus();}});
protocol.registerSchemesAsPrivileged([{ scheme: 'nodivu', privileges: { standard: true, secure: true } }]);
const profileRoot=app.isPackaged ? path.join(app.getPath('appData'),'Nodivu') : path.resolve(__dirname,'../../.local/electron-profile');
// Isolate Chromium caches as well as history from an already open user session.
app.setPath('userData', process.argv.includes('--smoke-test')?path.join(app.getPath('temp'),recoveryTest?'Nodivu-recovery-smoke-'+recoveryTestId:'Nodivu-smoke-'+process.pid):profileRoot);
if(!process.argv.includes('--smoke-test') && !app.requestSingleInstanceLock())app.exit(0);
const diagnostics = new Diagnostics(path.join(app.getPath('userData'), 'logs'));
diagnostics.record('app.start', { version: app.getVersion(), windows: os.release(), os: os.version(), arch: process.arch, electron: process.versions.electron, demo: demoMode, smoke: process.argv.includes('--smoke-test') });
// Deliberate fault injection is restricted to this opt-in integration test.
const startupRecoveryTest = process.argv.includes('--smoke-test') && process.argv.includes('--test-startup-recovery');
let testHelloCalls=0, testDeviceCalls=0;
function startEngine() {
  const manifest=process.env.NODIVU_PLUGIN_MANIFEST || installedManifest || path.resolve(__dirname, app.isPackaged?'../../plugins/plugin.json':'../../.local/plugins/plugin.json');
  const env={...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_APP_ROOT_PID:String(process.pid),NODIVU_PLUGIN_REPORT:path.join(app.getPath('userData'),'plugin-load-report.json')};
  if(process.env.NODIVU_PLUGIN_MANIFEST || require('node:fs').existsSync(path.dirname(manifest))) env.NODIVU_PLUGIN_MANIFEST=manifest;
  engine = demoMode ? new (require('./demo-engine.cjs').DemoEngine)() : new Engine(executable,env);
  engine.on('fault', message => { diagnostics.record('backend.fault', {message}); if (win && !win.isDestroyed()) {if(recovery)win.webContents.send('recovery:state',recovery.protect());win.webContents.send('engine:fault', message);} });
}
function trusted(event) {
  if (!win || win.isDestroyed() || event.sender.isDestroyed() || event.sender !== win.webContents || event.senderFrame !== win.webContents.mainFrame || event.senderFrame.url !== PAGE)
    throw new Error('Origem IPC não autorizada.');
}
async function shutdown() {
  if (quitting) return;
  quitting = true;
  hotkeys?.close();
  if (engine) await engine.stop();
  diagnostics.record('app.exit', {code:exitCode});
  await Promise.race([diagnostics.flush(), new Promise(resolve=>setTimeout(resolve,1000))]);
  app.exit(exitCode);
}
app.on('before-quit', event => { if (!quitting) { event.preventDefault(); void shutdown(); } });
app.whenReady().then(async () => {
  projectsDirectory=process.argv.includes('--smoke-test') ? path.join(app.getPath('userData'),'Projects') : path.join(app.getPath('documents'),'Nodivu','Projetos');
  await fs.mkdir(projectsDirectory,{recursive:true});
  // Smoke runs must not replace the user's real recent-project history.
  recovery=new (require('./project-recovery.cjs').ProjectRecovery)(path.join(app.getPath('userData'),demoMode?'demo-recovery':'recovery'));
  await recovery.init();
  async function clearCurrentRecovery(){try{await recovery.clear();return {};}catch(e){diagnostics.record('recovery.error',{message:e.message});return {warning:'Projeto salvo/aberto, mas a cópia antiga não foi removida: '+e.message};}}
  recentProjects=new RecentProjects(path.join(app.getPath('userData'),process.argv.includes('--smoke-test')?'smoke-'+process.pid:'','recent-projects.json'));
  async function rememberRecent(file) {
    try { return {recents:await recentProjects.remember(file)}; }
    catch(error) { return {warning:'Projeto salvo/aberto, mas não foi possível atualizar os recentes: '+error.message}; }
  }
  protocol.handle('nodivu', async request => {
    const url = new URL(request.url);
    const vendors={'/vendor/marked.js':'marked/lib/marked.esm.js','/vendor/purify.js':'dompurify/dist/purify.es.mjs'};
    if(url.host==='app'&&Object.hasOwn(vendors,url.pathname))return new Response(await fs.readFile(path.join(__dirname,'node_modules',vendors[url.pathname])),{headers:{'Content-Type':'text/javascript'}});
    const files = { '/index.html': 'text/html', '/renderer.js': 'text/javascript', '/viewport.js': 'text/javascript', '/momentary.mjs': 'text/javascript', '/hotkey-actions.mjs': 'text/javascript', '/hotkeys.js': 'text/javascript', '/hotkeys.css': 'text/css', '/virtual-cable.mjs': 'text/javascript', '/playback-state.mjs': 'text/javascript', '/capture-source.mjs': 'text/javascript', '/style.css': 'text/css', '/studio.css': 'text/css', '/studio.js': 'text/javascript', '/plugin-manager.js': 'text/javascript', '/plugin-manager.css': 'text/css', ...Object.fromEntries(['index','capture','output','gain','tone','plugin','mixer','meter'].map(name => ['/blocks/'+name+'.js','text/javascript'])) };
    if (url.host !== 'app' || !Object.hasOwn(files, url.pathname)) return new Response('', { status: 404 });
    return new Response(await fs.readFile(path.join(__dirname, 'ui', url.pathname.slice(1))),
      { headers: { 'Content-Type': files[url.pathname] + '; charset=utf-8' } });
  });
  session.defaultSession.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  win = new BrowserWindow({ width: 1440, height: 900, minWidth: 960, minHeight: 650,
    title: 'Nodivu — projeto de áudio', backgroundColor: '#101319', autoHideMenuBar: false,
    webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true, sandbox: true,
      nodeIntegration: false, backgroundThrottling: false } });
  require('./app-menu.cjs')({Menu,win});
  hotkeys=new (require('./hotkeys.cjs').Hotkeys)(path.join(app.getPath('userData'),'hotkeys.json'),globalShortcut,async binding=>{
    if(binding.action==='app.show'){win.show();win.restore();win.focus();return;}
    if(win.isDestroyed()||win.webContents.isLoading())throw Error('A interface ainda não está pronta.');
    return new Promise((resolve,reject)=>{
      const token=require('node:crypto').randomUUID();
      const timer=setTimeout(()=>{hotkeyRequest=null;reject(Error('Interface sem resposta ao atalho.'));},7000);
      hotkeyRequest={token,resolve,reject,timer};win.webContents.send('hotkeys:execute',{token,binding});
    });
  });
  hotkeys.on('change',state=>{if(!win.isDestroyed())win.webContents.send('hotkeys:state',state);});
  await hotkeys.init();
  ipcMain.on('hotkeys:context',(event,actions)=>{trusted(event);try{hotkeys.setActions(actions);}catch(e){diagnostics.record('hotkeys.context',{message:e.message});}});
  ipcMain.on('hotkeys:result',(event,result)=>{
    trusted(event);if(!hotkeyRequest||result?.token!==hotkeyRequest.token)return;
    const pending=hotkeyRequest;hotkeyRequest=null;clearTimeout(pending.timer);
    if(result.ok)pending.resolve();else{diagnostics.record('hotkeys.action',{message:String(result.error)});pending.reject(Error(result.error||'Ação não executada.'));}
  });
  ipcMain.handle('hotkeys:manage',async(event,action,payload)=>{
    trusted(event);
    try{
      if(action==='save')await hotkeys.save(payload);
      else if(action==='suspend'){if(typeof payload!=='boolean')throw Error('Estado inválido.');hotkeys.suspend(payload);}
      else if(action==='test'){if(!await hotkeys.fire(payload?.id,{test:true}))throw Error(hotkeys.lastResult?.message||'Ação não disponível.');}
      else if(action!=='list')throw Error('Ação de atalhos desconhecida.');
      return {ok:true,state:hotkeys.snapshot()};
    }catch(e){return {ok:false,error:e.message};}
  });
  win.webContents.on('before-input-event',(event,input)=>{if(hotkeys.local(input))event.preventDefault();});
  win.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  win.webContents.on('will-navigate', event => event.preventDefault());
  win.webContents.on('will-attach-webview', event => event.preventDefault());
  win.webContents.on('render-process-gone', (_event, details) => { diagnostics.record('renderer.gone', {reason:details.reason,code:details.exitCode}); void shutdown(); });
  win.on('close', event => {
    if (quitting) return;
    event.preventDefault();
    if (confirmingClose || projectOperation) return;
    confirmingClose = true;
    void (async()=>{
      if (!projectDirty || (await dialog.showMessageBox(win,{type:'question',message:'Fechar e descartar alterações não salvas?',buttons:['Continuar editando','Descartar e fechar'],defaultId:0,cancelId:0})).response===1) {await clearCurrentRecovery();await shutdown();}
      confirmingClose=false;
    })();
  });
  for (const event of ['minimize', 'restore']) win.on(event, () => win.webContents.send('window:minimized', win.isMinimized()));
  if (app.isPackaged && !demoMode && !process.env.NODIVU_PLUGIN_MANIFEST) {
    installedManifest = await require('./plugin-storage.cjs').preparePlugins(
      path.resolve(__dirname,'../../plugins'), path.join(app.getPath('userData'),'plugins'));
  }
  if (!process.env.NODIVU_PLUGIN_MANIFEST) {
    const pluginRoot=demoMode?path.join(app.getPath('userData'),'plugins'):path.dirname(installedManifest || path.resolve(__dirname,'../../.local/plugins/plugin.json'));
    pluginManager=new (require('./plugin-manager.cjs').PluginManager)(pluginRoot,path.join(app.getPath('userData'),'plugin-archive'),path.join(app.getPath('userData'),'plugin-load-report.json'));
    await pluginManager.init();
  }
  pluginCatalog=await require('./plugin-store.cjs').createStore(app,{test:storeTest});
  const refreshStore=()=>{if(!pluginOperation&&pluginCatalog.source)void pluginCatalog.refresh();};
  if(!process.argv.includes('--smoke-test')){setTimeout(refreshStore,20000).unref();setInterval(refreshStore,6*60*60*1000).unref();}
  ipcMain.handle('plugins:manage',async(event,action,payload)=>{
    trusted(event);
    if(!pluginManager)return {error:'Gerenciamento indisponível com NODIVU_PLUGIN_MANIFEST. Use a instalação normal do app.'};
    if(action==='store-status')return pluginCatalog.operationStatus();
    if(action==='store-cancel')return pluginCatalog.cancel();
    if(pluginQueued>=16)return {error:'Há muitas operações de plugins pendentes. Aguarde um momento.'};
    ++pluginQueued;
    const operation=async()=>{
    pluginOperation=true;
    try {
      if(typeof action==='string'&&action.startsWith('store-'))return await require('./plugin-store.cjs').handleStore(pluginCatalog,pluginManager,action,payload,storeTest?{showMessageBox:async()=>({response:1})}:dialog,win);
      if(action==='list')return await pluginManager.list();
      if(action==='clear-cache'){
        const removed=await require('./plugin-cache.cjs').clearPluginCache(pluginCatalog,pluginManager);
        const state=await pluginManager.list();state.warnings.push(`${removed} itens de cache removidos. Pacotes instalados, versões de recuperação e projetos preservados.`);return state;
      }
      if(action==='details')return await pluginManager.details(payload?.key);
      if(action==='image')return await pluginManager.image(payload?.key,payload?.relative);
      if(action==='cancel-update')return await pluginManager.cancelUpdate(payload?.key);
      if(action==='rollback'){
        const choice=await dialog.showMessageBox(win,{type:'question',message:'Agendar retorno à versão anterior?',detail:'Será aplicado na próxima abertura. Salve uma cópia do projeto antes de trocar versões.',buttons:['Cancelar','Agendar'],defaultId:0,cancelId:0});
        return choice.response===1?await pluginManager.rollback(payload?.key,payload?.previous):await pluginManager.list();
      }
      if(action==='link'){
        const url=new URL(payload?.url);if(url.protocol!=='https:'||url.username||url.password)throw new Error('Somente links HTTPS são permitidos.');
        await shell.openExternal(url.href);return {};
      }
      if(action==='enable')return await pluginManager.setEnabled(payload?.key,payload?.enabled);
      if(action==='cancel-removal')return await pluginManager.cancelRemoval(payload?.key);
      if(action==='remove'){
        const choice=await dialog.showMessageBox(win,{type:'question',message:'Remover este pacote na próxima abertura?',detail:'Projetos e configurações dos blocos serão preservados. Projetos que usam este plugin podem ficar indisponíveis até reinstalá-lo. Os arquivos serão guardados no arquivo local de plugins.',buttons:['Cancelar','Agendar remoção'],defaultId:0,cancelId:0});
        return choice.response===1?await pluginManager.remove(payload?.key):await pluginManager.list();
      }
      if(action==='install'||action==='install-zip'){
        const selected=await dialog.showOpenDialog(win,action==='install-zip'?{title:'Escolher pacote de plugin',properties:['openFile'],filters:[{name:'Plugin Nodivu / ZIP',extensions:['nodivu-plugin','zip']}]}:{title:'Escolher pasta de plugin (contendo plugin.json e biblioteca .clap)',properties:['openDirectory']});
        if(selected.canceled)return await pluginManager.list();
        let source=selected.filePaths[0],temporary;
        try{
          if(action==='install-zip'){
            temporary=path.join(app.getPath('userData'),'plugin-unpack',require('node:crypto').randomUUID());
            await fs.mkdir(path.dirname(temporary),{recursive:true});
            source=await require('./plugin-archive.cjs').extractArchive(source,temporary);
          }
          const info=await require('./plugin-manager.cjs').inspectFolder(source);
          const choice=await dialog.showMessageBox(win,{type:'question',message:'Instalar '+info.data.title+(info.metadata?' '+info.metadata.version:'')+'?',detail:'Este pacote contém código nativo e poderá executar com os mesmos acessos do Nodivu após reiniciar. Instale apenas de um autor em quem confia. Pacotes com versão mais recente atualizam a instalação existente na próxima abertura. Tamanho: '+(info.bytes/1048576).toFixed(1)+' MiB.',buttons:['Cancelar','Instalar'],defaultId:0,cancelId:0});
          return choice.response===1?await pluginManager.install(source):await pluginManager.list();
        }finally{if(temporary)await fs.rm(temporary,{recursive:true,force:true});}
      }
      throw new Error('Ação de plugins desconhecida.');
    }catch(error){diagnostics.record('plugins.error',{action,message:error.message});return {error:error.message};}
    finally{pluginOperation=false;--pluginQueued;}
    };
    const result=pluginQueue.then(operation,operation);pluginQueue=result.catch(()=>{});return result;
  });
  startEngine();
  const updateEnabled = app.isPackaged && process.platform === 'win32' && !demoMode &&
    (!process.argv.includes('--smoke-test') || updateTest) && require('node:fs').existsSync(path.join(process.resourcesPath,'app-update.yml'));
  const updater = updateEnabled ? require('electron-updater').autoUpdater : null;
  if(updateTest){
    const url=new URL(process.env.NODIVU_UPDATE_TEST_URL);
    if(url.protocol!=='http:'||url.hostname!=='127.0.0.1')throw new Error('Feed de ensaio exige localhost explícito.');
    updater.setFeedURL({provider:'generic',url:url.href,channel:'beta'});
    // Only an explicit smoke test can install without the user's dialog or relaunch.
    const install=updater.quitAndInstall.bind(updater);updater.quitAndInstall=()=>install(true,false);
  }
  updates = new (require('./updates.cjs').Updates)(updater, {
    version:app.getVersion(), log:(event,data)=>diagnostics.record(event,data),
    publish:state=>{if(!win.isDestroyed())win.webContents.send('updates:state',state);},
    prepareInstall:async()=>{
      if(projectDirty && updateTest)return false;
      if(projectDirty){await dialog.showMessageBox(win,{type:'info',message:'Salve seu projeto antes de atualizar.',detail:'As alterações continuam abertas. Depois de salvar, clique em Reiniciar e atualizar novamente.'});return false;}
      const choice=updateTest?{response:1}:await dialog.showMessageBox(win,{type:'question',message:'Reiniciar o Nodivu e instalar a atualização?',detail:'O áudio será interrompido durante a atualização. Seus projetos e configurações serão preservados.',buttons:['Depois','Reiniciar e atualizar'],defaultId:0,cancelId:0});
      if(choice.response!==1)return false;
      quitting=true;
      if(engine)await engine.stop();
      await diagnostics.flush();
      return true;
    }
  });
  ipcMain.handle('updates:action',async(event,action)=>{trusted(event);return action==='status'?updates.snapshot():updates.run(action);});
  ipcMain.handle('cable:installer',async event=>{
    trusted(event);
    const file=path.resolve(__dirname,'../../third-party/vb-cable/VBCABLE_Setup_x64.exe');
    try{await fs.access(file);const choice=await dialog.showMessageBox(win,{type:'question',message:'Abrir o instalador oficial do VB-CABLE?',detail:'VB-Audio Software · donationware. Pode pedir administrador e reinício do Windows. Se o cabo já funciona, não reinstale.',buttons:['Cancelar','Abrir instalador'],defaultId:0,cancelId:0});
      if(choice.response===1){const error=await shell.openPath(file);if(error)throw new Error(error);}return {ok:true};
    }catch(error){return {ok:false,error:{message:'Instalador do cabo indisponível: '+error.message}};}
  });
  if(updateEnabled){const check=setTimeout(()=>void updates.run('check'),15000);check.unref();const interval=setInterval(()=>void updates.run('check'),6*60*60*1000);interval.unref();}
  ipcMain.handle('engine:request', async (event, command, params) => {
    if (quitting) return {ok:false,error:{message:'Aplicativo encerrando.'}};
    trusted(event);
    if (!commands.has(command) || !params || typeof params !== 'object' || Array.isArray(params))
      return { ok: false, error: { message: 'Comando inválido.' } };
    try {
      if(startupRecoveryTest && command==='system.hello' && testHelloCalls++===0)throw new Error('Falha simulada de handshake para teste de recuperação');
      if(startupRecoveryTest && command==='devices.list' && testDeviceCalls++<2)throw new Error('Falha simulada: Enumerar endpoints MMDevice: 0xE000020B');
      const result=await engine.request(command, params);
      if(command==='devices.list')diagnostics.record('devices.inventory',{count:result.devices.length,warnings:(result.warnings||[]).join('\n')});
      if(command==='session.snapshot')for(const field of ['last_error','capture_error'])if(result[field])diagnostics.record('audio.error',{kind:field,code:result[field].code,message:result[field].message});
      return { ok: true, result };
    }
    catch (error) { diagnostics.record('request.error', {command,code:error.code||'',message:error.message}); return { ok: false, error: { message: error.message, code: error.code } }; }
  });
  ipcMain.handle('diagnostics:open', async event => {
    trusted(event); await diagnostics.flush();
    if(diagnostics.error)return {ok:false,error:{message:'Não foi possível gravar o diagnóstico: '+diagnostics.error}};
    const error=await shell.openPath(diagnostics.directory);
    return error?{ok:false,error:{message:error}}:{ok:true};
  });
  ipcMain.on('diagnostics:renderer', (event, message) => {
    trusted(event);
    if(typeof message==='string')diagnostics.record('renderer.error',{message:message.slice(0,2048)});
  });
  ipcMain.on('project:dirty', (event, dirty) => {trusted(event); projectDirty = dirty === true;});
  ipcMain.handle('project:recents', async event => {
    trusted(event);
    try {return {ok:true,files:await recentProjects.list()};}
    catch(error) {return {ok:false,error:{message:'Não foi possível ler os projetos recentes: '+error.message}};}
  });
  ipcMain.handle('project:file', async (event, action, payload) => {
    trusted(event);
    if(projectOperation||quitting)return {ok:false,error:{message:'Outra operação de projeto está em andamento.'}};
    projectOperation=true;
    try {
      if (action === 'save') {
        const {project} = await engine.request('project.validate', payload);
        const choice = await dialog.showSaveDialog(win,{defaultPath:projectPath || path.join(projectsDirectory,'Projeto.nodivu.json'),filters:[{name:'Projeto Nodivu',extensions:['json']}]});
        if (choice.canceled || !choice.filePath) return {ok:true,canceled:true};
        await writeProject(choice.filePath, resourcesForFile(project, choice.filePath, false));
        projectPath=choice.filePath; projectDirty=false;
        return {ok:true,path:projectPath,...await rememberRecent(projectPath),...await clearCurrentRecovery(),recovery:recovery.status()};
      }
      if(action==='folder'){const error=await shell.openPath(projectsDirectory);if(error)throw new Error(error);return {ok:true};}
      if (!['open','open-recent'].includes(action)) throw new Error('Ação de projeto inválida.');
      if (projectDirty && (await dialog.showMessageBox(win,{type:'question',message:'Abrir outro projeto e descartar alterações não salvas?',buttons:['Cancelar','Abrir outro'],defaultId:0,cancelId:0})).response!==1) return {ok:true,canceled:true};
      let file;
      if(action==='open-recent') file=await recentProjects.resolve(payload.path);
      else {
        const choice = await dialog.showOpenDialog(win,{defaultPath:projectsDirectory,properties:['openFile'],filters:[{name:'Projeto Nodivu',extensions:['json']}]});
        if (choice.canceled) return {ok:true,canceled:true};
        file=choice.filePaths[0];
      }
      const contents=await readProject(file);
      const {project}=await engine.request('project.validate',{contents,expected_revision:payload.expected_revision});
      const result=await engine.request('project.open',{contents:JSON.stringify(resourcesForFile(project,file,true)),expected_revision:payload.expected_revision});
      projectPath=file; projectDirty=false;
      return {ok:true,path:file,...result,...await rememberRecent(file),...await clearCurrentRecovery(),recovery:recovery.status()};
    } catch(error) {return {ok:false,error:{message:error.message,code:error.code}};}
    finally {projectOperation=false;}
  });
  ipcMain.handle('project:recovery',async(event,action,payload={})=>{
    trusted(event);
    if(action==='status')return {ok:true,recovery:recovery.status()};
    if(projectOperation||quitting)return {ok:false,error:{message:'Outra operação de projeto está em andamento.'}};
    projectOperation=true;
    try {
      if(action==='save'){
        if(recovery.pending)throw new Error('Resolva a recuperação anterior antes de salvar outra cópia.');
        const {project}=await engine.request('project.validate',payload);
        const absolute=resourcesForFile(project,projectPath||path.join(projectsDirectory,'Projeto.nodivu.json'),true);
        await recovery.save(absolute);
      }else if(action==='clean'){
        if(projectDirty)throw new Error('Há alterações não salvas; a cópia foi preservada.');
        await recovery.clear();
      }else if(action==='discard'){
        const choice=await dialog.showMessageBox(win,{type:'question',message:'Descartar a cópia de recuperação?',detail:'O projeto aberto e os arquivos salvos não serão alterados.',buttons:['Cancelar','Descartar cópia'],defaultId:0,cancelId:0});
        if(choice.response!==1)return {ok:true,canceled:true,recovery:recovery.status()};
        await recovery.clear(true);
      }else if(action==='restore'){
        if(projectDirty&&(await dialog.showMessageBox(win,{type:'question',message:'Restaurar a cópia e descartar as alterações abertas?',buttons:['Cancelar','Restaurar cópia'],defaultId:0,cancelId:0})).response!==1)return {ok:true,canceled:true};
        const contents=await recovery.read();
        const result=await engine.request('project.open',{contents,expected_revision:payload.expected_revision});
        recovery.accept();projectPath=null;projectDirty=true;
        return {ok:true,path:null,...result,recovery:recovery.status()};
      }else throw new Error('Ação de recuperação inválida.');
      return {ok:true,recovery:recovery.status()};
    }catch(error){diagnostics.record('recovery.error',{message:error.message});return {ok:false,recovery:recovery.status(),error:{message:error.message,code:error.code}};}
    finally{projectOperation=false;}
  });
  ipcMain.handle('plugin:choose-file',async event=>{trusted(event);const result=await dialog.showOpenDialog(win,{properties:['openFile'],filters:[{name:'Áudio · MP3, WAV, M4A',extensions:['mp3','wav','m4a']}]});return result.canceled?null:result.filePaths[0];});
  ipcMain.handle('engine:restart', async event => { trusted(event); await engine.stop(); startEngine(); });
  await win.loadURL(PAGE);
  if(process.argv.includes('--smoke-test')&&process.argv.includes('--test-hotkeys')){
    try{await require('./smoke-hotkeys.cjs')(win,hotkeys);}catch(error){console.error(error);exitCode=1;}await shutdown();return;
  }
  if(storeTest&&process.argv.includes('--test-utility-store')){
    try{await require('./smoke-utility-store.cjs')(win,pluginManager);}catch(error){console.error(error);exitCode=1;}
    await shutdown();return;
  }
  if(process.argv.includes('--devtools'))win.webContents.openDevTools({mode:'detach'});
  if(updateTest){await require('./smoke-update.cjs')(win,updates);return;}
  if(recoveryTest){try{await require('./smoke-recovery.cjs')(win,recovery,engine);}catch(error){console.error(error);exitCode=1;}await shutdown();return;}
  if (process.argv.includes('--smoke-test')) {
    try { if(startupRecoveryTest)await require('./smoke-startup.cjs')(win,diagnostics); await require('./smoke.cjs')(win, process.argv.includes('--test-audio')); if(process.argv.includes('--test-plugin'))await require('./smoke-plugin.cjs')(win); if(process.argv.includes('--test-mp3'))await require('./smoke-mp3.cjs')(win); if(process.argv.includes('--test-worker'))await require('./smoke-worker.cjs')(win); if(process.argv.includes('--test-routing'))await require('./smoke-routing.cjs')(win); if(process.argv.includes('--test-viewport'))await require('./smoke-viewport.cjs')(win); if(process.argv.includes('--test-project'))await require('./smoke-project.cjs')(win); if(process.argv.includes('--test-capture'))await require('./smoke-capture.cjs')(win); await require('./smoke-studio.cjs')(win); if(process.argv.includes('--test-cleanup'))await require('./smoke-cleanup.cjs')(win); if(!process.env.NODIVU_PLUGIN_MANIFEST)await require('./smoke-plugin-manager.cjs')(win,pluginManager); if(storeTest){if(process.argv.includes('--test-utility-store'))await require('./smoke-utility-store.cjs')(win,pluginManager);else await require('./smoke-plugin-store.cjs')(win);} await require('./smoke-virtual-cable.cjs')(win); if(process.argv.includes('--test-monitor'))await require('./smoke-monitor.cjs')(win); console.log('PASS: Electron, canvas, API e encerramento.'); }
    catch (error) { console.error(error); exitCode = 1; }
    await shutdown();
  }
}).catch(async error => { console.error(error); diagnostics.record('app.fatal',{message:error.message}); exitCode=1; await shutdown(); });

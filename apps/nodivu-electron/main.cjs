const { app, dialog, BrowserWindow, ipcMain, protocol, session, shell } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const { Engine } = require('./engine.cjs');
const { Diagnostics } = require('./diagnostics.cjs');
const os = require('node:os');
const {RecentProjects} = require('./recent-projects.cjs');
let recentProjects, projectsDirectory, installedManifest, updates, recovery;
let projectOperation=false;
const demoMode=process.argv.includes('--frontend-demo');
const {readProject, writeProject, resourcesForFile} = require('./project-files.cjs');
let projectPath = null, projectDirty = false, confirmingClose = false;
const PAGE = 'nodivu://app/index.html';
const commands = new Set(['system.hello','plugins.list','capture.targets','capture.configure','plugin.command','devices.list','session.snapshot','node.add','graph.apply','audio.mute','audio.retry','audio.monitor']);
const executable = app.isPackaged ? path.join(__dirname, 'bin/nodivu-app-backend.exe') : path.resolve(__dirname, '../../target/release/nodivu-app-backend.exe');
let win, engine, quitting = false, exitCode = 0;
const recoveryTest=process.argv.includes('--smoke-test')&&process.argv.includes('--test-recovery');
const recoveryTestId=process.env.NODIVU_RECOVERY_TEST_ID;
if(recoveryTest&&!/^[a-zA-Z0-9-]{1,80}$/.test(recoveryTestId||''))throw new Error('ID de ensaio de recuperação inválido.');
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
  const env={...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_APP_ROOT_PID:String(process.pid)};
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
    const files = { '/index.html': 'text/html', '/renderer.js': 'text/javascript', '/viewport.js': 'text/javascript', '/virtual-cable.mjs': 'text/javascript', '/playback-state.mjs': 'text/javascript', '/capture-source.mjs': 'text/javascript', '/style.css': 'text/css', ...Object.fromEntries(['index','capture','output','gain','tone','plugin','mixer','meter'].map(name => ['/blocks/'+name+'.js','text/javascript'])) };
    if (url.host !== 'app' || !Object.hasOwn(files, url.pathname)) return new Response('', { status: 404 });
    return new Response(await fs.readFile(path.join(__dirname, 'ui', url.pathname.slice(1))),
      { headers: { 'Content-Type': files[url.pathname] + '; charset=utf-8' } });
  });
  session.defaultSession.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
  session.defaultSession.setPermissionCheckHandler(() => false);
  win = new BrowserWindow({ width: 1440, height: 900, minWidth: 960, minHeight: 650,
    title: 'Nodivu — projeto de áudio', backgroundColor: '#101319', autoHideMenuBar: true,
    webPreferences: { preload: path.join(__dirname, 'preload.cjs'), contextIsolation: true, sandbox: true,
      nodeIntegration: false, backgroundThrottling: false } });
  win.removeMenu();
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
  if(process.argv.includes('--devtools'))win.webContents.openDevTools({mode:'detach'});
  if(updateTest){await require('./smoke-update.cjs')(win,updates);return;}
  if(recoveryTest){try{await require('./smoke-recovery.cjs')(win,recovery,engine);}catch(error){console.error(error);exitCode=1;}await shutdown();return;}
  if (process.argv.includes('--smoke-test')) {
    try { if(startupRecoveryTest)await require('./smoke-startup.cjs')(win,diagnostics); await require('./smoke.cjs')(win, process.argv.includes('--test-audio')); if(process.argv.includes('--test-plugin'))await require('./smoke-plugin.cjs')(win); if(process.argv.includes('--test-mp3'))await require('./smoke-mp3.cjs')(win); if(process.argv.includes('--test-worker'))await require('./smoke-worker.cjs')(win); if(process.argv.includes('--test-routing'))await require('./smoke-routing.cjs')(win); if(process.argv.includes('--test-viewport'))await require('./smoke-viewport.cjs')(win); if(process.argv.includes('--test-project'))await require('./smoke-project.cjs')(win); if(process.argv.includes('--test-capture'))await require('./smoke-capture.cjs')(win); await require('./smoke-virtual-cable.cjs')(win); if(process.argv.includes('--test-monitor'))await require('./smoke-monitor.cjs')(win); console.log('PASS: Electron, canvas, API e encerramento.'); }
    catch (error) { console.error(error); exitCode = 1; }
    await shutdown();
  }
}).catch(async error => { console.error(error); diagnostics.record('app.fatal',{message:error.message}); exitCode=1; await shutdown(); });

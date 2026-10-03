const { app, dialog, BrowserWindow, ipcMain, protocol, session, shell } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');
const { Engine } = require('./engine.cjs');
const { Diagnostics } = require('./diagnostics.cjs');
const os = require('node:os');
const {RecentProjects} = require('./recent-projects.cjs');
let recentProjects, projectsDirectory;
const demoMode=process.argv.includes('--frontend-demo');
const {readProject, writeProject, resourcesForFile} = require('./project-files.cjs');
let projectPath = null, projectDirty = false, confirmingClose = false;
const PAGE = 'nodivu://app/index.html';
const commands = new Set(['system.hello','plugins.list','capture.targets','capture.configure','plugin.command','devices.list','session.snapshot','node.add','graph.apply','audio.mute','audio.retry']);
const executable = app.isPackaged ? path.join(__dirname, 'bin/nodivu-app-backend.exe') : path.resolve(__dirname, '../../target/release/nodivu-app-backend.exe');
let win, engine, quitting = false, exitCode = 0;
protocol.registerSchemesAsPrivileged([{ scheme: 'nodivu', privileges: { standard: true, secure: true } }]);
const profileRoot=app.isPackaged ? path.join(app.getPath('appData'),'Nodivu') : path.resolve(__dirname,'../../.local/electron-profile');
// Isolate Chromium caches as well as history from an already open user session.
app.setPath('userData', process.argv.includes('--smoke-test')?path.join(app.getPath('temp'),'Nodivu-smoke-'+process.pid):profileRoot);
const diagnostics = new Diagnostics(path.join(app.getPath('userData'), 'logs'));
diagnostics.record('app.start', { version: app.getVersion(), windows: os.release(), os: os.version(), arch: process.arch, electron: process.versions.electron, demo: demoMode, smoke: process.argv.includes('--smoke-test') });
// Deliberate fault injection is restricted to this opt-in integration test.
const startupRecoveryTest = process.argv.includes('--smoke-test') && process.argv.includes('--test-startup-recovery');
let testHelloCalls=0, testDeviceCalls=0;
function startEngine() {
  const manifest=process.env.NODIVU_PLUGIN_MANIFEST || path.resolve(__dirname, app.isPackaged?'../../plugins/plugin.json':'../../.local/plugins/plugin.json');
  const env={...process.env,NODIVU_SCAN_PLUGINS:'1',NODIVU_APP_ROOT_PID:String(process.pid)};
  if(process.env.NODIVU_PLUGIN_MANIFEST || require('node:fs').existsSync(path.dirname(manifest))) env.NODIVU_PLUGIN_MANIFEST=manifest;
  engine = demoMode ? new (require('./demo-engine.cjs').DemoEngine)() : new Engine(executable,env);
  engine.on('fault', message => { diagnostics.record('backend.fault', {message}); if (win && !win.isDestroyed()) win.webContents.send('engine:fault', message); });
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
    if (confirmingClose) return;
    confirmingClose = true;
    void (async()=>{
      if (!projectDirty || (await dialog.showMessageBox(win,{type:'question',message:'Fechar e descartar alterações não salvas?',buttons:['Continuar editando','Descartar e fechar'],defaultId:0,cancelId:0})).response===1) await shutdown();
      confirmingClose=false;
    })();
  });
  for (const event of ['minimize', 'restore']) win.on(event, () => win.webContents.send('window:minimized', win.isMinimized()));
  startEngine();
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
    try {
      if (action === 'save') {
        const {project} = await engine.request('project.validate', payload);
        const choice = await dialog.showSaveDialog(win,{defaultPath:projectPath || path.join(projectsDirectory,'Projeto.nodivu.json'),filters:[{name:'Projeto Nodivu',extensions:['json']}]});
        if (choice.canceled || !choice.filePath) return {ok:true,canceled:true};
        await writeProject(choice.filePath, resourcesForFile(project, choice.filePath, false));
        projectPath=choice.filePath; projectDirty=false;
        return {ok:true,path:projectPath,...await rememberRecent(projectPath)};
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
      return {ok:true,path:file,...result,...await rememberRecent(file)};
    } catch(error) {return {ok:false,error:{message:error.message,code:error.code}};}
  });
  ipcMain.handle('plugin:choose-file',async event=>{trusted(event);const result=await dialog.showOpenDialog(win,{properties:['openFile'],filters:[{name:'Áudio · MP3, WAV, M4A',extensions:['mp3','wav','m4a']}]});return result.canceled?null:result.filePaths[0];});
  ipcMain.handle('engine:restart', async event => { trusted(event); await engine.stop(); startEngine(); });
  await win.loadURL(PAGE);
  if(process.argv.includes('--devtools'))win.webContents.openDevTools({mode:'detach'});
  if (process.argv.includes('--smoke-test')) {
    try { if(startupRecoveryTest)await require('./smoke-startup.cjs')(win,diagnostics); await require('./smoke.cjs')(win, process.argv.includes('--test-audio')); if(process.argv.includes('--test-plugin'))await require('./smoke-plugin.cjs')(win); if(process.argv.includes('--test-mp3'))await require('./smoke-mp3.cjs')(win); if(process.argv.includes('--test-worker'))await require('./smoke-worker.cjs')(win); if(process.argv.includes('--test-routing'))await require('./smoke-routing.cjs')(win); if(process.argv.includes('--test-viewport'))await require('./smoke-viewport.cjs')(win); if(process.argv.includes('--test-project'))await require('./smoke-project.cjs')(win); if(process.argv.includes('--test-capture'))await require('./smoke-capture.cjs')(win); await require('./smoke-virtual-cable.cjs')(win); console.log('PASS: Electron, canvas, API e encerramento.'); }
    catch (error) { console.error(error); exitCode = 1; }
    await shutdown();
  }
}).catch(async error => { console.error(error); diagnostics.record('app.fatal',{message:error.message}); exitCode=1; await shutdown(); });

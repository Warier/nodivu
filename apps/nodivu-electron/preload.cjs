const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('nodivu', {
  hotkeys:(action,payload)=>ipcRenderer.invoke('hotkeys:manage',action,payload),
  hotkeyContext:actions=>ipcRenderer.send('hotkeys:context',actions),
  onHotkeys:callback=>ipcRenderer.on('hotkeys:state',(_event,state)=>callback(state)),
  onHotkey:callback=>ipcRenderer.on('hotkeys:execute',async(_event,request)=>{try{await callback(request.binding);ipcRenderer.send('hotkeys:result',{token:request.token,ok:true});}catch(e){ipcRenderer.send('hotkeys:result',{token:request.token,ok:false,error:String(e.message).slice(0,1024)});}}),
  onMenu:callback=>ipcRenderer.on('app:menu',(_event,action)=>callback(action)),
  plugins:(action,payload)=>ipcRenderer.invoke('plugins:manage',action,payload),
  update:action=>ipcRenderer.invoke('updates:action',action),
  onUpdate:callback=>ipcRenderer.on('updates:state',(_event,state)=>callback(state)),
  installCable:()=>ipcRenderer.invoke('cable:installer'),
  openDiagnostics:()=>ipcRenderer.invoke('diagnostics:open'),
  reportFault:message=>ipcRenderer.send('diagnostics:renderer',String(message).slice(0,2048)),
  onRecovery:callback=>ipcRenderer.on('recovery:state',(_event,state)=>callback(state)),
  recovery:(action,payload)=>ipcRenderer.invoke('project:recovery',action,payload),
  recentProjects:()=>ipcRenderer.invoke('project:recents'),
  projectFile:(action,payload)=>ipcRenderer.invoke('project:file',action,payload),
  setProjectDirty:dirty=>ipcRenderer.send('project:dirty',dirty),
  chooseFile:()=>ipcRenderer.invoke('plugin:choose-file'),
  request: (command, params = {}) => ipcRenderer.invoke('engine:request', command, params),
  restart: () => ipcRenderer.invoke('engine:restart'),
  onFault: callback => ipcRenderer.on('engine:fault', (_event, message) => callback(message)),
  onMinimized: callback => ipcRenderer.on('window:minimized', (_event, minimized) => callback(minimized))
});

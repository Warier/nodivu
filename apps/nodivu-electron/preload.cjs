const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('nodivu', {
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

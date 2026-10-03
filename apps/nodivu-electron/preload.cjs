const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('nodivu', {
  openDiagnostics:()=>ipcRenderer.invoke('diagnostics:open'),
  reportFault:message=>ipcRenderer.send('diagnostics:renderer',String(message).slice(0,2048)),
  recentProjects:()=>ipcRenderer.invoke('project:recents'),
  projectFile:(action,payload)=>ipcRenderer.invoke('project:file',action,payload),
  setProjectDirty:dirty=>ipcRenderer.send('project:dirty',dirty),
  chooseFile:()=>ipcRenderer.invoke('plugin:choose-file'),
  request: (command, params = {}) => ipcRenderer.invoke('engine:request', command, params),
  restart: () => ipcRenderer.invoke('engine:restart'),
  onFault: callback => ipcRenderer.on('engine:fault', (_event, message) => callback(message)),
  onMinimized: callback => ipcRenderer.on('window:minimized', (_event, minimized) => callback(minimized))
});

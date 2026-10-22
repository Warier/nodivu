module.exports=({Menu,win})=>{
 const send=action=>()=>win.webContents.send('app:menu',action);
 const item=(label,action)=>({label,click:send(action)});
 Menu.setApplicationMenu(Menu.buildFromTemplate([
  {label:'Arquivo',submenu:[item('Abrir projeto…  Ctrl+O','project-open'),item('Salvar projeto…  Ctrl+S','project-save'),item('Pasta de projetos','project-folder'),{type:'separator'},{label:'Sair',click:()=>win.close()}]},
  {label:'Editar',submenu:[{label:'Recortar',role:'cut'},{label:'Copiar',role:'copy'},{label:'Colar',role:'paste'},{label:'Selecionar texto',role:'selectAll'}]},
  {label:'Visualizar',submenu:[item('Enquadrar projeto','fit-view'),item('Organizar blocos','center'),item('Biblioteca','library-toggle'),item('Inspetor do bloco','inspect-selected'),{type:'separator'},item('Ampliar','zoom-in'),item('Reduzir','zoom-out'),item('Zoom 100%','zoom-reset')]},
  {label:'Áudio',submenu:[item('Silenciar / restaurar tudo','mute-all'),item('Escuta local','listen-shortcut'),item('Atualizar dispositivos','refresh')]},
  {label:'Configurações',submenu:[item('Atalhos de teclado…','hotkeys-open'),item('Atalhos do bloco selecionado…','hotkeys-selected'),item('Plugins…','plugins-library'),item('Sistema','settings-toggle')]},
  {label:'Ajuda',submenu:[item('Logs de diagnóstico','diagnostics-open'),item('Atualizações e versão','updates-panel')]}
 ]));
};

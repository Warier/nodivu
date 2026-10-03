// Somente apresentação declarativa. Nenhum HTML/JS/CSS do pacote é executado.
export function pluginDescriptor(packageInfo) {
  const a=packageInfo.appearance;
  const controls=[...a.controls];
  for(const p of packageInfo.parameters)if(!controls.some(c=>c.parameter===p.id))
    controls.push({parameter:p.id,label:'Parâmetro '+p.id,widget:'number',unit:'',group:''});
  return {kind:'plugin',pluginId:packageInfo.id,title:a.title,subtitle:packageInfo.worker?'Worker Python · +20 ms':'Plugin local · CLAP',icon:'◇',worker:packageInfo.worker,
    accent:a.accent,layout:a.layout,inputs:packageInfo.source?0:1,outputs:packageInfo.consumer?0:1,captureSource:packageInfo.capture_source,filePlayer:packageInfo.file_player,fileTransport:packageInfo.file_transport,
    fields:[...controls.map(c=>{
      const p=packageInfo.parameters.find(p=>p.id===c.parameter);
      return {key:'parameter:'+p.id,label:c.label,group:c.group,unit:c.unit,readonly:p.readonly,
        type:c.widget==='slider'?'range':c.widget==='toggle'?'checkbox':'number',
        min:p.min,max:p.max,default:p.default,step:p.stepped?1:Math.max((p.max-p.min)/1000,0.000001)};
    }),{key:'bypass',label:packageInfo.source?'Silenciar fonte':packageInfo.consumer?'Suspender consumidor':packageInfo.worker?'Bypass · original com atraso alinhado':'Bypass · manter o áudio original',type:'checkbox',group:'Rota'}],
    note:a.description};
}

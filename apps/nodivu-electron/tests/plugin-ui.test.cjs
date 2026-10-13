const {test}=require('node:test');
const assert=require('node:assert/strict');
const fixture=()=>({id:'org.example.test',source:false,consumer:false,appearance:{title:'Efeito',description:'Texto',layout:'grid',accent:'#abcdef',controls:[{parameter:9,label:'Ganho',widget:'slider',unit:'×',group:'Sinal'}]},parameters:[{id:7,min:0,max:1,default:0,readonly:true,stepped:true},{id:9,min:0,max:4,default:1,readonly:false,stepped:false}]});
test('legacy plugin presentation retains IDs, undeclared parameters and read-only metadata',async()=>{
 const {pluginDescriptor}=await import('../ui/blocks/plugin.js');const d=pluginDescriptor(fixture());
 assert.equal(d.fields[0].key,'parameter:9');assert.equal(d.fields[0].max,4);assert.equal(d.fields[0].quick,true);
 const fallback=d.fields.find(f=>f.key==='parameter:7');assert.equal(fallback.type,'number');assert.equal(fallback.readonly,true);assert.equal(fallback.step,1);
 assert.equal(d.fields.at(-1).key,'bypass');assert.equal(d.inputs,1);assert.equal(d.outputs,1);
});
test('declarative pages, select zero value and advanced controls reach the host without losing metadata',async()=>{
 const {pluginDescriptor}=await import('../ui/blocks/plugin.js');const p=fixture();
 p.appearance.controls[0]={...p.appearance.controls[0],widget:'select',page:'Modo',advanced:true,help:'<script>texto</script>',options:[{value:0,label:'Zero'},{value:1,label:'Um'}]};
 const f=pluginDescriptor(p).fields[0];assert.equal(f.type,'select');assert.equal(f.options[0].value,0);assert.equal(f.page,'Modo');assert.equal(f.quick,false);assert.equal(f.help,'<script>texto</script>');assert.equal(f.advanced,true);
});
test('new UI hints do not change source/consumer ports or file transport capabilities',async()=>{
 const {pluginDescriptor}=await import('../ui/blocks/plugin.js');const p=fixture();p.source=true;p.file_player=true;p.file_transport=true;
 const source=pluginDescriptor(p);assert.equal(source.inputs,0);assert.equal(source.outputs,1);assert.equal(source.fileTransport,true);
 p.source=false;p.consumer=true;const sink=pluginDescriptor(p);assert.equal(sink.inputs,1);assert.equal(sink.outputs,0);
});

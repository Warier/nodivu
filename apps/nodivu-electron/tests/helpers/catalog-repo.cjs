// Test-only TUF publisher. Keys are ephemeral, never a production trust anchor.
const crypto=require('node:crypto');
const {Metadata,Root,Key,Signature,Targets,TargetFile,MetaFile,Snapshot,Timestamp}=require('@tufjs/models');
const {zip}=require('./zip.cjs');
const hash=data=>({sha256:crypto.createHash('sha256').update(data).digest('hex')});
class TestRepository{
 constructor(){
  this.files=new Map();this.keys={};this.revision=0;this.entries=[];
  const root=new Root({version:1,expires:new Date(Date.now()+86400000).toISOString(),consistentSnapshot:false});
  for(const role of ['root','timestamp','snapshot','targets']){
   const keys=crypto.generateKeyPairSync('ed25519');this.keys[role]=keys.privateKey;
   root.addKey(new Key({keyID:role,keyType:'ed25519',scheme:'ed25519',keyVal:{public:keys.publicKey.export({format:'der',type:'spki'}).subarray(-32).toString('hex')}}),role);
  }
  this.root=this.signed(root,'root');
 }
 signed(data,role){const metadata=new Metadata(data);metadata.sign(bytes=>new Signature({keyID:role,sig:crypto.sign(null,bytes,this.keys[role]).toString('hex')}));return Buffer.from(JSON.stringify(metadata.toJSON()));}
 package(id,version='1.0.0',library='not an executable, test fixture only'){
  const prefix=id+'/'+version;
  const readme='# Plugin de teste\n\n| Controle | Efeito |\n|---|---|\n| Ganho | Volume |\n\n![Logo](logo.png)';
  const logo=Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a0tQAAAAASUVORK5CYII=','base64');
  const archive=zip([{name:'plugin.json',data:JSON.stringify({schema_version:1,plugin_id:id,title:'Teste '+id,library:'effect.clap',description:'Pacote do ensaio',accent:'#72BCF7',layout:'stack',controls:[]})},{name:'effect.clap',data:library},{name:'nodivu-package.json',data:JSON.stringify({schema_version:1,plugin_id:id,version,author:'Test author'})},{name:'README.md',data:readme},{name:'logo.png',data:logo}]);
  this.files.set('/targets/'+prefix+'.zip',archive);this.files.set('/targets/'+prefix+'.md',Buffer.from(readme));this.files.set('/targets/'+prefix+'.png',logo);
  const entry={id,version,title:'Plugin '+id,author:'Test author',publisher_id:id.startsWith('org.nodivu')?'nodivu':'community',description:'Pacote simulado do ensaio, sem DSP.',archive:prefix+'.zip',readme:prefix+'.md',images:{'logo.png':prefix+'.png'},logo:prefix+'.png',download_bytes:archive.length,platform:`${process.platform}-${process.arch}`,host_api:1,resource_tier:'unknown'};
  this.entries=this.entries.filter(e=>e.id!==id);this.entries.push(entry);return entry;
 }
 publish(expires=new Date(Date.now()+3600000).toISOString()){
  const version=++this.revision;this.files.set('/targets/catalog.json',Buffer.from(JSON.stringify({schema_version:1,plugins:this.entries})));
  const targets=new Targets({version,expires});
  for(const [name,bytes] of this.files)if(name.startsWith('/targets/'))targets.addTarget(new TargetFile({path:name.slice(9),length:bytes.length,hashes:hash(bytes)}));
  const targetsData=this.signed(targets,'targets');this.files.set('/metadata/targets.json',targetsData);
  const snapshot=this.signed(new Snapshot({version,expires,meta:{'targets.json':new MetaFile({version,length:targetsData.length,hashes:hash(targetsData)})}}),'snapshot');this.files.set('/metadata/snapshot.json',snapshot);
  this.files.set('/metadata/timestamp.json',this.signed(new Timestamp({version,expires,snapshotMeta:new MetaFile({version,length:snapshot.length,hashes:hash(snapshot)})}),'timestamp'));
 }
 source(base){return {schema_version:1,root:this.root.toString(),metadata_url:base+'/metadata/',target_url:base+'/targets/',official_publishers:['nodivu']};}
 handler(req,res){const data=this.files.get(req.url);if(!data){res.writeHead(404);res.end();return;}res.writeHead(200,{'Content-Length':data.length});res.end(data);}
}
module.exports={TestRepository};

const fs=require('node:fs/promises');
const {createWriteStream}=require('node:fs');
const path=require('node:path');
const {Transform}=require('node:stream');
const {pipeline}=require('node:stream/promises');
const {crc32}=require('node:zlib');
const yauzl=require('yauzl');
const LIMIT=256*1024*1024;

function safeName(name){
 if(typeof name!=='string'||name.includes('\\')||name.startsWith('/')||name.length>240)throw new Error('Caminho inválido no ZIP.');
 const parts=name.replace(/\/$/,'').split('/');
 if(parts.length>13||parts.some(p=>!p||p==='.'||p==='..'||/[\x00-\x1f:<>"|?*]/.test(p)||/[. ]$/.test(p)||/^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\.|$)/i.test(p)))throw new Error('Caminho inválido no ZIP.');
 return parts.join(path.sep);
}
// Destination must be a new, private directory. Extraction never runs package scripts.
async function extractArchive(file,destination){
 if((await fs.stat(file)).size>LIMIT)throw new Error('Arquivo ZIP excede 256 MiB.');
 await fs.mkdir(destination,{recursive:false});
 const zip=await yauzl.openPromise(file,{lazyEntries:true,validateEntrySizes:true,strictFileNames:true});
 let count=0,total=0;
 const seen=new Set();
 try{
  for await(const entry of zip.eachEntry()){
   if(++count>2048)throw new Error('ZIP excede 2048 entradas.');
   const name=safeName(entry.fileName),mode=(entry.externalFileAttributes>>>16)&0xf000;
   if(mode&&mode!==0x8000&&mode!==0x4000)throw new Error('ZIP contém link ou arquivo especial.');
   if(entry.generalPurposeBitFlag&1)throw new Error('ZIP criptografado não é suportado.');
   const folded=name.toLowerCase();if(seen.has(folded))throw new Error('Caminho duplicado no ZIP.');seen.add(folded);
   if(entry.uncompressedSize>LIMIT-total)throw new Error('ZIP expandido excede 256 MiB.');
   const target=path.join(destination,name);
   if(entry.fileName.endsWith('/')){await fs.mkdir(target,{recursive:true});continue;}
   await fs.mkdir(path.dirname(target),{recursive:true});
   let checksum=0;
   const bounded=new Transform({transform(chunk,_encoding,done){
    total+=chunk.length;
    if(total>LIMIT)return done(new Error('ZIP expandido excede 256 MiB.'));
    checksum=crc32(chunk,checksum);done(null,chunk);
   }});
   await pipeline(await zip.openReadStreamPromise(entry),bounded,createWriteStream(target,{flags:'wx'}));
   if(checksum!==entry.crc32)throw new Error('ZIP corrompido: checksum não corresponde.');
  }
 }finally{zip.close();}
 // Accept packages at root or one enclosing folder, as commonly produced by ZIP tools.
 if(await fs.stat(path.join(destination,'plugin.json')).then(s=>s.isFile(),()=>false))return destination;
 const children=await fs.readdir(destination,{withFileTypes:true});
 if(children.length===1&&children[0].isDirectory())return path.join(destination,children[0].name);
 throw new Error('ZIP precisa conter plugin.json na raiz ou em uma única pasta.');
}
module.exports={extractArchive,safeName};

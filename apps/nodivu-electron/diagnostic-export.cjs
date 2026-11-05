const fs = require('node:fs/promises');
const path = require('node:path');

// Export only: local troubleshooting logs are preserved verbatim. Aliases stay
// consistent inside one report, without persisting a lookup table of identities.
function redactor(privateRoots = []) {
  const aliases = new Map();
  const identity = value => {
    const key = value.toLowerCase();
    if (!aliases.has(key)) aliases.set(key, `<id:${aliases.size + 1}>`);
    return aliases.get(key);
  };
  const escape = value => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const roots = privateRoots.filter(Boolean).sort((a,b)=>b.length-a.length)
    .map(root => new RegExp(escape(root).replaceAll('\\\\', '[\\\\/]'), 'gi'));
  function value(input) {
    if (typeof input === 'string') {
      // Quoted filesystem paths occur in Node/Windows error messages. Do not
      // retain filenames, which may themselves contain personal information.
      let text = input.replace(/(['"])(?:[A-Za-z]:[\\/]|\\\\)[^'"\r\n]*\1/g, '<arquivo-local>');
      for (const root of roots) text = text.replace(root, '<pasta-privada>');
      return text.replace(/\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b/gi, identity)
        .replace(/\bS-1-5-21(?:-\d+){3,4}\b/gi, identity);
    }
    if (Array.isArray(input)) return input.map(value);
    if (input && typeof input === 'object') return Object.fromEntries(Object.entries(input).map(([key,item])=>[key,value(item)]));
    return input;
  }
  return value;
}

async function readBounded(file, limit) {
  let handle;
  try { handle = await fs.open(file,'r'); }
  catch(error) { if(error.code==='ENOENT')return '';throw error; }
  try {
    const bytes=Buffer.alloc(limit+1);let length=0;
    while(length<bytes.length){const read=await handle.read(bytes,length,bytes.length-length,null);if(!read.bytesRead)break;length+=read.bytesRead;}
    if(length>limit)throw new Error('Log excede o limite de exportação.');
    return new TextDecoder('utf-8',{fatal:true}).decode(bytes.subarray(0,length));
  } finally { await handle.close(); }
}

async function exportDiagnostics(diagnostics, privateRoots) {
  await diagnostics.flush();
  if(diagnostics.error)throw new Error(diagnostics.error);
  const redact=redactor(privateRoots),records=[];
  for(const name of ['nodivu.previous.log','nodivu.log']) {
    const text=await readBounded(path.join(diagnostics.directory,name),diagnostics.maxBytes);
    for(const line of text.split('\n').filter(Boolean))records.push(redact(JSON.parse(line)));
  }
  return JSON.stringify({format:'nodivu-diagnostics',version:1,
    notice:'Caminhos e identificadores conhecidos foram reduzidos. Revise as mensagens antes de compartilhar; nomes livres ainda podem conter dados pessoais.',records},null,2)+'\n';
}
module.exports={redactor,exportDiagnostics};

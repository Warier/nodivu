// Recovery stores project data only; domain validation stays in Rust before write/open.
// Existing copies are protected until the user restores or explicitly discards them.
const fs = require('node:fs/promises');
const path = require('node:path');
const {readProject, writeProject} = require('./project-files.cjs');
class ProjectRecovery {
  constructor(directory, write = writeProject) {
    this.directory=directory; this.file=path.join(directory,'recovery.nodivu.json');
    this.write=write; this.pending=false; this.available=false; this.savedAt=null; this.error='';
    this.tail=Promise.resolve();
  }
  status(){return {pending:this.pending,available:this.available,savedAt:this.savedAt,error:this.error};}
  async init(){
    try {
      const info=await fs.stat(this.file);this.available=true;this.pending=true;this.savedAt=info.mtime.toISOString();
      JSON.parse(await readProject(this.file));
    } catch(e){if(e.code!=='ENOENT'){this.pending=true;this.available=true;this.error='Não foi possível ler a cópia de recuperação: '+e.message;}}
    return this.status();
  }
  serial(fn){const task=this.tail.then(fn);this.tail=task.catch(()=>{});return task;}
  save(project){return this.serial(async()=>{
    if(this.pending)throw new Error('Restaure ou descarte a cópia anterior antes de gravar outra recuperação.');
    try {
      await fs.mkdir(this.directory,{recursive:true});await this.write(this.file,project);
      this.available=true;this.savedAt=new Date().toISOString();this.error='';return this.status();
    }catch(e){this.error='Falha na cópia automática: '+e.message;throw e;}
  });}
  read(){return this.serial(()=>readProject(this.file));}
  // Successful restore leaves its copy intact until a real Save or explicit discard.
  protect(){if(this.available)this.pending=true;return this.status();}
  accept(){this.pending=false;this.error='';return this.status();}
  clear(includePending=false){return this.serial(async()=>{
    if(this.pending&&!includePending)return this.status();
    await fs.rm(this.file,{force:true});this.pending=false;this.available=false;this.savedAt=null;this.error='';return this.status();
  });}
}
module.exports={ProjectRecovery};

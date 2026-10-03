// Called after Build-Electron.ps1. No publishing credentials or user data in staging.
const fs=require('node:fs/promises');const path=require('node:path');
const root=path.resolve(__dirname,'../..');
(async()=>{
 const stage=path.join(root,'.local/release-support');
 await fs.mkdir(stage,{recursive:true});
 await require('../package-licenses.cjs')(root,stage);
 await fs.cp(path.join(root,'.tools/vb-cable'),path.join(stage,'third-party/vb-cable'),{recursive:true});
 // Retain license texts of runtime JavaScript dependencies, including the updater.
 const ui=path.join(root,'apps/nodivu-electron');
 const dirs=require('node:child_process').execSync('npm.cmd ls --omit=dev --all --parseable',
   {cwd:ui,encoding:'utf8',windowsHide:true}).trim().split(/\r?\n/).slice(1);
 const index=[];
 for(const dir of dirs){
   const p=JSON.parse(await fs.readFile(path.join(dir,'package.json'),'utf8'));
   index.push({name:p.name,version:p.version,license:p.license});
   const dest=path.join(stage,'third-party/licenses/npm',p.name+'-'+p.version);
   await fs.mkdir(dest,{recursive:true});
   const files=(await fs.readdir(dir)).filter(f=>/^(license|copying|notice)/i.test(f));
   // lazy-val 1.0.5 declares MIT in its published metadata, but upstream ships no
   // LICENSE file. Preserve that declaration and attribution instead of inventing one.
   if(!files.length){
     if(p.name!=='lazy-val'||p.version!=='1.0.5'||p.license!=='MIT')throw new Error('Licenca ausente: '+p.name);
     await fs.copyFile(path.join(dir,'package.json'),path.join(dest,'package.json'));
     await fs.writeFile(path.join(dest,'NOTICE.txt'),'lazy-val 1.0.5; author: Vladimir Krivosheev. MIT as declared in package.json. Upstream: https://github.com/develar/lazy-val . Upstream provides no separate license text.\n');
   }
   for(const file of files)await fs.cp(path.join(dir,file),path.join(dest,file),{recursive:true});
 }
 await fs.writeFile(path.join(stage,'third-party/licenses/npm-dependencies.json'),JSON.stringify(index,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});

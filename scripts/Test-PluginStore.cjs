// Local signed catalog integration test. No production publishing and no persistent private keys.
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),os=require('node:os');
const {spawn}=require('node:child_process');
const {TestRepository}=require('../apps/nodivu-electron/tests/helpers/catalog-repo.cjs');
(async()=>{
 const root=path.resolve(__dirname,'..'),temporary=await fs.mkdtemp(path.join(os.tmpdir(),'nodivu-store-smoke-'));
 const repo=new TestRepository();repo.package('org.nodivu.official');repo.package('com.example.community');repo.publish();
 const server=http.createServer((req,res)=>repo.handler(req,res));await new Promise(r=>server.listen(0,'127.0.0.1',r));
 try{
  const config=path.join(temporary,'source.json');await fs.writeFile(config,JSON.stringify(repo.source('http://127.0.0.1:'+server.address().port)));
  const child=spawn(path.join(root,'.local/electron/nodivu-electron.exe'),['--smoke-test','--test-store'],{windowsHide:true,env:{...process.env,NODIVU_STORE_CONFIG:config},stdio:['ignore','pipe','pipe']});
  let stdout='',stderr='';child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);
  const timeout=setTimeout(()=>child.kill(),45000);
  let code;try{code=await new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',resolve);});}finally{clearTimeout(timeout);}
  await fs.writeFile(path.join(root,'.local/store-test.stdout.log'),stdout);await fs.writeFile(path.join(root,'.local/store-test.stderr.log'),stderr);
  process.stdout.write(stdout);if(code!==0||!stdout.includes('PASS: loja com catálogo TUF real')||!stdout.includes('PASS: Electron, canvas, API e encerramento.')){process.stderr.write(stderr);throw new Error('Store smoke failed: '+code);}
 }finally{server.closeAllConnections();await new Promise(r=>server.close(r));await fs.rm(temporary,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});

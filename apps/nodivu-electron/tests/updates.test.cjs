const {test}=require('node:test');const assert=require('node:assert/strict');const {EventEmitter}=require('node:events');
const {Updates}=require('../updates.cjs');
test('updates require download and restart consent, including app quit, and serialize requests',async()=>{
  const u=new EventEmitter();let installs=0,downloads=0,checks=0,approve=false,release;
  u.checkForUpdates=async()=>{checks++;u.emit('checking-for-update');await new Promise(r=>release=r);u.emit('update-available',{version:'0.1.4-beta.1'});};
  u.downloadUpdate=async()=>{downloads++;u.emit('download-progress',{percent:75});u.emit('update-downloaded',{version:'0.1.4-beta.1'});};
  u.quitAndInstall=()=>installs++;
  const c=new Updates(u,{version:'0.1.3-beta.1',prepareInstall:async()=>approve});
  assert.equal(u.autoInstallOnAppQuit,false);assert.equal(u.autoDownload,false);assert.equal(u.allowDowngrade,false);
  await c.run('install');assert.equal(installs,0);
  const pending=c.run('check');await c.run('check');assert.equal(checks,1);release();await pending;
  assert.equal(downloads,0);await c.run('download');assert.equal(c.state.phase,'downloaded');
  await c.run('check');assert.equal(checks,1);
  await c.run('install');assert.equal(installs,0);assert.equal(c.state.phase,'downloaded');
  approve=true;await c.run('install');assert.equal(installs,1);
});
test('failed download never enables installation and can be retried',async()=>{
  const u=new EventEmitter();let installs=0;
  u.checkForUpdates=async()=>u.emit('update-available',{version:'0.1.4-beta.1'});
  u.downloadUpdate=async()=>{throw new Error('checksum incorreto');};u.quitAndInstall=()=>installs++;
  const c=new Updates(u,{version:'0.1.3-beta.1'});await c.run('check');await c.run('download');await c.run('install');
  assert.equal(c.state.phase,'error');assert.match(c.state.message,/checksum/);assert.equal(installs,0);
  await c.run('check');assert.equal(c.state.phase,'available');
});
test('development and demo do not initiate network updates',async()=>{
  const c=new Updates(null,{version:'test'});assert.equal((await c.run('check')).phase,'disabled');
});

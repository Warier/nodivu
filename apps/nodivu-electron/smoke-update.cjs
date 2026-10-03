// Installed-app integration test. The feed override is gated by two smoke flags
// and restricted to localhost in main; production renderer cannot choose a URL.
const assert=require('node:assert/strict');
module.exports=async(win,updates)=>{
  const wait=async test=>{const end=Date.now()+90000;while(Date.now()<end){if(test())return;await new Promise(r=>setTimeout(r,100));}throw new Error('Timeout update smoke: '+JSON.stringify(updates.snapshot()));};
  await win.webContents.executeJavaScript("document.getElementById('update-action').click()");
  await wait(()=>updates.state.phase==='available'||updates.state.phase==='error');
  assert.equal(updates.state.phase,'available',updates.state.message);
  await win.webContents.executeJavaScript("document.getElementById('update-action').click()");
  await wait(()=>updates.state.phase==='downloaded'||updates.state.phase==='error');
  assert.equal(updates.state.phase,'downloaded',updates.state.message);
  await win.webContents.executeJavaScript('window.nodivu.setProjectDirty(true)');
  await new Promise(r=>setTimeout(r,100));
  await updates.run('install');assert.equal(updates.state.phase,'downloaded');
  console.log('PASS: feed, download/hash, UI and unsaved-project gate. Installing approved update.');
  await win.webContents.executeJavaScript('window.nodivu.setProjectDirty(false)');
  await new Promise(r=>setTimeout(r,100));
  await updates.run('install');
};

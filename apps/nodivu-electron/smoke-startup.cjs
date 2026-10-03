// Explicitly simulated handshake/discovery failures; real renderer and backend.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
module.exports = async (win, diagnostics) => {
  const run = code => win.webContents.executeJavaScript(code);
  async function until(code) {
    const end = Date.now() + 9000;
    while (Date.now() < end) { if (await run(code)) return; await new Promise(r => setTimeout(r, 40)); }
    throw new Error('Startup regression: ' + code + ' / ' + await run('document.getElementById("error").textContent'));
  }
  const idle = () => until('document.body.dataset.busy==="false"');
  async function click(selector) { await idle(); await run(`document.querySelector(${JSON.stringify(selector)}).click()`); await idle(); }
  await until('document.getElementById("error").textContent.includes("handshake")');
  assert.equal(await run('[...document.querySelectorAll("#palette button")].every(b=>b.disabled)'), true);
  await click('#palette [data-kind=gain]');
  assert.match(await run('document.getElementById("error").textContent'), /handshake/);
  assert.equal(await run('document.querySelectorAll(".node").length'), 0);
  await click('#audio-action'); // Recover the handshake, then fail MMDevice discovery.
  await until('document.body.dataset.ready==="true"');
  assert.match(await run('document.getElementById("audio-detail").textContent'), /0xE000020B/);
  assert.equal(await run('document.querySelector("#palette [data-kind=gain]").disabled'), false);
  await click('#palette [data-kind=gain]');
  assert.equal(await run('document.querySelectorAll(".node").length'), 1);
  assert.match(await run('document.getElementById("error").textContent'), /0xE000020B/);
  const revision = await run('document.body.dataset.revision');
  await click('#audio-action'); // Repeated failure remains actionable, without resetting graph.
  assert.equal(await run('document.body.dataset.revision'), revision);
  assert.match(await run('document.getElementById("audio-detail").textContent'), /0xE000020B/);
  await click('#audio-action'); // Real discovery now succeeds.
  assert.equal(await run('document.body.dataset.revision'), revision);
  assert.equal(await run('document.querySelectorAll(".node").length'), 1);
  assert.equal(await run('document.getElementById("error").hidden'), true);
  await run("window.nodivu.reportFault('Falha simulada no renderer')");
  await new Promise(r=>setTimeout(r,80));
  await diagnostics.flush();
  const logs = await fs.readFile(diagnostics.current, 'utf8');
  assert.match(logs, /0xE000020B/); assert.match(logs, /devices.inventory/); assert.match(logs, /renderer.error/);
  assert.equal(await run('document.getElementById("diagnostics-open")!==null'), true);
  // Clear only this disposable smoke session before the existing normal suite.
  await click('#reconnect'); await until('document.body.dataset.ready==="true"');
  assert.equal(await run('document.querySelectorAll(".node").length'), 0);
  console.log('PASS: handshake failure, no null revision, editable graph with discovery failure, retry preserves graph and diagnostics record cause.');
};

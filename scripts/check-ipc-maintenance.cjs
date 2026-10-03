// Processo real de teste, sem hardware: stdin parcial não deve monopolizar o owner.
const { spawn } = require('node:child_process');
const path = require('node:path');
const assert = require('node:assert/strict');
const executable = path.resolve(__dirname, '../target/release/examples/maintenance_probe.exe');
const child = spawn(executable, [], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
let stdout = '', stderr = '', completed = false;
const timeout = setTimeout(() => { child.kill(); console.error('FAIL: manutenção bloqueada por entrada parcial.'); process.exitCode = 1; }, 5000);
child.on('error', error => { clearTimeout(timeout); console.error(error.message); process.exitCode = 1; });
child.stdin.on('error', error => { console.error(error.message); process.exitCode = 1; });
child.stdout.on('data', data => { stdout += data; });
child.stderr.on('data', data => {
  stderr += data;
  if (!completed && stderr.includes('MAINTENANCE_READY')) {
    completed = true;
    child.stdin.end('}\n');
  }
});
child.on('close', code => {
  clearTimeout(timeout);
  try {
    assert.equal(code, 0);
    assert.equal(completed, true);
    const response = JSON.parse(stdout);
    assert.ok(response.result.ticks >= 3);
    assert.equal(response.result.frame_bytes, 3);
    console.log('PASS: manutenção progrediu com stdin parcial; resposta e encerramento preservados.');
  } catch (error) { console.error(error.message); process.exitCode = 1; }
});
child.stdin.write('{');

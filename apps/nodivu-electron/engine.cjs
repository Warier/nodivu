const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');
const MAX_LINE = 256 * 1024;

// Um único pedido pendente. Pipes não transportam PCM e não enfileiram movimentos.
class Engine extends EventEmitter {
  constructor(executable, env = process.env) {
    super();
    this.serial = 0; this.buffer = Buffer.alloc(0); this.pending = null;
    this.closed = false; this.closing = false;
    this.child = spawn(executable, [], { windowsHide: true, env, stdio: ['pipe', 'pipe', 'pipe'] });
    this.stderrTail = '';
    this.ready = new Promise((resolve, reject) => {
      this.child.once('spawn', resolve); this.child.once('error', reject);
    });
    this.ready.catch(() => {});
    this.exited = new Promise(resolve => this.child.once('close', resolve));
    this.child.on('error', error => this.fail(error));
    this.child.stdin.on('error', error => this.fail(error));
    this.child.stderr.on('data', data => { this.stderrTail = (this.stderrTail + data.toString('utf8')).slice(-4096); });
    this.child.stdout.on('data', data => this.receive(data));
    this.child.on('close', () => {
      this.closed = true;
      if (this.pending || !this.closing) this.fail(new Error('Motor desconectado. '+this.stderrTail.trim()+' Reconecte para uma sessão vazia.'));
    });
  }
  fail(error) {
    if (this.pending) {
      clearTimeout(this.pending.timer); this.pending.reject(error); this.pending = null;
    }
    if (!this.closed) this.child.kill();
    if (!this.closing) this.emit('fault', error.message);
  }
  receive(data) {
    if (data.length + this.buffer.length > MAX_LINE) return this.fail(new Error('Resposta excedeu 256 KiB.'));
    this.buffer = Buffer.concat([this.buffer, data]);
    const newline = this.buffer.indexOf(10);
    if (newline < 0) return;
    try {
      const response = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(this.buffer.subarray(0, newline)));
      this.buffer = this.buffer.subarray(newline + 1);
      const pending = this.pending;
      if (this.buffer.length || !pending || response.protocol_version !== 1 || response.id !== pending.id ||
          typeof response.ok !== 'boolean' || Object.hasOwn(response, 'result') === Object.hasOwn(response, 'error')) {
        throw new Error('Resposta inválida ou fora de ordem.');
      }
      clearTimeout(pending.timer); this.pending = null;
      if (response.ok) pending.resolve(response.result);
      else { const error = new Error(response.error.message); error.code = response.error.code; pending.reject(error); }
    } catch (error) { this.fail(error); }
  }
  async request(command, params = {}) {
    await this.ready;
    if (this.closed || (this.closing && command !== 'system.shutdown')) throw new Error('Motor encerrado.');
    if (this.pending) throw new Error('Aguarde a operação atual.');
    const id = String(++this.serial);
    const bytes = JSON.stringify({ protocol_version: 1, id, command, params }) + '\n';
    if (Buffer.byteLength(bytes) > MAX_LINE) throw new Error('Comando excedeu o limite.');
    const done = new Promise((resolve, reject) => {
      const timer = setTimeout(() => this.fail(new Error('Motor sem resposta por 5 segundos.')), 5000);
      this.pending = { id, resolve, reject, timer };
      this.child.stdin.write(bytes);
    });
    if (this.pending) this.pending.done = done;
    return done;
  }
  async stop() {
    if (this.closing) return this.exited;
    this.closing = true;
    const deadline = setTimeout(() => this.child.kill(), 3000);
    try {
      if (this.pending) await this.pending.done.catch(() => {});
      if (!this.closed) await this.request('system.shutdown');
    } catch { if (!this.closed) this.child.kill(); }
    finally { this.child.stdin.end(); await this.exited; clearTimeout(deadline); }
  }
}
module.exports = { Engine };

const fs = require('node:fs/promises');
const path = require('node:path');

// Control-thread diagnostics only: no PCM, project documents or command params.
// Queue, record size and disk usage are bounded, including a repeating failure.
class Diagnostics {
  constructor(directory, { maxBytes = 1024 * 1024, maxPending = 32 } = {}) {
    this.directory = directory;
    this.current = path.join(directory, 'nodivu.log');
    this.previous = path.join(directory, 'nodivu.previous.log');
    this.maxBytes = maxBytes; this.maxPending = maxPending;
    this.pending = 0; this.dropped = 0; this.error = null; this.recent = new Map();
    this.tail = fs.mkdir(directory, { recursive: true }).catch(error => { this.error = error.message; });
  }
  record(event, details = {}) {
    const fields = Object.fromEntries(Object.entries(details).slice(0, 16).map(([key, value]) =>
      [key.slice(0, 64), typeof value === 'string' ? value.slice(0, 2048) :
        typeof value === 'number' || typeof value === 'boolean' ? value : String(value).slice(0, 2048)]));
    const key = JSON.stringify([event, fields]);
    const now = Date.now();
    if (now - (this.recent.get(key) || 0) < 10000) return;
    if (this.pending >= this.maxPending) { this.dropped++; return; }
    if (this.recent.size >= 32) this.recent.delete(this.recent.keys().next().value);
    this.recent.set(key, now);
    // Keep valid JSON even when a single record is unexpectedly large.
    let row = { time: new Date(now).toISOString(), event: String(event).slice(0, 100), ...fields, dropped: this.dropped };
    let line = JSON.stringify(row) + '\n';
    if (Buffer.byteLength(line) > Math.min(this.maxBytes, 16384)) {
      row = { time: row.time, event: row.event, message: 'Detalhes excederam o limite do registro.', dropped: this.dropped };
      line = JSON.stringify(row) + '\n';
    }
    this.dropped = 0; this.pending++;
    this.tail = this.tail.then(async () => {
      await fs.mkdir(this.directory, { recursive: true });
      let size = 0;
      try { size = (await fs.stat(this.current)).size; } catch (error) { if (error.code !== 'ENOENT') throw error; }
      if (size + Buffer.byteLength(line) > this.maxBytes) {
        await fs.rm(this.previous, { force: true });
        if (size) await fs.rename(this.current, this.previous);
      }
      await fs.appendFile(this.current, line, 'utf8');
      this.error = null;
    }).catch(error => { this.error = error.message; }).finally(() => { this.pending--; });
  }
  async flush() { await this.tail; }
}
module.exports = { Diagnostics };

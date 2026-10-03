// Control plane only. No update operation touches the audio callback.
class Updates {
  constructor(updater, {version, publish = () => {}, log = () => {}, prepareInstall = async () => true}) {
    this.updater = updater; this.publish = publish; this.log = log; this.prepareInstall = prepareInstall;
    this.state = {phase: updater ? 'idle' : 'disabled', current: version, version: null, percent: 0, message: updater ? 'Canal de testes' : 'Atualizações disponíveis na versão instalada.'};
    this.operation = null;
    if (!updater) return;
    updater.autoDownload = false;
    updater.autoInstallOnAppQuit = false; // Even closing the app must not install without consent.
    updater.allowDowngrade = false;
    updater.allowPrerelease = true;
    updater.on('checking-for-update', () => this.set({phase:'checking', message:'Procurando atualização…'}));
    updater.on('update-available', info => this.set({phase:'available', version:info.version, message:'Nova versão disponível.'}));
    updater.on('update-not-available', () => this.set({phase:'idle', message:'Você está na versão mais recente deste canal.'}));
    updater.on('download-progress', p => this.set({phase:'downloading', percent:Math.max(0,Math.min(100,Number(p.percent)||0)), message:'Baixando atualização…'}));
    updater.on('update-downloaded', info => this.set({phase:'downloaded', version:info.version, percent:100, message:'Pronta. Reinicie quando puder interromper o áudio.'}));
    updater.on('error', error => this.fail(error));
  }
  snapshot() { return {...this.state}; }
  set(change) { Object.assign(this.state, change); this.publish(this.snapshot()); }
  fail(error) {
    const message = String(error.message || error).slice(0,1000);
    this.log('update.error', {message});
    this.set({phase:'error',message:'Não foi possível atualizar. Sua versão continua instalada. '+message});
  }
  async run(action) {
    if (!this.updater || this.operation) return this.snapshot();
    if (!['check','download','install'].includes(action)) throw new Error('Ação de atualização inválida.');
    if (action === 'check' && ['downloaded','installing'].includes(this.state.phase)) return this.snapshot();
    if (action === 'download' && this.state.phase !== 'available') return this.snapshot();
    if (action === 'install' && this.state.phase !== 'downloaded') return this.snapshot();
    this.operation = action;
    try {
      if (action === 'check') await this.updater.checkForUpdates();
      else if (action === 'download') {
        this.set({phase:'downloading',percent:0,message:'Baixando atualização…'});
        await this.updater.downloadUpdate(); // Library verifies the SHA-512 from the feed.
      } else if (await this.prepareInstall()) {
        this.set({phase:'installing',message:'Encerrando para atualizar…'});
        this.log('update.install', {version:this.state.version});
        this.updater.quitAndInstall(false, true);
      }
    } catch (error) { this.fail(error); }
    finally { this.operation = null; }
    return this.snapshot();
  }
}
module.exports = {Updates};

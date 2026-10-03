import capture from './capture.js';
import output from './output.js';
import gain from './gain.js';
import tone from './tone.js';
import mixer from './mixer.js';
import meter from './meter.js';
// Somente tipos internos conhecidos; não carrega código de plugins externos.
export const blocks=Object.freeze(Object.fromEntries([capture,output,gain,tone,mixer,meter].map(b=>[b.kind,Object.freeze(b)])));

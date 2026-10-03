const path = require('node:path');
const { Engine } = require('../engine.cjs');
const engine = new Engine(path.resolve(__dirname, '../../../target/release/nodivu-app-backend.exe'));
engine.request('system.hello').then(() => console.log(engine.child.pid)).catch(error => { console.error(error); process.exit(1); });
setInterval(() => {}, 1000);

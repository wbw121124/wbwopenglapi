const { Worker } = require('worker_threads');
const { createCanvas } = require('../lib/index.js');

const workerCode = `
const { parentPort } = require('worker_threads');
const { createCanvas } = require('./lib/index.js');
const c = createCanvas(32, 32);
c.clear([1, 2, 3, 255]);
c.fillStyle('red');
c.fillRect(2, 2, 8, 8);
c.resolve();
const px = c.readPixels(0, 0, 32, 32);
parentPort.postMessage({ r: px[0], g: px[1], b: px[2] });
c.close();
`;

const mainC = createCanvas(64, 64);
mainC.clear([255, 0, 0, 255]);
mainC.resolve();
const w = new Worker(workerCode, { eval: true });
const timeout = setTimeout(() => { console.error('TIMEOUT'); process.exit(1); }, 15000);
w.on('message', (m) => {
  clearTimeout(timeout);
  const okW = m.r === 1 && m.g === 2 && m.b === 3;
  console.log('worker pixels:', JSON.stringify(m), okW ? 'OK' : 'BAD');
  const px = mainC.readPixels(0, 0, 64, 64);
  const okM = px[0] === 255 && px[1] === 0 && px[2] === 0;
  console.log('main still works:', px[0], px[1], px[2], okM ? 'OK' : 'BAD');
  mainC.close();
  w.terminate();
  process.exit(okW && okM ? 0 : 1);
});
w.on('error', (e) => { console.error('worker error:', e); process.exit(1); });
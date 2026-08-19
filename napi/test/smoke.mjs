import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCanvas } from '../lib/index.js';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

const c = createCanvas(64, 64);
c.clear([255, 128, 0, 255]);
c.fillStyle({ r: 0, g: 0, b: 255, a: 1 });
c.fillRect(10, 10, 20, 20);
c.resolve();
const px = c.readPixels(0, 0, c.width, c.height);
if (px.length !== c.width * c.height * 4) throw new Error('readPixels 尺寸错误');
if (px[(15 * c.width + 15) * 4 + 2] !== 255) throw new Error('填充矩形像素错误');
const bmp = c.toBMP();
if (bmp[0] !== 0x42 || bmp[1] !== 0x4d) throw new Error('BMP 头错误');
c.close();
console.log('smoke OK: createCanvas/fillRect/resolve/readPixels/toBMP (' + root + ')');

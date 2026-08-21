import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCanvas, loadPNG } from '../lib/index.js';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

const c = createCanvas(64, 64);
c.clear([255, 128, 0, 255]);
c.fillStyle({ r: 0, g: 0, b: 255, a: 1 });
c.fillRect(10, 10, 20, 20);
// Canvas2D 补全 API 冒烟：渐变 + roundRect + clip + 合成
const g = c.createLinearGradient(0, 0, 64, 0);
g.addColorStop(0, 'red');
g.addColorStop(1, 'blue');
c.fillStyle(g);
c.beginPath();
c.roundRect(36, 8, 20, 20, [4]);
c.fill();
c.save();
c.beginPath();
c.rect(0, 32, 32, 32);
c.clip();
c.globalCompositeOperation('lighter');
c.fillStyle([255, 255, 0, 128]);
c.fillRect(0, 32, 64, 64); // 裁剪区内 lighter 叠加
c.restore();
c.globalCompositeOperation('source-over');
c.resolve();
const px = c.readPixels(0, 0, c.width, c.height);
if (px.length !== c.width * c.height * 4) throw new Error('readPixels 尺寸错误');
if (px[(15 * c.width + 15) * 4 + 2] !== 255) throw new Error('填充矩形像素错误');
if (px[(16 * c.width + 44) * 4 + 2] < px[(16 * c.width + 44) * 4] + 60) {
  throw new Error('渐变矩形像素错误');
}
const bmp = c.toBMP();
if (bmp[0] !== 0x42 || bmp[1] !== 0x4d) throw new Error('BMP 头错误');
if (typeof loadPNG === 'function') {
  const png = c.toPNG();
  if (png[0] !== 0x89 || png[1] !== 0x50) throw new Error('PNG 头错误');
  const img = loadPNG(png);
  if (img.width !== c.width || img.height !== c.height) throw new Error('loadPNG 尺寸错误');
}
c.close();
console.log('smoke OK: createCanvas/fillRect/gradient/roundRect/clip/composite/resolve/readPixels/toBMP/toPNG (' + root + ')');

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createCanvas, loadBMP } from '../lib/index.js';

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..');

test('createCanvas 尺寸与像素格式', () => {
  const c = createCanvas(64, 64);
  assert.ok(c.width >= 64 && c.height >= 64);
  c.clear([255, 128, 0, 255]);
  c.resolve();
  const px = c.readPixels(0, 0, c.width, c.height);
  assert.equal(px.length, c.width * c.height * 4);
  assert.deepEqual([...px.subarray(0, 4)], [255, 128, 0, 255]);
  c.close();
});

test('fillRect 颜色对象/数组/CSS 与 y 翻转', () => {
  const c = createCanvas(64, 64);
  c.clear([255, 255, 255, 255]);
  c.fillStyle({ r: 0, g: 0, b: 255, a: 1 });
  c.fillRect(10, 10, 20, 20);
  c.fillStyle([0, 255, 0, 1]);
  c.fillRect(40, 10, 10, 10);
  c.strokeStyle('rgb(255,0,0)');
  c.lineWidth(2);
  c.fill();
  c.resolve();
  const px = c.readPixels(0, 0, c.width, c.height);
  const at = (x, y) => px.subarray((y * c.width + x) * 4, (y * c.width + x) * 4 + 4);
  assert.deepEqual([...at(15, 15)], [0, 0, 255, 255]);
  assert.deepEqual([...at(41, 15)], [0, 255, 0, 255]);
  c.close();
});

test('路径绘制', () => {
  const c = createCanvas(32, 32);
  c.clear([255, 255, 255, 255]);
  c.strokeStyle('black');
  c.lineWidth(2);
  c.beginPath();
  c.moveTo(4, 4);
  c.lineTo(16, 16);
  c.stroke();
  c.resolve();
  const px = c.readPixels(0, 0, 32, 32);
  assert.equal(px[(8 * 32 + 8) * 4], 0);
  c.close();
});

test('文本 measureText/fillText/fontFeatures', () => {
  const c = createCanvas(200, 40);
  c.clear([255, 255, 255, 255]);
  c.font('20px sans-serif');
  c.fillStyle('black');
  c.textAlign('left');
  c.textBaseline('top');
  c.fontFeatures('on');
  const w = c.measureText('Hello wbw');
  assert.ok(w > 0);
  c.fillText('Hello wbw', 4, 4);
  c.resolve();
  const px = c.readPixels(0, 0, 200, 40);
  let dark = 0;
  for (let i = 0; i < px.length; i += 4) if (px[i] < 128) dark++;
  assert.ok(dark > 30, '文本像素数量 ' + dark);
  c.resetFontFeatures();
  c.close();
});

test('图像 loadBMP/drawImage', () => {
  const img = loadBMP(path.join(root, 'test', '12_antialias_off.bmp'));
  assert.ok(img.width > 0 && img.height > 0);
  const c = createCanvas(64, 64);
  c.clear([255, 255, 255, 255]);
  c.drawImage(img, 0, 0, 64, 64);
  c.resolve();
  const px = c.readPixels(0, 0, 64, 64);
  assert.notEqual(px[(12 * 64 + 12) * 4], 255);
  c.close();
});

test('变换与状态保存恢复', () => {
  const c = createCanvas(32, 32);
  c.clear([255, 255, 255, 255]);
  c.save();
  c.translate(16, 16);
  c.rotate(Math.PI);
  c.fillStyle('rgb(0,128,0)');
  c.fillRect(-8, -8, 16, 16);
  c.restore();
  c.resetTransform();
  c.resolve();
  const px = c.readPixels(0, 0, 32, 32);
  const at = (x, y) => px.subarray((y * 32 + x) * 4, (y * 32 + x) * 4 + 4);
  assert.deepEqual([...at(16, 16)], [0, 128, 0, 255]);
  c.close();
});

test('toBMP 输出有效 BMP 且方向正确', () => {
  const c = createCanvas(16, 16);
  c.clear([10, 20, 30, 255]);       // 背景
  c.fillStyle('#e74c3c');
  c.fillRect(0, 0, 16, 8);          // 画布顶部 8 行红色（BMP 自下而上存底部优先）
  c.resolve();
  const bmp = c.toBMP();
  assert.ok(bmp.length > 54 + 16 * 16 * 3);
  assert.equal(bmp[0], 0x42);
  assert.equal(bmp[1], 0x4d);
  // 行序校验（按 BMP 头实际尺寸解析，HiDPI 下 framebuffer 可能大于画布逻辑尺寸）:
  // 文件首行 = 图像底部（背景），文件末行 = 图像顶部（红色）
  const w = bmp.readInt32LE(18);
  const h = bmp.readInt32LE(22);
  const stride = Math.floor((w * 3 + 3) / 4) * 4;
  const lastRow = bmp.subarray(54 + (h - 1) * stride, 54 + h * stride);
  const firstRow = bmp.subarray(54, 54 + stride);
  assert.deepEqual([lastRow[2], lastRow[1], lastRow[0]], [231, 76, 60]);
  assert.deepEqual([firstRow[2], firstRow[1], firstRow[0]], [10, 20, 30]);
  c.close();
});

test('关闭后访问抛错', () => {
  const c = createCanvas(16, 16);
  c.close();
  assert.throws(() => c.clear([1, 1, 1, 1]));
});

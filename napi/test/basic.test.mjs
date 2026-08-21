import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createCanvas, loadBMP, loadPNG, savePNG } from '../lib/index.js';

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

test('ellipse 与 roundRect（含半径数组）', () => {
  const c = createCanvas(64, 64);
  c.clear([255, 255, 255, 255]);
  c.fillStyle('red');
  c.beginPath();
  c.ellipse(16, 16, 12, 8, 0, 0, Math.PI * 2);
  c.fill();
  c.fillStyle('blue');
  c.beginPath();
  c.roundRect(36, 36, 20, 20, [6]); // 数组形式（Canvas 规范 1..4 元素）
  c.fill();
  c.resolve();
  const px = c.readPixels(0, 0, 64, 64);
  const at = (x, y) => [...px.subarray((y * 64 + x) * 4, (y * 64 + x) * 4 + 4)];
  assert.deepEqual(at(16, 16), [255, 0, 0, 255]);     // 椭圆中心
  assert.deepEqual(at(16, 7), [255, 0, 0, 255]);      // 椭圆短轴内 (ry=8)
  assert.deepEqual(at(40, 40), [0, 0, 255, 255]);     // 圆角矩形内部
  assert.deepEqual(at(37, 37), [255, 255, 255, 255]); // 圆角切掉的直角处
  c.close();
});

test('线性/径向渐变', () => {
  const c = createCanvas(64, 64);
  c.clear([255, 255, 255, 255]);
  const lg = c.createLinearGradient(0, 0, 64, 0);
  lg.addColorStop(0, 'red');
  lg.addColorStop(1, 'blue');
  c.fillStyle(lg);
  c.fillRect(0, 0, 64, 32);
  const rg = c.createRadialGradient(32, 48, 0, 32, 48, 14);
  rg.addColorStop(0, 'green');
  rg.addColorStop(1, 'white');
  c.fillStyle(rg);
  c.fillRect(0, 32, 64, 32);
  c.resolve();
  const px = c.readPixels(0, 0, 64, 64);
  const at = (x, y) => [...px.subarray((y * 64 + x) * 4, (y * 64 + x) * 4 + 4)];
  const l0 = at(2, 8), l1 = at(61, 8), r0 = at(32, 48);
  assert.ok(l0[0] > 200 && l0[2] < 80, '线性起点红 ' + l0);
  assert.ok(l1[2] > 200 && l1[0] < 80, '线性终点蓝 ' + l1);
  assert.ok(r0[1] > r0[0] + 60 && r0[1] > r0[2] + 60, '径向中心绿 ' + r0);
  c.close();
});

test('clip 裁剪与 restore 恢复', () => {
  const c = createCanvas(64, 64);
  c.clear([255, 255, 255, 255]);
  c.save();
  c.beginPath();
  c.rect(8, 8, 24, 24);
  c.clip();
  c.fillStyle('red');
  c.fillRect(0, 0, 64, 64); // 全屏填充但被裁剪
  c.restore();
  c.fillStyle('lime');
  c.fillRect(40, 40, 10, 10); // restore 后不受裁剪影响
  c.resolve();
  const px = c.readPixels(0, 0, 64, 64);
  const at = (x, y) => [...px.subarray((y * 64 + x) * 4, (y * 64 + x) * 4 + 4)];
  assert.deepEqual(at(10, 10), [255, 0, 0, 255]);     // 裁剪区内
  assert.deepEqual(at(40, 10), [255, 255, 255, 255]); // 裁剪区外
  assert.deepEqual(at(44, 44), [0, 255, 0, 255]);     // restore 后正常绘制
  c.close();
});

test('globalCompositeOperation source-in', () => {
  const c = createCanvas(32, 32);
  c.clearRect(0, 0, 32, 32);
  c.fillStyle('red');
  c.fillRect(0, 0, 16, 32); // 左半红
  c.globalCompositeOperation('source-in');
  c.fillStyle([0, 0, 255, 255]);
  c.fillRect(8, 0, 16, 32); // 仅在已有 alpha 内着色 -> 左半变蓝
  c.resolve();
  const px = c.readPixels(0, 0, 32, 32);
  const at = (x, y) => [...px.subarray((y * 32 + x) * 4, (y * 32 + x) * 4 + 4)];
  assert.deepEqual(at(4, 16), [0, 0, 255, 255]); // dst 内：src 替换
  assert.deepEqual(at(28, 16), [0, 0, 0, 0]);    // dst 外：透明
  c.globalCompositeOperation('source-over');     // 未知外的合法值恢复
  c.close();
});

test('PNG 编解码往返（toPNG/loadPNG/savePNG）', (t) => {
  if (typeof loadPNG !== 'function' || typeof savePNG !== 'function') {
    t.skip('构建未启用 WBWOPENGAL_API_PNG（无系统 zlib）');
    return;
  }
  const c = createCanvas(48, 32);
  c.clear([255, 255, 255, 255]);
  c.fillStyle('#204060');
  c.fillRect(4, 4, 20, 20);
  c.resolve();
  const png = c.toPNG(); // 整帧编码
  assert.deepEqual([...png.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const img = loadPNG(png); // Buffer 解码
  assert.equal(img.width, c.width);
  assert.equal(img.height, c.height);
  const frame = c.readPixels(0, 0, c.width, c.height);
  const off = (10 * c.width + 10) * 4;
  assert.deepEqual([...img.rgba.subarray(off, off + 4)], [...frame.subarray(off, off + 4)]);
  assert.equal(typeof img.toPNG, 'function');
  assert.equal(img.toPNG()[0], 0x89);
  const file = path.join(root, 'napi', 'test', '_png_roundtrip.png');
  savePNG(img, file);
  const img2 = loadPNG(file);
  assert.equal(img2.width, img.width);
  assert.deepEqual([...img2.rgba.subarray(off, off + 4)], [...img.rgba.subarray(off, off + 4)]);
  fs.rmSync(file, { force: true });
  c.close();
});

import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { createCanvas, loadBMP } = require('./build/wbwopenglapi.node');

function assert(cond, msg) { if (!cond) throw new Error('ASSERT FAIL: ' + msg); }

// 1) 基础：clear + fillRect + resolve + readPixels
const c = createCanvas(64, 64);
assert(c.width >= 64 && c.height >= 64, 'size ' + c.width + 'x' + c.height);
c.clear([255, 128, 0, 255]);
c.fillStyle({ r: 0, g: 0, b: 255, a: 1 });   // 对象样式
c.fillRect(10, 10, 20, 20);                    // 像素化坐标：实际^1.875, y^1
c.fillStyle([0, 255, 0, 1]);                  // 数组样式
c.fillRect(40, 10, 10, 10);
c.strokeStyle("red");
c.lineWidth(2);
c.fill();
c.resolve();
const px = c.readPixels(0, 0, c.width, c.height);
assert(px.length === c.width * c.height * 4, 'px size ' + px.length);
const topRow = px.subarray(0, c.width * 4);
assert(topRow[0] === 255 && topRow[2] === 0, 'bg top-left');
const at = (x, y) => px.subarray((y * c.width + x) * 4, (y * c.width + x) * 4 + 4);
assert(at(15, 15)[2] === 255 && at(15, 15)[0] === 0, 'blue rect @15,15');
assert(at(15, 15)[3] === 255, 'blue rect alpha');
assert(at(41, 15)[1] === 255 && at(41, 15)[0] === 0, 'green rect @41,15');
c.close();

// 2) 路径：beginPath/moveTo/lineTo/stroke
const p = createCanvas(32, 32);
p.clear([255, 255, 255, 1]);
p.strokeStyle({ r: 0, g: 0, b: 0, a: 1 });
p.lineWidth(2);
p.beginPath();
p.moveTo(4, 4);
p.lineTo(16, 16);
p.stroke();
p.resolve();
const pp = p.readPixels(0, 0, 32, 32);
const idx = (x, y) => (y * 32 + x) * 4;
assert(pp[idx(8, 8)] === 0, 'path pixel dark ' + pp[idx(8, 8)]);
p.close();

// 3) 文本
const t = createCanvas(200, 40);
t.clear([255, 255, 255, 1]);
t.font("20px sans-serif");
t.fillStyle("black");
t.textAlign("left");
t.textBaseline("top");
t.fontFeatures('on');     // 开启 kern/liga
const m = t.measureText('Hello wbw');
assert(m > 0, 'measureText ' + m);
t.fillText('Hello wbw', 4, 4);
t.resolve();
const tp = t.readPixels(0, 0, 200, 40);
let dark = 0;
for (let i = 0; i < tp.length; i += 4) if (tp[i] < 128) dark++;
assert(dark > 30, 'text pixels ' + dark);
t.resetFontFeatures();
t.close();

// 4) 图像
const img = loadBMP('../test/12_antialias_off.bmp');
assert(img.width > 0 && img.height > 0, 'img ' + img.width + 'x' + img.height);
const d = createCanvas(64, 64);
d.clear([255, 255, 255, 1]);
d.drawImage(img, 0, 0, 64, 64);
d.resolve();
const dp = d.readPixels(0, 0, 64, 64);
const di = (x, y) => (y * 64 + x) * 4;
assert(dp[di(12, 12)] !== 255, 'image drawn ' + dp[di(12, 12)]);
d.close();

// 5) 变换 + save/restore
const tr = createCanvas(32, 32);
tr.clear([255, 255, 255, 1]);
tr.save();
tr.translate(16, 16);
tr.rotate(3.14159265);
tr.fillStyle("rgb(0,128,0)");
tr.fillRect(-8, -8, 16, 16);
tr.restore();
tr.resetTransform();
tr.resolve();
const trp = tr.readPixels(0, 0, 32, 32);
assert(trp[(16 * 32 + 16) * 4] === 0 && trp[(16 * 32 + 16) * 4 + 1] === 128, 'rotate+translate fill');
tr.close();

// 6) toBMP
const b = createCanvas(16, 16);
b.clear([10, 20, 30, 255]);
b.resolve();
const bmp = b.toBMP();
assert(bmp.length > 54 + 16 * 16 * 3, 'bmp size ' + bmp.length);
assert(bmp[0] === 0x42 && bmp[1] === 0x4D, 'BMP magic');
b.close();

console.log('SMOKE2 OK: createCanvas/fill/stroke/text/image/transform/toBMP 全部通过');





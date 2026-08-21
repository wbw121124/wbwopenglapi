'use strict';

const path = require('node:path');
const native = require(path.join(__dirname, '..', 'build', 'wbwopenglapi.node'));

const methodNames = [
  'clear', 'fillStyle', 'strokeStyle', 'lineWidth', 'globalAlpha',
  'lineAlgorithm', 'antialias', 'font', 'textAlign', 'textBaseline',
  'fontFeatures', 'resetFontFeatures',
  'globalCompositeOperation', 'createLinearGradient', 'createRadialGradient',
  'fillRect', 'strokeRect', 'clearRect',
  'beginPath', 'moveTo', 'lineTo', 'quadraticCurveTo', 'bezierCurveTo',
  'arc', 'ellipse', 'roundRect', 'rect', 'closePath', 'clip', 'fill', 'stroke',
  'fillText', 'strokeText', 'measureText',
  'translate', 'rotate', 'save', 'restore', 'resetTransform',
  'drawImage', 'resolve', 'readPixels', 'toBMP', 'toPNG', 'swapBuffers', 'close',
];

function wrapCanvasHandle(handle) {
  const wrapper = {};
  Object.defineProperty(wrapper, '_handle', { value: handle });
  wrapper.width = handle.width;
  wrapper.height = handle.height;

  for (const name of methodNames) {
    if (typeof handle[name] !== 'function') continue;
    Object.defineProperty(wrapper, name, {
      configurable: true,
      enumerable: true,
      writable: true,
      value: handle[name].bind(handle),
    });
  }
  return wrapper;
}

function createCanvas(width, height) {
  return wrapCanvasHandle(native.createCanvas(width, height));
}

function loadBMP(filePath) {
  return native.loadBMP(filePath);
}

const api = { createCanvas, loadBMP };
// PNG 导出仅在构建期探测到系统 zlib 时存在（WBWOPENGAL_API_PNG）
for (const name of ['loadPNG', 'savePNG']) {
  if (typeof native[name] === 'function') api[name] = native[name];
}

module.exports = api;

'use strict';

const path = require('node:path');
const native = require(path.join(__dirname, '..', 'build', 'wbwopenglapi.node'));

const methodNames = [
  'clear', 'fillStyle', 'strokeStyle', 'lineWidth', 'globalAlpha',
  'lineAlgorithm', 'antialias', 'font', 'textAlign', 'textBaseline',
  'fontFeatures', 'resetFontFeatures',
  'fillRect', 'strokeRect', 'clearRect',
  'beginPath', 'moveTo', 'lineTo', 'quadraticCurveTo', 'bezierCurveTo',
  'arc', 'rect', 'closePath', 'fill', 'stroke',
  'fillText', 'strokeText', 'measureText',
  'translate', 'rotate', 'save', 'restore', 'resetTransform',
  'drawImage', 'resolve', 'readPixels', 'toBMP', 'swapBuffers', 'close',
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

module.exports = { createCanvas, loadBMP };

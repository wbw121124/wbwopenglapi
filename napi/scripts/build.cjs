// scripts/build.cjs - wbwopenglapi Node 插件构建入口
//   背景：Node 24 的 child_process.spawn 不再隐式解析 .cmd 后缀，
//   cmake-js 以裸 'cmake' spawn 时在 Windows 上 ENOENT。
//   此处解析 cmake-runtime 提供的真实 cmake 可执行文件绝对路径，
//   经 cmake-js 的 -c 参数显式传入，保证构建可复现、可移植。
'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');

function findCmake() {
    const candidates = [
        path.join(root, 'node_modules', 'cmake-runtime-win32-x64', 'bin', 'cmake.exe'),
        path.join(root, 'node_modules', 'cmake-runtime-linux-x64', 'bin', 'cmake'),
        path.join(root, 'node_modules', 'cmake-runtime-darwin-x64', 'bin', 'cmake'),
    ];
    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    return process.env.CMAKE_BIN || 'cmake';
}

const cmd = process.argv[2] || 'compile';
const cmakeJsCli = path.join(root, 'node_modules', 'cmake-js', 'bin', 'cmake-js');
const args = [cmakeJsCli, cmd, '-G', 'MinGW Makefiles', '-c', findCmake(), ...process.argv.slice(3)];

const result = spawnSync(process.execPath, args, { cwd: root, stdio: 'inherit' });
process.exit(result.status === null ? 1 : result.status);

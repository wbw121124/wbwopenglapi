// scripts/build.cjs - wbwopenglapi Node 插件构建入口
//   背景：Node 24 的 child_process.spawn 不再隐式解析 .cmd 后缀，
//   cmake-js 以裸 'cmake' spawn 时在 Windows 上 ENOENT。
//   此处解析 cmake-runtime 提供的真实 cmake 可执行文件绝对路径，
//   经 cmake-js 的 -c 参数显式传入，保证构建可复现、可移植。
//   CMAKE_JS_EXTRA_ARGS 环境变量可追加 cmake-js 参数（如交叉编译
//   --CXXFLAGS=-m32），Linux/macOS 无 cmake-runtime 包时回退 'cmake'。
'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');

function findCmake() {
    const candidates = [
        path.join(root, 'node_modules', 'cmake-runtime-win32-x64', 'bin', 'cmake.exe'),
        path.join(root, 'node_modules', 'cmake-runtime-linux-x64', 'bin', 'cmake'),
        path.join(root, 'node_modules', 'cmake-runtime-linux-arm64', 'bin', 'cmake'),
        path.join(root, 'node_modules', 'cmake-runtime-darwin-x64', 'bin', 'cmake'),
        path.join(root, 'node_modules', 'cmake-runtime-darwin-arm64', 'bin', 'cmake'),
    ];
    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    return process.env.CMAKE_BIN || 'cmake';
}

const isWin = process.platform === 'win32';
const cmd = process.argv[2] || 'compile';
const cmakeJsCli = path.join(root, 'node_modules', 'cmake-js', 'bin', 'cmake-js');
const args = [cmakeJsCli, cmd, ...(isWin ? ['-G', 'MinGW Makefiles'] : []), '-c', findCmake()];

const extra = (process.env.CMAKE_JS_EXTRA_ARGS || '').trim();
if (extra) args.push(...extra.split(/\s+/));
args.push(...process.argv.slice(3));

const result = spawnSync(process.execPath, args, { cwd: root, stdio: 'inherit' });
process.exit(result.status === null ? 1 : result.status);
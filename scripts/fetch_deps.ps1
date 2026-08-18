# fetch_deps.ps1 - 下载并安装 wbwopenglapi 的第三方依赖到 third_party/
#   GLFW 3.4   : GitHub 官方预编译 MinGW 包（经 ghfast.top 代理）
#   GLAD 2     : gen.glad.sh 在线生成器（gl:core=3.3, C）
#   FreeType   : msys2 仓库 mingw-w64-x86_64-freetype（可选，Windows 上仅 FreeType 后端需要）
#   HarfBuzz   : 官方源码自编译（可选，-HarfBuzz；产物仅依赖 freetype.dll，供 OpenType 整形）
#   Fira Code  : 官方 release（供 examples/10_ligature.cpp 验证连体）
# 用法: powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 [-HarfBuzz]
param(
    [switch]$SkipGLFW,
    [switch]$SkipGLAD,
    [switch]$SkipFreeType,
    [switch]$HarfBuzz,
    [switch]$SkipFiraCode
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$third = Join-Path $root 'third_party'
$tmp = Join-Path $env:TEMP 'wbwopenglapi_deps'
$zstdExe = Join-Path $tmp 'zstd.exe'
New-Item -ItemType Directory -Force -Path $third, $tmp | Out-Null

function Invoke-Download {
    param([string]$Url, [string]$Out, [int]$TimeoutSec = 300)
    Write-Host "[fetch] $Url"
    & curl.exe -sL --fail --max-time $TimeoutSec -o $Out $Url
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Out)) { throw "下载失败: $Url" }
}

# ---------------- GLFW ----------------
$glfwDone = Test-Path (Join-Path $third 'glfw\include\GLFW\glfw3.h')
if ($SkipGLFW) { $glfwDone = $true }
if (-not $glfwDone) {
    $zip = Join-Path $tmp 'glfw.zip'
    Invoke-Download 'https://ghfast.top/https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.WIN64.zip' $zip
    $dst = Join-Path $tmp 'glfw_x'
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    Expand-Archive -Path $zip -DestinationPath $dst
    $inner = Get-ChildItem $dst -Directory | Select-Object -First 1
    Copy-Item -Recurse -Force (Join-Path $inner.FullName 'include\GLFW') (Join-Path $third 'glfw\include\')
    Copy-Item -Force (Join-Path $inner.FullName 'lib-mingw-w64\libglfw3dll.a'),
                     (Join-Path $inner.FullName 'lib-mingw-w64\libglfw3.a'),
                     (Join-Path $inner.FullName 'lib-mingw-w64\glfw3.dll'),
                     (Join-Path $inner.FullName 'LICENSE.md') (Join-Path $third 'glfw\')
    Write-Host '[ok] GLFW -> third_party/glfw'
}

# ---------------- GLAD (glad2, gl:core=3.3) ----------------
$gladDone = (Test-Path (Join-Path $third 'glad\include\glad\gl.h')) -and
            (Test-Path (Join-Path $third 'glad\src\gl.c'))
if ($SkipGLAD) { $gladDone = $true }
if (-not $gladDone) {
    $hdrs = Join-Path $tmp 'glad_headers.txt'
    # 注意: 不能用 -X POST, 否则 302 跟随后仍用 POST 导致 405
    & curl.exe -s -D $hdrs -o NUL `
        -F 'generator=c' -F 'specification=gl' -F 'api=gl=3.3' -F 'profile=gl=core' `
        -F 'language=c' -F 'extensions=none' -F 'output=glad.zip' 'https://gen.glad.sh/generate'
    $m = Select-String -Path $hdrs -Pattern '^Location: (.+)$' | Select-Object -First 1
    if (-not $m) { throw 'GLAD 生成失败: 未收到重定向 Location' }
    $loc = $m.Matches[0].Groups[1].Value.Trim().TrimEnd('/')
    $zipUrl = 'https://gen.glad.sh' + $loc + '/glad.zip'
    $zip = Join-Path $tmp 'glad.zip'
    Invoke-Download $zipUrl $zip
    $dst = Join-Path $tmp 'glad_x'
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    Expand-Archive -Path $zip -DestinationPath $dst
    New-Item -ItemType Directory -Force -Path (Join-Path $third 'glad') | Out-Null
    Copy-Item -Recurse -Force (Join-Path $dst 'include\glad') (Join-Path $third 'glad\include\')
    Copy-Item -Recurse -Force (Join-Path $dst 'include\KHR') (Join-Path $third 'glad\include\')
    New-Item -ItemType Directory -Force -Path (Join-Path $third 'glad\src') | Out-Null
    Copy-Item -Force (Join-Path $dst 'src\gl.c') (Join-Path $third 'glad\src\gl.c')
    Write-Host '[ok] GLAD -> third_party/glad'
}

# ---------------- FreeType (可选) ----------------
# 源: ubawurinna/freetype-windows-binaries（官方源码预编译 x64 DLL，
#     仅依赖 Universal CRT；msys2 版 libfreetype-6.dll 依赖
#     harfbuzz/libpng 等 DLL 链，MinGW 直链过深，故改用此源）
$ftDone = Test-Path (Join-Path $third 'freetype\include\ft2build.h')
if ($SkipFreeType) { $ftDone = $true }
if (-not $ftDone) {
    try {
        $zip = Join-Path $tmp 'ftwb.zip'
        Invoke-Download 'https://codeload.github.com/ubawurinna/freetype-windows-binaries/zip/refs/heads/master' $zip
        $dst = Join-Path $tmp 'ftwb_x'
        if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
        New-Item -ItemType Directory -Force -Path $dst | Out-Null
        tar -xf $zip -C $dst
        $src = Get-ChildItem $dst -Directory | Select-Object -First 1
        New-Item -ItemType Directory -Force -Path (Join-Path $third 'freetype') | Out-Null
        Copy-Item -Recurse -Force (Join-Path $src 'include') (Join-Path $third 'freetype\include\')
        Copy-Item -Force (Join-Path $src 'release dll\x64\freetype.dll') (Join-Path $third 'freetype\bin\')
        Write-Host '[ok] FreeType -> third_party/freetype'
    } catch {
        Write-Warning "FreeType 获取失败（可选依赖，GDI 后端不受影响）: $_"
    }
}

# ---------------- HarfBuzz (可选, OpenType 整形) ----------------
# 源: 官方源码自编译（harfbuzz 14.3.1, meson + MinGW gcc）
#   禁用 glib/icu/gobject/cairo，仅启用 freetype；产物 harfbuzz.dll
#   依赖链: freetype.dll + libgcc/msvcrt（与现有 freetype.dll 模式一致）
$hbDone = Test-Path (Join-Path $third 'harfbuzz\bin\harfbuzz.dll')
if ($HarfBuzz) {
    if (-not (Test-Path (Join-Path $third 'freetype\bin\freetype.dll'))) {
        throw 'HarfBuzz 需要先就绪 FreeType: 请先运行 fetch_deps.ps1 安装 freetype'
    }
    if (-not $hbDone) {
        $oldPath = $env:Path
        try {
            Write-Host '[fetch] HarfBuzz 自编译（pip meson/ninja + 源码）...'
            # 1. pip 安装 meson/ninja（user site）
            python -m pip install --quiet meson ninja
            if ($LASTEXITCODE -ne 0) { throw 'pip 安装 meson/ninja 失败' }
            $pyScripts = Join-Path (Split-Path (python -m site --user-site) -Parent) 'Scripts'
            $meson = Join-Path $pyScripts 'meson.exe'
            $ninja = Join-Path $pyScripts 'ninja.exe'
            if (-not (Test-Path $meson) -or -not (Test-Path $ninja)) {
                throw "meson/ninja 未安装到 $pyScripts"
            }
            # 2. 下载源码
            $srcUrl = 'https://ghfast.top/https://github.com/harfbuzz/harfbuzz/releases/download/14.3.1/harfbuzz-14.3.1.tar.xz'
            $tar = Join-Path $tmp 'harfbuzz-14.3.1.tar.xz'
            if (-not (Test-Path $tar)) { Invoke-Download $srcUrl $tar 900 }
            $srcDir = Join-Path $tmp 'hb_src'
            if (Test-Path $srcDir) { Remove-Item -Recurse -Force $srcDir }
            New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
            # 3. 解压（Python lzma/tarfile: Windows bsdtar 无 xz 过滤器）
            python -c "import tarfile,sys; tarfile.open(sys.argv[1],'r:xz').extractall(sys.argv[2])" $tar $srcDir
            $hbSrc = Get-ChildItem $srcDir -Directory | Select-Object -First 1
            # 4. 提供 pkg-config（meson 查 freetype2 用；本项目无 pkg-config.exe）
            $pcDir = Join-Path $tmp 'pkgconfig'
            New-Item -ItemType Directory -Force -Path $pcDir | Out-Null
            $ftThird = (Join-Path $third 'freetype').Replace('\', '/')
            @"
prefix=$ftThird
libdir=`${prefix}/bin
includedir=`${prefix}/include

Name: FreeType 2
Description: A free, high-quality, and portable font engine.
Version: 26.1.20
Libs: -L`${libdir} -lfreetype
Cflags: -I`${includedir}
"@ | Set-Content -Path (Join-Path $pcDir 'freetype2.pc') -Encoding ascii
            @'
import os, re, sys, glob

def read_pc(name):
    paths = os.environ.get('PKG_CONFIG_PATH', '').split(os.pathsep)
    for d in paths:
        if not d:
            continue
        for f in glob.glob(os.path.join(d, '*.pc')):
            base = os.path.splitext(os.path.basename(f))[0]
            if base == name:
                return parse_pc(f)
    return None

def parse_pc(path):
    vars_ = {}
    lines = []
    with open(path, 'r', encoding='utf-8-sig') as fh:
        for ln in fh:
            ln = ln.strip()
            if not ln or ln.startswith('#'):
                continue
            m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$', ln)
            if m:
                vars_[m.group(1)] = m.group(2)
            else:
                lines.append(ln)
    def expand(s):
        for _ in range(10):
            if '${' not in s:
                break
            for k, v in vars_.items():
                s = s.replace('${' + k + '}', v)
        return s
    result = {'vars': {k: expand(v) for k, v in vars_.items()}, 'fields': {}}
    for ln in lines:
        m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*)$', ln)
        if m:
            result['fields'][m.group(1)] = expand(m.group(2))
    return result

def main():
    args = sys.argv[1:]
    name = None
    var = None
    mode = 'print'
    for a in args:
        if a.startswith('--variable='):
            var = a[len('--variable='):]
        elif a.startswith('--define-variable='):
            pass
        elif a.startswith('--atleast-version=') or a.startswith('--exact-version=') or a.startswith('--max-version='):
            mode = 'check'
        elif a in ('--exists', '--atleast-version', '--exact-version', '--max-version',
                   '--modversion', '--cflags', '--cflags-only-I', '--libs', '--libs-only-L',
                   '--libs-only-l', '--print-errors', '--short-errors', '--silence-errors',
                   '--static', '--help', '--list-all', '--print-provides', '--print-requires'):
            mode = 'print'
        elif a == '--version':
            print('1.8.0')
            return 0
        elif a.startswith('-'):
            pass
        elif name is None:
            name = a
    if mode == 'check':
        return 0 if read_pc(name) is not None else 1
    pc = read_pc(name) if name else None
    if pc is None:
        sys.stderr.write('Package %s not found\n' % name)
        return 1
    if var:
        print(pc['vars'].get(var, ''))
        return 0
    if '--modversion' in args:
        print(pc['vars'].get('Version', '') or pc['fields'].get('Version', ''))
        return 0
    f = pc['fields']
    if '--cflags' in args or '--cflags-only-I' in args:
        print(f.get('Cflags', ''))
    if '--libs' in args or '--libs-only-L' in args or '--libs-only-l' in args:
        print(f.get('Libs', ''))
    if '--list-all' in args:
        print('%s %s' % (name, f.get('Description', '')))
    return 0

if __name__ == '__main__':
    sys.exit(main())
'@ | Set-Content -Path (Join-Path $tmp 'pkg-config.py') -Encoding ascii
            '@echo off
python "%~dp0pkg-config.py" %*
'@ | Set-Content -Path (Join-Path $tmp 'pkg-config.cmd') -Encoding ascii
            # 5. meson 配置 + 编译 + 安装
            $oldPath = $env:Path
            $env:Path = "$tmp;$pyScripts;$oldPath"
            $env:PKG_CONFIG_PATH = $pcDir
            $buildDir = Join-Path $hbSrc.FullName 'build'
            & $meson setup --wipe $buildDir --buildtype=release `
                -Dglib=disabled -Dgobject=disabled -Dicu=disabled -Dcairo=disabled `
                -Dfreetype=enabled -Dgraphite2=disabled -Dfontations=disabled `
                -Dwasm=disabled -Dutilities=disabled -Dtests=disabled `
                -Dbenchmark=disabled -Ddocs=disabled -Dintrospection=disabled `
                -Ddefault_library=shared
            if ($LASTEXITCODE -ne 0) { throw 'meson setup 失败' }
            & $meson compile -C $buildDir
            if ($LASTEXITCODE -ne 0) { throw 'meson compile 失败' }
            $stage = Join-Path $tmp 'hb_stage'
            if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
            & $meson install -C $buildDir --destdir $stage
            if ($LASTEXITCODE -ne 0) { throw 'meson install 失败' }
            $env:Path = $oldPath
            # 6. 拷贝到 third_party/harfbuzz（保留 DLL 原名: MinGW 链接时
            #    以 PE 导出表内的 DLL name 作为运行时依赖名，重命名会导致 0xC0000135）
            New-Item -ItemType Directory -Force -Path (Join-Path $third 'harfbuzz\include'), (Join-Path $third 'harfbuzz\bin') | Out-Null
            Copy-Item -Recurse -Force (Join-Path $stage 'include\harfbuzz') (Join-Path $third 'harfbuzz\include\')
            Copy-Item -Force (Join-Path $stage 'bin\libharfbuzz-0.dll') (Join-Path $third 'harfbuzz\bin\')
            Write-Host '[ok] HarfBuzz -> third_party/harfbuzz'
        } catch {
            $env:Path = $oldPath
            Write-Warning "HarfBuzz 获取失败（可选依赖，不影响 FreeType 后端）: $_"
        }
    }
}

# ---------------- Fira Code (示例用字体, 10_ligature.cpp) ----------------
$fcDone = Test-Path (Join-Path $third 'fonts\FiraCode-Regular.ttf')
if ($SkipFiraCode) { $fcDone = $true }
if (-not $fcDone) {
    try {
        $zip = Join-Path $tmp 'firacode.zip'
        Invoke-Download 'https://github.com/tonsky/FiraCode/releases/download/6.2/Fira_Code_v6.2.zip' $zip
        $dst = Join-Path $tmp 'fc_x'
        if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
        Expand-Archive -Path $zip -DestinationPath $dst
        New-Item -ItemType Directory -Force -Path (Join-Path $third 'fonts') | Out-Null
        Copy-Item -Force (Join-Path $dst 'ttf\FiraCode-Regular.ttf') (Join-Path $third 'fonts\')
        Write-Host '[ok] Fira Code -> third_party/fonts'
    } catch {
        Write-Warning "Fira Code 获取失败（仅 10_ligature 示例需要）: $_"
    }
}

Write-Host 'fetch_deps.ps1 完成'
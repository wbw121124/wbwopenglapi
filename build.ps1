# build.ps1 - 本机（win32/MinGW）一键构建脚本
#   默认后端 auto: 若 third_party/freetype 已就绪则用 FreeType 后端，否则 GDI 后端
#   -HarfBuzz: 在 FreeType 后端基础上启用 HarfBuzz 整形（需 third_party/harfbuzz 就绪）
#   -Backend dwrite: DirectWrite 矢量字体后端（Windows 8.1+ 系统库，无第三方依赖）
# 用法:
#   powershell -ExecutionPolicy Bypass -File build.ps1                # 构建全部示例
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Backend gdi   # 指定后端
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Backend dwrite # DirectWrite 后端
#   powershell -ExecutionPolicy Bypass -File build.ps1 -HarfBuzz      # FreeType + HarfBuzz
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Example 01_hello -Run
param(
    [ValidateSet('auto', 'gdi', 'freetype', 'dwrite')][string]$Backend = 'auto',
    [string]$Example = '',
    [switch]$Run,
    [switch]$HarfBuzz
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '.')
$third = Join-Path $root 'third_party'
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$freetypeAvailable = Test-Path (Join-Path $third 'freetype\include\ft2build.h')
if ($Backend -eq 'auto') {
    $Backend = if ($freetypeAvailable) { 'freetype' } else { 'gdi' }
}
if ($HarfBuzz -and $Backend -ne 'freetype') {
    throw '-HarfBuzz 仅支持 FreeType 后端（-Backend freetype）'
}
$harfbuzzAvailable = Test-Path (Join-Path $third 'harfbuzz\bin\libharfbuzz-0.dll')
if ($HarfBuzz -and -not $harfbuzzAvailable) {
    throw 'third_party/harfbuzz 未就绪: 请先运行 fetch_deps.ps1 -HarfBuzz'
}
Write-Host "后端: $Backend (FreeType 可用: $freetypeAvailable, HarfBuzz 可用: $harfbuzzAvailable)"

# PNG 支持：系统 zlib 探测（Windows MinGW 自带 libz.a / Linux 系统 libz）。
# 可用 -> 启用 WBWOPENGAL_API_PNG 宏并链接 -lz；否则 19_png 编译为提示 stub。
$pngOk = $true
$zprobe = Join-Path $build 'zprobe.cpp'
Set-Content -LiteralPath $zprobe -Encoding ascii -Value @'
#include <zlib.h>
int main() { return zlibVersion() == 0; }
'@
& g++ $zprobe -lz -o (Join-Path $build 'zprobe.exe') 2>$null
if ($LASTEXITCODE -ne 0) { $pngOk = $false }
Write-Host "PNG(zlib): $pngOk"

function Compile-Example {
    param([string]$Src, [string]$Out, [string]$UseBackend)
    $cxx = 'g++'
    $args = @('-std=c++17', '-O2', '-Wall', '-Wextra',
        "-I$root\include", "-I$third\glad\include", "-I$third\glfw\include")
    $libs = @("-L$third\glfw", '-lglfw3dll', '-lopengl32')
    if ($pngOk) {
        $args += '-DWBWOPENGAL_API_PNG'
        $libs += '-lz'
    }
    if ($UseBackend -eq 'freetype') {
        $args += "-I$third\freetype\include"
        $args += '-DWBWOPENGAL_API_FONT_FREETYPE'
        # 直接链接 DLL（MinGW 可链接 MSVC 编译的 freetype.dll；须在源码之后）
        $libs += (Join-Path $third 'freetype\bin\freetype.dll')
        if ($HarfBuzz) {
            $args += "-I$third\harfbuzz\include"
            $args += '-DWBWOPENGAL_API_FONT_HARFBUZZ'
            $libs += (Join-Path $third 'harfbuzz\bin\libharfbuzz-0.dll')
        }
    } elseif ($UseBackend -eq 'dwrite') {
        # DirectWrite 系统库（MinGW 自带 libdwrite.a）
        $args += '-DWBWOPENGAL_API_FONT_DWRITE'
        $libs += '-ldwrite'
        # 过渡期: 步 2 接入头文件 DWrite 分支前, 宏未生效时回落 GDI 后端, 保留 gdi32
        $libs += '-lgdi32'
    } else {
        $libs += '-lgdi32'
    }
    & $cxx @args $Src (Join-Path $third 'glad\src\gl.c') @libs -o $Out
    if ($LASTEXITCODE -ne 0) { throw "编译失败: $Src" }
    Copy-Item -Force (Join-Path $third 'glfw\glfw3.dll') $build
    if ($UseBackend -eq 'freetype') {
        Copy-Item -Force (Join-Path $third 'freetype\bin\freetype.dll') $build
        if ($HarfBuzz) {
            Copy-Item -Force (Join-Path $third 'harfbuzz\bin\libharfbuzz-0.dll') $build
        }
    }
    Write-Host "[ok] $Out"
}

$sources = if ($Example) {
    @((Join-Path $root "examples\$Example.cpp"))
} else {
    Get-ChildItem (Join-Path $root 'examples') -Filter '*.cpp' | ForEach-Object { $_.FullName }
}
if (-not $sources) { throw '未找到示例源文件' }
if ($Example -eq '14_skia') {
    throw '示例 14_skia 需 Skia 后端（vcpkg + CMake 构建），build.ps1 不提供；请参见 README/plan.md'
}
foreach ($src in $sources) {
    if ((Get-Item $src).Name -eq '14_skia.cpp') { continue } # Skia 示例由 CMake 专用 target 构建
    $name = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $out = Join-Path $build "$name.exe"
    Compile-Example $src $out $Backend
}

if ($Run) {
    $target = Join-Path $build (($(if ($Example) { $Example } else { '01_hello' })) + '.exe')
    Write-Host "运行: $target"
    & $target
}
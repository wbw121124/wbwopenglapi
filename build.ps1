# build.ps1 - 本机（win32/MinGW）一键构建脚本
#   默认后端 auto: 若 third_party/freetype 已就绪则用 FreeType 后端，否则 GDI 后端
#   全量构建时额外编译 03_text 的 GDI 版本 (03_text_gdi.exe) 以验证双后端编译
# 用法:
#   powershell -ExecutionPolicy Bypass -File build.ps1                # 构建全部示例
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Backend gdi   # 指定后端
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Example 01_hello -Run
param(
    [ValidateSet('auto', 'gdi', 'freetype')][string]$Backend = 'auto',
    [string]$Example = '',
    [switch]$Run
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '.')
$third = Join-Path $root 'third_party'
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null

$freetypeAvailable = Test-Path (Join-Path $third 'freetype\include\freetype2\ft2build.h')
if ($Backend -eq 'auto') {
    $Backend = if ($freetypeAvailable) { 'freetype' } else { 'gdi' }
}
Write-Host "后端: $Backend (FreeType 可用: $freetypeAvailable)"

function Compile-Example {
    param([string]$Src, [string]$Out, [string]$UseBackend)
    $cxx = 'g++'
    $args = @('-std=c++17', '-O2', '-Wall', '-Wextra',
        "-I$root\include", "-I$third\glad\include", "-I$third\glfw\include")
    $libs = @("-L$third\glfw", '-lglfw3dll', '-lopengl32')
    if ($UseBackend -eq 'freetype') {
        $args += "-I$third\freetype\include\freetype2"
        $libs += "-L$third\freetype\lib", '-lfreetype'
        $args += '-DWBWOPENGAL_API_FONT_FREETYPE'
    } else {
        $libs += '-lgdi32'
    }
    & $cxx @args $Src (Join-Path $third 'glad\src\gl.c') @libs -o $Out
    if ($LASTEXITCODE -ne 0) { throw "编译失败: $Src" }
    Copy-Item -Force (Join-Path $third 'glfw\glfw3.dll') $build
    if ($UseBackend -eq 'freetype') {
        Copy-Item -Force (Join-Path $third 'freetype\bin\libfreetype-6.dll') $build
    }
    Write-Host "[ok] $Out"
}

$sources = if ($Example) {
    @((Join-Path $root "examples\$Example.cpp"))
} else {
    Get-ChildItem (Join-Path $root 'examples') -Filter '*.cpp' | ForEach-Object { $_.FullName }
}
if (-not $sources) { throw '未找到示例源文件' }

foreach ($src in $sources) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($src)
    $out = Join-Path $build "$name.exe"
    Compile-Example $src $out $Backend
    if (-not $Example -and $Backend -eq 'freetype' -and $name -eq '03_text') {
        Compile-Example $src (Join-Path $build '03_text_gdi.exe') 'gdi'
    }
}

if ($Run) {
    $target = Join-Path $build (($(if ($Example) { $Example } else { '01_hello' })) + '.exe')
    Write-Host "运行: $target"
    & $target
}
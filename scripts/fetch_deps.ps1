# fetch_deps.ps1 - 下载并安装 wbwopenglapi 的第三方依赖到 third_party/
#   GLFW 3.4   : GitHub 官方预编译 MinGW 包（经 ghfast.top 代理）
#   GLAD 2     : gen.glad.sh 在线生成器（gl:core=3.3, C）
#   FreeType   : msys2 仓库 mingw-w64-x86_64-freetype（可选，Windows 上仅 FreeType 后端需要）
# 用法: powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1
param(
    [switch]$SkipGLFW,
    [switch]$SkipGLAD,
    [switch]$SkipFreeType
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

Write-Host 'fetch_deps.ps1 完成'
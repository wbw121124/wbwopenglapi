# wbwopenglapi

Header-only 的 C++17 OpenGL 2D 绘图库，API 风格贴近 JavaScript 的 Canvas 2D。
全部实现集中于单个头文件 `include/wbwopenglapi.hpp`，无需 .cpp 编译依赖。

```cpp
#include <wbwopenglapi.hpp>

int main() {
    wbwopenglapi::Window win(800, 600, "Demo");
    wbwopenglapi::Canvas ctx(win);
    while (!win.shouldClose()) {
        ctx.clear("#f5f5f5");
        ctx.fillStyle("#ff8000");
        ctx.fillRect(50, 50, 200, 150);
        ctx.beginPath();
        ctx.arc(400, 300, 80, 0, 6.2832);
        ctx.fill();
        ctx.fillStyle("#204060");
        ctx.font("32px sans-serif");
        ctx.fillText("Hello, wbwopenglapi", 60, 260);
        win.pollEvents();
        win.swapBuffers();
    }
    return 0;
}
```

## 特性

- **Canvas 2D 风格 API**：矩形、路径（arc / 二次三次贝塞尔 / closePath）、
  矢量文本（fillText / strokeText / measureText）、变换矩阵栈
  （translate / rotate / save / restore）、drawImage
- **矢量文本三后端**：Windows 默认 GDI（零第三方依赖），可选 FreeType 或
  DirectWrite（Windows 8.1+ 系统库，无第三方依赖）；Linux 使用 FreeType。
  字形轮廓与用户路径共用同一 fill/stroke 管线
- **OpenType 特性（可选）**：FreeType + HarfBuzz 时支持连体字与
  `font-feature-settings` 风格特性（liga / calt / cvNN / ssNN / zero 等），
  fontFeatures() 显式开启，默认全关、输出与无 HarfBuzz 时像素级一致
- **纯标准库**：CSS 颜色解析、BMP 解码、粗线三角带生成均无第三方依赖
- **现代 OpenGL**：GL 3.3 core，CPU 端顶点变换 + stencil even-odd 填充，
  无弃用 API

## 构建

### Windows（MinGW）

```
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1   # 首次：下载依赖
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 -HarfBuzz   # 可选：HarfBuzz（连体）
powershell -ExecutionPolicy Bypass -File build.ps1 -Example 08_demo
powershell -ExecutionPolicy Bypass -File build.ps1 -HarfBuzz -Example 10_ligature
```

参数：`-Example`（examples 下的示例名，如 02_shapes / 05_text / 08_demo），
`-Backend gdi|freetype|dwrite`（默认 auto：FreeType 优先；dwrite 为 Windows 8.1+
DirectWrite 系统后端），`-HarfBuzz`（在 FreeType 基础上启用 HarfBuzz 整形；
build.ps1 须用 PowerShell 7+ 运行）。

### Linux（CMake）

```
sudo apt install libglfw3-dev libfreetype-dev libharfbuzz-dev
cmake -B build -DWBWOPENGAL_API_FONT_HARFBUZZ=ON && cmake --build build
```

编译时必须定义 `WBWOPENGAL_API_FONT_FREETYPE` 并链接 FreeType（Linux 经
`find_package(Freetype)`；Windows 由 `scripts/fetch_deps.ps1` 获取
ubawurinna/freetype-windows-binaries 的预编译 `freetype.dll`，仅依赖
Universal CRT，MinGW 直接链接该 DLL）。Windows 不定义该宏则自动使用
系统 GDI 后端（零第三方依赖）。定义 `WBWOPENGAL_API_FONT_DWRITE`
（Windows 8.1+，链接系统 `-ldwrite`，仅需 dwrite.h/d2d1_1.h 头）可切换到
DirectWrite 后端——与 GDI 相同的默认字体回退序列（Segoe UI -> Arial ->
微软雅黑）与字形空间语义，且与 FreeType 互斥。定义 `WBWOPENGAL_API_FONT_HARFBUZZ`
（Windows 链接 third_party/harfbuzz 自编译的 `libharfbuzz-0.dll`，仅依赖
freetype.dll；Linux 经 pkg-config harfbuzz）后启用 OpenType 特性。

### Node.js（Node-API，napi/）

```
cd napi
npm install        # node-addon-api + cmake-js + cmake-runtime
npm run build      # 生成 build/wbwopenglapi.node（MinGW；需已编译 third_party/glfw）
npm test           # node:test 自动化测试（10 项）+ smoke
```

```js
const { createCanvas, loadBMP } = require('./napi/lib/index.js');

const c = createCanvas(800, 600);
c.clear([245, 245, 245, 255]);
c.fillStyle('#ff8000');
c.fillRect(50, 50, 200, 150);
c.font('32px sans-serif');
c.fillText('Hello from Node.js', 60, 260);
c.resolve();                          // 渲染当前帧到默认帧缓冲
const px = c.readPixels(0, 0, c.width, c.height); // RGBA Buffer（y 向下）
fs.writeFileSync('out.bmp', c.toBMP());
c.close();
```

构建链：cmake-js + MinGW Makefiles + cmake-runtime 提供的 cmake.exe。
颜色参数支持 CSS 字符串、`{r,g,b,a}` 对象、`[r,g,b,a]` 数组（数值 0..255 或 0..1）。
隐藏窗口无头渲染，HiDPI 下 framebuffer 可能非等比缩放（画布坐标保持逻辑像素）。
类构造函数按 env 键控存储（`napi_set_instance_data`），worker_threads 多
环境各自独立，黑白名单无共享状态。

## 发布（GitHub Actions）

推送 `v*` 标签（或手动 `workflow_dispatch`）触发 `.github/workflows/release.yml`，
在 7 个平台组合上构建并发布两个包到 Releases：

- `wbwopenglapi-<os>-<arch>.tar.gz`：核心包（header-only 库：include/ + CMakeLists/
  README；Windows 额外含 glfw3.dll 与导入库）
- `wbwopenglapi-napi-<os>-<arch>.tar.gz`：Node 插件（build/*.node + lib/index.js +
  package.json，解压后 `npm install <tarball>` 可用）

平台：linux / windows（amd64、x86）/ macos（amd64、arm64）全覆盖；
windows arm64 无官方 MinGW 工具链与 runner，不提供。x86 无官方原生 runner：
linux-x86 用 gcc -m32 + i386 系统库交叉，windows-x86 用 msys2 MINGW32 i686
工具链（交叉产物不做运行时测试，x64 Node 无法加载 32 位插件）。

## 示例

| 示例 | 内容 |
|---|---|
| 01_hello | 窗口 + 清屏 + 矩形 |
| 02_shapes | fillRect / strokeRect / clearRect / 样式 / globalAlpha |
| 04_path | 路径：fill(stencil even-odd) / stroke / arc / 贝塞尔 / 挖空 |
| 05_text | 矢量文本：fillText / strokeText / measureText / 对齐基线 |
| 06_transform | 变换矩阵栈：translate / rotate / save / restore |
| 07_image | loadBMP / drawImage（原尺寸与缩放） |
| 08_demo | 综合演示：全部 API + 动画 |
| 09_text_lines | 多行文本混排（中西文 / 对齐 / 基线 / 字体切换） |
| 10_ligature | OpenType 特性：calt 连体 / cv02 / zero（需 Fira Code + HarfBuzz） |
| 13_dwrite_text | DirectWrite 后端文本验证（fillText / strokeText / 中文回退；无 DWrite 宏时等同 05） |

所有示例支持 `-t` 测试模式：渲染 0.5 秒后逐像素校验，全部通过退出码 0。

## API 一览

```cpp
namespace wbwopenglapi {

struct Color;                       // 0..1，可从 CSS 字符串解析
struct Image;                       // RGBA 位图
Image loadBMP(const std::string& path);

class Window;                       // GLFW 窗口与上下文（RAII）

enum class TextAlign { Left, Center, Right };
enum class TextBaseline { Top, Middle, Alphabetic, Bottom };

class Canvas {                      // 即 "ctx"
    void clear(const Color& / const std::string& css);
    // 样式
    void fillStyle(const Color& / const std::string& css);
    void strokeStyle(const Color& / const std::string& css);
    void lineWidth(double w);
    void globalAlpha(double a);
    void font(const std::string& css);   // "16px sans-serif" 或字体文件路径
    void textAlign(TextAlign a);
    void textBaseline(TextBaseline b);
    void fontFeatures(const std::string& css);  // "liga, calt 1, cv02 0"（HarfBuzz 时生效）
    void fontFeatures(std::initializer_list<std::pair<std::string, bool>> feats);
    void resetFontFeatures();           // 关闭全部特性（默认状态）
    // 矩形
    void fillRect(x, y, w, h);  void strokeRect(x, y, w, h);
    void clearRect(x, y, w, h);
    // 路径
    void beginPath();  void moveTo(x, y);  void lineTo(x, y);
    void quadraticCurveTo(cx, cy, x, y);
    void bezierCurveTo(c1x, c1y, c2x, c2y, x, y);
    void arc(cx, cy, r, a0, a1, bool ccw = false);
    void rect(x, y, w, h);  void closePath();
    void fill();  void stroke();
    // 文本（矢量轮廓）
    void fillText(text, x, y, maxWidth = 0);
    void strokeText(text, x, y, maxWidth = 0);
    double measureText(text) const;
    // 变换（后调用先应用）
    void translate(dx, dy);  void rotate(rad);
    void save();  void restore();  void resetTransform();
    // 图像
    void drawImage(const Image& img, x, y, w = 0, h = 0);  // w/h=0 原尺寸
};

} // namespace wbwopenglapi
```

## 坐标系

Canvas 坐标：左上原点、y 向下、单位像素（与 Web Canvas 2D 一致），
HiDPI 自动按 framebuffer 尺寸适配。路径 fill 采用 stencil even-odd
两遍法（支持凹形/自交/多环挖空），stroke 为 CPU 粗线三角带生成。

## 技术要点

- GL 3.3 core；顶点在 CPU 端变换为 NDC 后直写 `gl_Position`
  （规避部分驱动对 GL_FLOAT attribute 大数值与 mat3 uniform 的读取异常）
- 字形空间统一为 1/64 像素、y 向上、基线 y=0；GDI 输出（像素单位）×64、
  DirectWrite 输出（像素单位，y 向下取反）×64、FreeType（1/64 像素）对齐；
  DirectWrite 缺字形时经回退链（Segoe UI -> Arial -> 微软雅黑）顺次尝试
- HarfBuzz 整形（可选宏）：FontFace 持有 hb_font_t（hb_ft_font_create_referenced，
  继承 FT 的 1/64 像素 scale），shape() 输出 glyph 索引 + 位移/步进；
  字形缓存键为字形索引（特性已编码进索引）。**默认特性全关**：features 为空时
  走逐码点路径（与无 HarfBuzz 时像素级一致）；fontFeatures() 显式开启后
  才调用 hb_shape。GDI 后端无连体（忽略 fontFeatures）
- glTexImage2D 像素数组首行位于纹理坐标 v=0，drawImage 据此映射
  （画布顶 = v=0）
- 依赖：GLFW 3.4 + GLAD（gl 3.3 core）+ 可选 FreeType + 可选 HarfBuzz；
  其余纯标准库

## 目录结构

```
wbwopenglapi/
├─ include/wbwopenglapi.hpp   # 唯一头文件：全部 API 实现
├─ third_party/               # 依赖（gitignore）
│  ├─ fonts/                  # Fira Code（10_ligature 用，fetch_deps 下载）
│  └─ harfbuzz/               # HarfBuzz 自编译（fetch_deps.ps1 -HarfBuzz）
├─ examples/                  # 01_hello ... 13_dwrite_text
├─ scripts/fetch_deps.ps1     # 下载依赖（Windows；-HarfBuzz 自编译 HarfBuzz）
├─ build.ps1                  # 一键构建（Windows/MinGW）
├─ CMakeLists.txt             # 跨平台构建
├─ napi/                      # Node-API 绑定包（lib/index.js + src/*.cc + 测试）
└─ plan.md                    # 开发计划与排障记录
```
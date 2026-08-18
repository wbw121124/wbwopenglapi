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
- **矢量文本双后端**：Windows 默认 GDI（零第三方依赖），可选 FreeType；
  Linux 使用 FreeType。字形轮廓与用户路径共用同一 fill/stroke 管线
- **纯标准库**：CSS 颜色解析、BMP 解码、粗线三角带生成均无第三方依赖
- **现代 OpenGL**：GL 3.3 core，CPU 端顶点变换 + stencil even-odd 填充，
  无弃用 API

## 构建

### Windows（MinGW）

```
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1   # 首次：下载依赖
powershell -ExecutionPolicy Bypass -File build.ps1 -Example 08_demo
```

参数：`-Example`（examples 下的示例名，如 02_shapes / 05_text / 08_demo），
`-Backend gdi|freetype`（默认 auto：FreeType 优先）。

### Linux（CMake）

```
sudo apt install libglfw3-dev libfreetype-dev
cmake -B build && cmake --build build
```

编译时必须定义 `WBWOPENGAL_API_FONT_FREETYPE` 并链接 FreeType（Linux 经
`find_package(Freetype)`；Windows 由 `scripts/fetch_deps.ps1` 获取
ubawurinna/freetype-windows-binaries 的预编译 `freetype.dll`，仅依赖
Universal CRT，MinGW 直接链接该 DLL）。Windows 不定义该宏则自动使用
系统 GDI 后端（零第三方依赖）。

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
- 字形空间统一为 1/64 像素、y 向上、基线 y=0；GDI 输出（像素单位）×64
  与 FreeType（1/64 像素）对齐
- glTexImage2D 像素数组首行位于纹理坐标 v=0，drawImage 据此映射
  （画布顶 = v=0）
- 依赖：GLFW 3.4 + GLAD（gl 3.3 core）+ 可选 FreeType；其余纯标准库

## 目录结构

```
wbwopenglapi/
├─ include/wbwopenglapi.hpp   # 唯一头文件：全部 API 实现
├─ third_party/               # 依赖（gitignore）
├─ examples/                  # 01_hello ... 08_demo
├─ scripts/fetch_deps.ps1     # 下载依赖（Windows）
├─ build.ps1                  # 一键构建（Windows/MinGW）
├─ CMakeLists.txt             # 跨平台构建
└─ plan.md                    # 开发计划与排障记录
```
# wbwopenglapi 开发计划（plan.md）

## 当前进度

- [x] 阶段 1 设计：本文件定稿（2026-08-18）
- [x] 阶段 2 骨架（2026-08-18）
  - 阶段目标：git init 完成；建立 include/ examples/ scripts/ 目录；fetch_deps.ps1 下载
    GLFW/GLAD/FreeType 到 third_party/；build.ps1 一键编译示例；CMakeLists.txt 跨平台；
    wbwopenglapi.hpp 实现 Window RAII + 输入轮询 + GL 版本校验 + 最小 clear。
  - 完成情况：全部达成。01_hello 双后端（freetype/gdi）编译通过并运行验证
    （GL 3.3.0 / Intel UHD 630 / 退出码 0）。
  - 上下文变更：GLAD 用 glad2（glad.sh 已迁移到 gen.glad.sh，POST /generate 需 302
    后自动转 GET，禁用 -X POST 否则 405）；glad2 生成文件为 src/gl.c（无实现宏）。
    FreeType 2.14.3 取自 msys2 仓库（zstd 解压）；GLFW 3.4 取自 ghfast.top 代理。
    include 顺序：GLAD 在 GLFW 之前 + GLFW_INCLUDE_NONE（防止系统 GL/gl.h 冲突）。
  - 注意事项：build.ps1 默认后端 auto（freetype 优先），全量构建时额外编译
    03_text_gdi.exe 验证 GDI 路径；DLL（glfw3.dll/libfreetype-6.dll）自动拷贝到 build/。
  - 待解决：剩余阶段（矩形/路径/文本/变换/图像/示例/文档）。
- [x] 阶段 3 基础矩形：渲染管线 + fillRect / strokeRect / clearRect（2026-08-18）
  - 阶段目标：GLSL 330 solid 着色器程序（内嵌源码）；GLShader/GLProgram/GLVAO/GLVBO
    RAII 封装；正交投影（左上原点、y 向下、framebuffer 尺寸、HiDPI 兼容）；
    CSS 颜色解析（#RGB/#RRGGBB/#RRGGBBAA/rgb()/rgba()/常用颜色名）；样式状态
    （fillStyle/strokeStyle/lineWidth/globalAlpha，默认值对齐 Canvas 2D）；
    fillRect/strokeRect/clearRect；CPU 粗线三角带生成器（butt 端点 + miter/bevel
    连接，core profile 合规，阶段 4 的 stroke() 复用）；clearRect 用 scissor+透明清屏
    （注意 scissor y 轴方向与逻辑/物理像素换算）。
  - 完成情况：全部达成。02_shapes 双后端（gdi/freetype）测试模式 9/9 像素校验通过
    （退出码 0）。测试点含 top-left 光栅化边界与 alpha 重叠的修正说明。
  - 上下文变更（排障记录，重要）：
    1. **矩阵布局坑**：投影矩阵数组若按"行主序"排列传给 GLSL mat3（列主序），
       平移项会落在第三行被 vec3 乘法丢弃，顶点全部挤在单点画不出。
       实测 Intel UHD 630（Build 27.20.100.9168）上 glGetUniformfv 读回的是
       原始字节（看起来"正确"），但渲染结果已错——不可用读回验证语义。
       最终方案：**CPU 端变换顶点到 NDC，shader 直写 gl_Position**，不再使用
       mat3 uniform；proj_ 矩阵按列主序存储（col0=(2/fw,0,0) col1=(0,-2/fh,0)
       col2=(-1,1,1)），与阶段 6 的 CPU 矩阵栈设计一致。
    2. **GL_FLOAT attribute 大数值读取异常**（同驱动）：顶点值 >~1（如像素坐标
       40/200）时部分 draw 无输出，NDC 小数（-1..1）正常。CPU 变换后属性恒为
       小数，天然规避；已写入 kSolidVS 注释。
    3. **GL top-left 光栅化规则**：奇数像素宽边框（如线宽 1）的任何整数坐标点，
       像素中心恰在边缘会被排除；测试用偶数线宽 + 边框内部点。
    4. **半透明重叠**：globalAlpha 0.3 与 0.6 的两个矩形重叠区像素是两次混合的
       结果（实测 (251,67,67) 与理论一致），测试点避开重叠区。
    5. VertexBuffer::upload 采用首次 glBufferData + 后续 glBufferSubData
       （容量不足才重分配），避免重复重分配。
  - 待解决：剩余阶段（路径/文本/变换/图像/示例/文档）。
- [x] 阶段 4 路径系统（2026-08-18）
  - 阶段目标：beginPath/moveTo/lineTo/quadraticCurveTo/bezierCurveTo/arc/
    closePath/rect/fill/stroke；fill 用 stencil even-odd 两遍法（轮廓三角扇
    GL_INVERT -> 全屏四边形 GL_NOTEQUAL），stroke 复用 buildStrokeStrip；
    贝塞尔用 de Casteljau 递归细分（6 级），arc 角度归一（Δ∈[0,2π) 或
    (-2π,0]）后按弧度步进 π/16 细分。
  - 完成情况：全部达成。04_path 双后端 9/9 像素校验通过（退出码 0），
    覆盖：三角形 fill、圆形 arc、**even-odd 环形挖空**、二次/三次贝塞尔区域、
    未闭合折线 stroke、closePath 闭合菱形 stroke。
  - 上下文变更：fill 的全屏四边形顶点为 NDC（-1..1），drawSolid 增加可选
    mat 参数（nullptr=proj_，否则列主序矩阵，单位矩阵 kIdentity）；新增
    rect() 路径方法（补充进 API 清单）。
  - 待解决：剩余阶段（文本/变换/图像/示例/文档）。
- [x] 阶段 5 矢量文本：GDI / FreeType 双后端（2026-08-18）
  - 阶段目标：font()（"NNpx" 或字体文件路径）/ textAlign / textBaseline /
    fillText / strokeText / measureText；Glyph 缓存；字形轮廓与用户路径共用
    fill/stroke 管线（fill 重构为 fillOutline，stroke 重构为 strokeOutline）；
    默认字体候选表（Windows msyh.ttc/simsun.ttc/arial.ttf/segoeui.ttf，
    Linux DejaVuSans/LiberationSans 等）；统一字形空间：1/64 像素、y 向上、
    基线 y=0，画布坐标 = (tx + px*scale/64, ty - py*scale/64)。
  - 完成情况：全部达成。05_text 双后端（gdi/freetype）测试模式 5/5 像素校验
    通过（退出码 0），覆盖：fillText 方块字符 U+2588、fillText 实心圆 U+25CF、
    strokeText（线宽 8）、中文"中"、背景。02_shapes / 04_path 回归通过。
  - 上下文变更（排障记录，重要）：
    1. **GDI/FreeType 字形单位不一致**：GetGlyphOutlineW（MM_TEXT）输出单位是
       像素，FreeType 输出 1/64 像素。GDI 侧坐标与 advance 必须 ×64 统一，
       否则字形缩小 64 倍不可见（首版全 FAIL 的根因）。
    2. **GDI y 方向**：GGO_NATIVE 输出 y 向上为正（与画布 y 向下相反），
       画布 y = ty - py/64 直接翻转；ascender/descender 取自 OUTLINETEXTMETRICW
       otmAscent/otmDescent（×64，descender 取负）。
    3. **GDI 字形坐标经 hinting 可能略超 ascender/descender 范围**（如 U+2588
       y ∈ [-24, 73] vs asc 70），测试点选字形内部深处避免边缘。
    4. **MAT2 初始化格式**：`MAT2 mat = {{0,1},{0,0},{0,0},{0,1}}`（4 个 FIXED
       平铺），不能按 2×2 分组。
    5. **GDI font linking**：中文字符经字体链接 fallback（Segoe UI -> msyh），
       GetGlyphOutlineW 按需取用链接字体，坐标体系不变，无需特殊处理。
    6. FreeType 后端：FT_Set_Char_Size(0, size*64, 72, 72) 得到像素字号；
       ascender/descender 取 face_->size->metrics；advance 用
       face_->glyph->advance.x（1/64 像素）。
    7. TextAlign/TextBaseline 枚举放顶层命名空间；font() 解析 "NNpx" 前缀
       （字号 ≤0 回退 16px），含 .ttf/.otf/.ttc 或路径分隔符视为文件路径
       （GDI 后端忽略文件名，始终用系统字体）。
  - 待解决：剩余阶段（变换/图像/示例/文档）。
- [x] 阶段 6 进阶：变换矩阵栈 + drawImage / BMP（2026-08-18）
  - 阶段目标：translate/rotate（顺时针，后调用先应用）/ save / restore /
    resetTransform；save/restore 保存变换矩阵 + 全部样式（fillStyle/strokeStyle/
    lineWidth/globalAlpha/textAlign/textBaseline/font）；loadBMP（纯标准库解码
    未压缩 24/32 位 BMP，行序统一自上而下）；drawImage（原尺寸或缩放，纹理
    通道独立 GLSL 程序 + GLTexture RAII + pos/uv 交错 VAO）；drawSolid 集成
    当前变换（有效矩阵 = proj_ * current_，stencil 全屏 quad 不受变换影响）。
  - 完成情况：全部达成。06_transform 双后端 6/6、07_image 双后端 9/9 像素
    校验通过（退出码 0）；02_shapes / 04_path / 05_text 双后端全量回归通过。
  - 上下文变更（排障记录，重要）：
    1. **纹理 v 坐标方向**：glTexImage2D 像素数组首行位于纹理坐标 v=0（GL
       纹素原点左下）。图像行 0（顶行）即数据首行 -> 画布顶对应 v=0，
       画布底 v=1（初版把顶行映射到 v=1，整幅图上下颠倒）。
    2. **BMP 行对齐**：24 位 4 像素行 = 12 字节，已 4 字节对齐**无 padding**
       （rowBytes = ((w*bpp+31)/32)*4 = 12）。测试 BMP 首次生成时错误加了
       4 字节 padding，导致解析结果整幅错位（首版全 FAIL 根因之一）。
    3. BMP bottom-up：biHeight > 0 时文件行序自下而上，解析时按
       srcY = height-1-y 翻转；负 biHeight（top-down）直接顺序读。
    4. **GDI/FreeType 后端与变换/图像无关**：矩阵栈与纹理通道为纯 CPU/GL
       逻辑，双后端行为一致，无字体后端差异。
    5. rotate 矩阵（y 向下坐标系）：[[cos,-sin],[sin,cos]] 在画布上呈
       顺时针（Canvas 语义）；测试用 45 度旋转正方形的**中心点**校验
       （旋转不改变中心，稳健）。
    6. save/restore 的 font 恢复：比较 fontCss_ 字符串，不同则重建
       FontFace 并清字形缓存。
  - 待解决：剩余阶段（示例/文档）。
- [x] 阶段 7 示例：演示程序（2026-08-18）
  - 阶段目标：08_demo 综合演示，覆盖全部 API（矩形/路径/文本/变换/图像）
    并带动画（旋转星形、摆动标题、旋转图像、贝塞尔曲线、even-odd 圆环）。
  - 完成情况：08_demo 双后端（gdi/freetype）编译通过，测试模式运行 3 秒
    自动退出码 0。build.ps1 清理 03_text 残留逻辑（阶段 5 已将文本示例
    重命名为 05_text）。
  - 待解决：剩余阶段（文档）。
- [x] 阶段 8 文档：README + 中文注释（2026-08-18）
  - 阶段目标：README.md（特性/构建/示例/API 一览/坐标系/技术要点/目录结构）。
  - 完成情况：README.md 定稿；头文件关键排障结论（矩阵/单位/v 坐标/对齐）
    已写入 plan.md 各阶段记录与代码注释。
  - 收尾排障（重要）：
    1. **freetype 后端构建假象**：阶段 5/6 的"freetype 构建成功"实为误判——
       build.ps1 编译失败（throw）被 `Select-Object -Last 1` 吞掉，随后运行
       的是 build/ 里上一次 gdi 构建的 exe（像素结果相同），掩盖了失败。
    2. **FT_Outline_Decompose 未声明**：freetype.h 不自动包含 ftoutln.h，
       需显式 `#include <freetype/ftoutln.h>`（已加）。
    3. **msys2 版 libfreetype-6.dll 依赖链过深**：依赖 libharfbuzz-0.dll /
       libpng16-16.dll / libbrotlidec.dll / libbz2-1.dll / zlib1.dll，
       MSVC 版 harfbuzz 又会拖入 glib/icu 等，无法简单拷贝运行
       （0xC0000135 DLL 缺失）。
    4. **换源**：改用 ubawurinna/freetype-windows-binaries（官方源码
       VS2026 预编译 x64 freetype.dll，仅依赖 Universal CRT）。
       MinGW g++ 可**直接链接该 DLL**（须放在源码之后，链接顺序敏感），
       include 路径为 freetype/include（无 freetype2 子目录）；
       fetch_deps.ps1 / build.ps1 / CMakeLists.txt 已同步更新。
    5. **回归纪律**：全量构建须检查每行 [ok] 与警告；运行测试前确认
       exe 依赖（objdump -p）包含 freetype.dll 再判定后端生效。
  - 待解决：全部阶段完成。
- [x] 阶段 9 连体与 OpenType 特性：HarfBuzz 集成（2026-08-18）
  - 阶段目标：fillText 支持连体字与 font-feature-settings 风格特性（liga/calt/
    cvNN/ssNN/zero 等）：fontFeatures(css) / fontFeatures(initializer_list) /
    resetFontFeatures()；默认全关（不调 fontFeatures 时输出与改造前像素级一致）；
    HarfBuzz 为可选依赖（WBWOPENGAL_API_FONT_HARFBUZZ 宏，GDI 后端无连体）。
  - 完成情况：全部达成。10_ligature 在 FreeType+HarfBuzz 配置 7/7 断言通过
    （calt 连体、cv02、zero、多行），三配置（gdi/freetype/freetype+harfbuzz）
    全量回归 9 示例 × 3 = 27 次全部退出码 0。09_text_lines 在 FT+HB 下
    7/7 通过（默认特性空 -> 逐码点回退路径 -> 输出与 FT 后端像素一致）。
  - 上下文变更（排障记录，重要）：
    1. **Fira Code 连体机制**：6.2 版 GSUB 无 liga 特性（FeatureList 实测），
       连体全部在 calt（~100 个 lookup，type 6 chained contextual + type 1
       混合）。"->" 连体 = hyphen_start.seq + greater_hyphen_end.seq 两个
       **部件字形**（advance 与两独立字符完全相同，宽度不变，靠轮廓替换
       实现视觉连体）。calt 是 OpenType **默认特性**：harfbuzz 无显式
       features 时也会应用 -> 这正是"默认全关"决策的意义（不调 fontFeatures
       则库不走 shape，逐码点，严格向后兼容）。
    2. **fontTools ChainSubRule.Input 语义**：Input 数组从**第二个** input
       glyph 开始（第一个在 Coverage 表），Input=[] 表示 inputCount=1
       （仅 coverage glyph），**是标准结构**，不是"空 input 规则"。
       （曾误判为空 input chain context 导致 harfbuzz 不处理，实为误读。）
    3. **harfbuzz 无 features 也会应用 calt**：hb-shape（msys2 14.3.1）与
       自编译 14.3.1、uharfbuzz wheel 三者行为一致：默认输出连体部件
       glyph（hyphen_start.seq=1186 等）；显式 calt=0 时才是普通 hyphen/
       greater（1221/1580）。calt=1 与默认等价。
    4. **Fira Code cv02 = 单层 g**（g->g.cv02，SingleSubst lookup 362），
       不是箭头变体；测试须用 "g" 字符。（首版用 "->" 测 cv02 无效。）
    5. **Fira Code 默认 '0' 是双孔方孔零**：外框 + 上孔 + 下孔（3 contour，
       孔为 45 度菱形），中心点恰在孔边界上 -> 取样须取孔几何中心
       (300+21, 基线-19) 而非 advance 中心。zero 特性把 zero->zero.zero
       （glyph 1005->1015，斜杠穿过中心）。
    6. **HarfBuzz 自编译（Windows/MinGW）**：官方 win64 预编译包为 MSYS2
       构建（无头文件、glib/cairo/png 依赖链深），弃用。改 pip install
       meson ninja（装到 %APPDATA%\Python\Python313\Scripts，不在 PATH）
       + ghfast.top 下载 harfbuzz-14.3.1.tar.xz + Python tarfile r:xz
       解压（Windows bsdtar 无 xz 过滤器）。**meson 需要真实 pkg-config
       二进制**：自写 pkg-config.py + pkg-config.cmd 模拟器（%TEMP%\
       wbwopenglapi_deps\），${var} 引用须多轮迭代展开；freetype2.pc 路径
       必须 D:/wbwopenglapi（项目在 D 盘）；Version 26.1.20 满足 hb
       >=20.0.14 要求（libtool 版本语义）。meson setup 须 --wipe 才能
       刷新 .pc 变化。配置 -Dglib/-Dgobject/-Dicu/-Dcairo/-Dgraphite2/
       -Dfontations/-Dwasm/-Dutilities/-Dtests/-Dbenchmark/-Ddocs/
       -Dintrospection=disabled + -Dfreetype=enabled + -Ddefault_library=
       shared；编译 120/120；产物仅依赖 libgcc_s_seh-1.dll + KERNEL32.dll
       + msvcrt.dll + freetype.dll。
    7. **MinGW DLL 链接命名坑**：exe 对 DLL 的依赖名取自 PE 导出表内
       DLL name（libharfbuzz-0.dll），重命名为 harfbuzz.dll 后 exe 仍
       依赖 libharfbuzz-0.dll（0xC0000135）-> 必须保留原名。
    8. **build.ps1 必须用 pwsh 跑**：PowerShell 5.1 按 ANSI 解码无 BOM
       UTF-8 源码 -> 解析错误；bash 工具的 pwsh 7 正常。控制台中文乱码
       属 GBK 显示问题，不影响。
    9. **Fira Code 6.2 下载**：GitHub API latest tag=6.2，zip 内 ttf/
       FiraCode-Regular.ttf（289624 字节）-> third_party/fonts/。
  - 待解决：无。

## 项目目标

实现一个 **header-only** 的 C++17 库「wbwopenglapi」，让 C++ 的 OpenGL 编程体验尽量接近
JavaScript 的 Canvas 2D API（`ctx.fillRect`、`ctx.beginPath`、`ctx.arc`、`ctx.fill` 等），
显著降低学习与使用门槛。全部实现集中于单个头文件 `include/wbwopenglapi.hpp`，无 .cpp 编译依赖。

平台支持：**Windows**（GLFW + GLAD + GDI 或 FreeType）与 **Linux**（GLFW + GLAD + FreeType）。

## API 设计概览（Canvas 风格函数清单与签名）

```cpp
namespace wbwopenglapi {

struct Color { float r, g, b, a; };                 // 0..1，可从 CSS 字符串解析

struct Image { int width, height; std::vector<uint8_t> rgba; };
Image loadBMP(const std::string& path);             // 纯标准库 BMP 解码（24/32 位）

class Window {                                      // GLFW 窗口与上下文（RAII）
    Window(int w, int h, const std::string& title, bool resizable = true);
    ~Window();
    int width() const;  int height() const;         // 窗口逻辑尺寸
    int framebufferWidth() const;                   // 实际像素尺寸（HiDPI）
    int framebufferHeight() const;
    bool shouldClose() const;
    void pollEvents();
    void swapBuffers();
    bool keyPressed(int key) const;                 // GLFW_KEY_* 轮询
    bool mousePressed(int button) const;            // GLFW_MOUSE_BUTTON_*
    void mousePosition(double& x, double& y) const;
};

enum class TextAlign { Left, Center, Right };
enum class TextBaseline { Top, Middle, Alphabetic, Bottom };

class Canvas {                                      // 即 "ctx"，一次创建、每帧绘制
    explicit Canvas(Window& win);

    // 清屏（扩展 API：Canvas 无背景概念，此处为便捷方法）
    void clear(const Color& c);
    void clear(const std::string& css);

    // ---- 样式属性 ----
    void fillStyle(const Color& c);
    void fillStyle(const std::string& css);
    void strokeStyle(const Color& c);
    void strokeStyle(const std::string& css);
    void lineWidth(double w);
    void globalAlpha(double a);
    void font(const std::string& css);              // "16px sans-serif" 或字体文件路径
    void textAlign(TextAlign a);
    void textBaseline(TextBaseline b);

    // ---- 矩形 ----
    void fillRect(double x, double y, double w, double h);
    void strokeRect(double x, double y, double w, double h);
    void clearRect(double x, double y, double w, double h);

    // ---- 路径 ----
    void beginPath();
    void moveTo(double x, double y);
    void lineTo(double x, double y);
    void quadraticCurveTo(double cx, double cy, double x, double y);
    void bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x, double y);
    void arc(double cx, double cy, double r, double a0, double a1, bool ccw = false);
    void rect(double x, double y, double w, double h);  // 追加矩形子路径
    void closePath();
    void fill();                                    // 用 fillStyle 填充当前路径
    void stroke();                                  // 用 strokeStyle/lineWidth 描边

    // ---- 文本（矢量轮廓）----
    void fillText(const std::string& text, double x, double y, double maxWidth = 0);
    void strokeText(const std::string& text, double x, double y, double maxWidth = 0);
    double measureText(const std::string& text) const;

    // ---- 变换 ----
    void translate(double dx, double dy);
    void rotate(double rad);                        // 弧度，顺时针（Canvas 语义）
    void save();                                    // 压栈：变换 + 全部样式
    void restore();
    void resetTransform();                          // 恢复单位矩阵（仅变换）

    // ---- 图像 ----
    void drawImage(const Image& img, double x, double y,
                   double w = 0, double h = 0);     // w/h 为 0 时按原尺寸
};

} // namespace wbwopenglapi
```

### 典型用法

```cpp
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
```

## 目录结构与文件规划

```
wbwopenglapi/
├─ include/
│  └─ wbwopenglapi.hpp        # 唯一头文件：全部 API 实现（header-only）
├─ third_party/               # 依赖（gitignore，由 scripts/fetch_deps.ps1 生成）
│  ├─ glfw/                   # GLFW 3.4：include/ + lib-mingw-w64/ + glfw3.dll
│  ├─ glad/                   # GLAD gl:core=3.3：src/glad.c + include/glad/ + include/KHR/
│  └─ freetype/               # FreeType（可选，Windows 启用宏时才需要）：include/ + lib/ + dll
├─ examples/
│  ├─ 01_hello.cpp            # 窗口 + 清屏 + 矩形
│  ├─ 02_shapes.cpp           # 路径 / arc / 贝塞尔 / 样式
│  ├─ 03_text.cpp             # 矢量文本（GDI 与 FreeType 双后端编译验证）
│  ├─ 04_transform.cpp        # translate / rotate / save / restore 动画
│  └─ 05_image.cpp            # drawImage + BMP
├─ scripts/
│  └─ fetch_deps.ps1          # curl 下载 GLFW(ghfast.top 代理) / GLAD(web 生成器) / FreeType(msys2)
├─ build.ps1                  # 本机（win32/MinGW）一键：fetch + g++ 编译全部示例 + 拷贝 DLL
├─ CMakeLists.txt             # 跨平台构建（Linux: find_package；Windows: third_party）
├─ plan.md                    # 本文档
└─ README.md                  # 使用与构建文档（阶段 8）
```

## 依赖选择与理由

| 依赖 | 理由 |
|---|---|
| **GLFW 3.4** | 官方预编译 MinGW 二进制（含 `libglfw3dll.a` 与 `glfw3.dll`），无需 CMake 即可 g++ 链接；跨平台窗口/上下文/输入/HiDPI 支持成熟；本机无 vcpkg/winget，用 ghfast.top 代理下载（已验证可达） |
| **GLAD（gl:core=3.3）** | 现代 OpenGL 加载器的事实标准；由 glad.dav1d.de 生成器生成 gl 3.3 core 的 glad.c+glad.h，文件少且可控；生成服务已验证可达（HTTP 200）。**兜底**：若生成失败，头文件内手写约 45 个函数指针的最小加载器（wglGetProcAddress + GetProcAddress） |
| **FreeType（可选，Linux 必需）** | 唯一第三方字体库；矢量字形轮廓（FT_Outline）与 GDI 的 GGO_NATIVE 同为"定点坐标轮廓"，天然适配统一抽象；Linux 上经包管理器安装（`apt install libfreetype-dev`），Windows 上由 fetch_deps.ps1 从 msys2 仓库获取（已验证可达） |
| **GDI（Windows 默认字体后端）** | 操作系统自带库（非第三方），零额外下载；`GetGlyphOutlineW` 直接产出矢量轮廓，与 FreeType 后端共用归一化/缓存/渲染代码 |
| 标准库 | 容器、字符串、文件读取、数学（手写 3x3 矩阵，不引入 glm） |

**不引入**：FreeType 的 C++ 绑定库（FreeType 官方即 C API，以 C++ RAII 封装，避免多一层依赖）；
stb_image 等图像库（BMP 解码纯标准库实现，量小可控）；glm（3x3 矩阵手写 30 行内完成）。

### 后端选择机制（编译期宏）

```cpp
#if defined(WBWOPENGAL_API_FONT_FREETYPE)
    // FreeType（Windows / Linux 均可）
#elif defined(_WIN32)
    // GDI（Windows 默认，零第三方依赖）
#else
#error "wbwopenglapi: 在非 Windows 平台必须定义 WBWOPENGAL_API_FONT_FREETYPE"
#endif
```

## 关键技术决策与理由

1. **GL 3.3 core**：主流 Windows/Linux 驱动全覆盖；core profile 合规（stroke 用 CPU 生成
   粗线三角带，不使用已废弃的 glLineWidth）。启动时校验版本，不满足给出明确报错。
2. **Canvas 坐标系**：左上原点、y 向下（与 Canvas 2D 一致）；正交投影翻转 y；
   投影矩阵按 framebuffer 像素尺寸建立（HiDPI 正确）。
3. **路径 fill = stencil even-odd 两遍法**：轮廓三角形扇写 stencil（GL_INVERT），
   全屏 quad 经 stencil 测试着色。对凹/自交/多环路径健壮，实现简单（约 40 行）。
4. **路径 stroke = CPU 粗线**：按线宽生成三角形带（butt 端点 + miter 连接，miter 限制）
   顶点序列，一次 draw；线宽任意非负，无 GL 状态依赖。
5. **矢量文本 = 轮廓路径复用**：两字体后端都输出 `MoveTo/LineTo/QuadraticTo/CubicTo`
   命令序列（1/64 定点换算像素 + y 翻转），送入与用户路径相同的 fill/stroke 管线，
   天然支持 globalAlpha、变换、strokeText。Glyph 轮廓按 (size, codepoint) 缓存。
6. **变换矩阵栈**：手写 3x3 仿射矩阵（xy 平移 + 旋转缩放），save/restore 栈同时保存
   变换与全部样式状态（与 Canvas 语义一致）。
7. **混合与 alpha**：GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA；globalAlpha 以 uniform 乘入
   片元 alpha。clearRect = scissor + 透明清屏。
8. **资源管理**：Window/GLProgram/GLBuffer/GLVertexArray/GLTexture 全部 RAII，
   析构释放；着色器源码内嵌头文件；每 draw 调用上传顶点（GL_DYNAMIC_DRAW），
   正确性优先，批处理列为后续扩展。
9. **多通道绘制**：solid 通道（颜色 uniform）与 texture 通道（文本/图像纹理）两个 GLSL 程序。

## 注意事项

### 版本兼容
- C++17（g++ 8.1+ 已支持）；GLSL 330。
- GLFW 3.3+（预编译 3.4）；GLAD 生成 gl 3.3 core。
- FreeType 2.10+（msys2 包 / 发行版默认均满足）。

### 跨平台
- Windows：GLFW 用第三方 libglfw3dll.a + 运行时拷贝 glfw3.dll（build.ps1 自动处理）。
- Linux：CMake 走 `find_package(glfw3 / Freetype / OpenGL)`；README 给出 apt 安装命令；
  X11 链接由 find_package 自动处理。
- GDI 后端仅在 `_WIN32` 且未定义 FreeType 宏时启用；Linux 强制 FreeType（#error 保护）。
- Linux 字体家族名→路径查找表覆盖常见发行版路径（DejaVu/Noto/文泉驿），支持绝对路径直载。

### 后续扩展方向
- 顶点批处理（单 buffer 合并多个图元）提升性能
- 用户图片格式扩展（PNG/JPEG 需第三方解码器，属可选依赖）
- 事件回调注册（glfwSetXxxCallback 透传）
- 渐变（CanvasGradient）与 shadow 属性
- 内嵌矢量字体兜底（Linux 无系统字体时）

## 待解决问题（随阶段更新）

- [x] GLAD 生成服务的 POST API 实测（阶段 2）：成功。端点 gen.glad.sh/generate，
      字段 generator=c/specification=gl/api=gl=3.3/profile=gl=core/language=c/extensions=none/output=glad.zip；
      302 跟随须自动转 GET（禁用 -X POST，否则 405）。
- [x] msys2 镜像下载 FreeType 包 + zstd 解压链路实测（阶段 2）：成功（2.14.3-1）。
- [x] 本机显卡 GL 3.3 支持实测（阶段 2）：成功（Intel UHD 630，GL 3.3.0）。
- [ ] Linux 全流程需真实 Linux 环境验证（本机仅能验证 FreeType 后端的 MinGW 编译路径）

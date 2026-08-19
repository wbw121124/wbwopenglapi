# plan.md — 直线粗细不均（11 default 矢量描边）修复计划

## 任务
11 里 `ctx.lineAlgorithm("default")` 的矢量描边线（(60,80)-(660,230) lw4）视觉粗细不均（两头粗中间细）。
12 的 ssaa 也存在 FBO 内容偏窄疑点（x=400 仅 4 行）。每完成一小步：更新本文件 + git commit + git push。

## 步骤
- [x] 11 增加 saveBmp 输出，量化线宽
- [x] 修复 saveBmp 的 y 方向颠倒（11 + 12），重新生成 BMP 验证方向
- [x] 正确方向下量化 default 线宽分布，定位粗细分段
- [x] 修复矢量描边（buildStrokeStrip/drawSolid/投影）根因：三角形分解改为 {al,ar,bl, ar,bl,br}
- [x] 回归 11/12 全绿（09/10 重编译重跑确认全绿）
- [x] 拆除 12 调试块（present/drawSolid DEBUG + 12 cpp m==0 块）
- [x] build/ DLL 复制到 test/
- [x] 最终全量回归 + commit + push

## 诊断记录
- 11_lines.bmp x 列红行分布异常（x=120: 114,164,334,503-506…）→ 发现 BMP 上下颠倒
- saveBmp: glReadPixels 行 0 是 GL 顶行，BMP 行 0 也是顶 → canvas y 与 BMP 行映射为 fh-1-y，图像颠倒
- 12_antialias_off.bmp 实测 (310,400)=(60,76,231) 红、(310,200)=背景，与 canvas 坐标 (310,183) 线位置差 217px → 同样颠倒
- **根因定位（buildStrokeStrip triangle 分解错误）**：out 顺序 {al,ar,bl, al,bl,br}，两三角形共享边 al-bl（左上→右下对角线），右下边 ar-br 附近带无三角形覆盖 → 平行四边形中段只被盖 ~2.3px（应 4px）→ 「两头粗中间细」
  - 手算验证 x=300（t=0.4008）：平行四边形覆盖 NDC y∈[0.5265,0.5396]=3.9px；原分解并集仅 [0.5265,0.5342]=2.3px，缺 [0.5342,0.5396]
  - 修复：{al,ar,bl, ar,bl,br}（共享对角线 ar-bl），并集=完整平行四边形
- 11_lines.bmp 量化（方向修复后）：A 线 x=60-88 段 4 行 → x≈342-377 段仅 2 行 → x=526+ 又 4 行；lw4 理论恒 4.12px
- **修复验证**：buildStrokeStrip 改为 {al,ar,bl, ar,bl,br} 后——
  - 11 A 线 x=60..659 全部恒定 4 行（0 个 2/3 行段）
  - 12 off 斜线 x=90..350 全部恒定 3 行
  - ssaa FBO 竖切 x=400: y906-912 七行完整（修复前仅 906-909 四行）；proj 复核恒为 0.0025/-0.0033（此前疑云排除）
  - 11/12 exit=0 全绿
- **收尾**：调试块全部拆除（hpp present/drawSolid DEBUG、12 cpp m==0 块）；全量回归 09/10/11/12 均 exit=0，stderr 零调试输出；5 张 BMP + 11_lines.bmp 正常；build/ 5 个 DLL 复制到 test/
# Node-API 支持（方案 A：N-API 原生插件，napi/ 独立 npm 包）

## 目标
- napi/ 独立包：node-addon-api（N-API 8，binary.napi_versions=[8]，兼容 Node>=16.11 LTS），完整 Canvas 2D API 映射到 JS
- 头文件既有意图：visible=false 隐藏窗口「供 Node.js 绑定使用」；零侵入：不改 include/wbwopenglapi.hpp、根 CMakeLists.txt、build.ps1、examples
- 构建链：cmake-js 7.4 + cmake-runtime（预编译 cmake 4.3，免系统安装）+ MinGW Makefiles + node-addon-api 8.9.2

## 排障记录
- 本机无 cmake/无 MSVC；cmake-runtime 包提供真实 cmake.exe（node_modules/cmake-runtime-win32-x64/bin）
- Node 24 spawn 不再解析 .cmd：cmake-js 裸 'cmake' 必 ENOENT → scripts/build.cjs 解析绝对路径经 -c 传入
- cmake-js 7.x 不用 find_package(Node)；注入变量 CMAKE_JS_INC/CMAKE_JS_LIB/CMAKE_JS_SRC/CMAKE_JS_NODELIB_DEF/TARGET（MinGW 用 dlltool 从 node_api.def 生成 node.lib）
- cmake-js Windows 无条件注入 MSVC 专用 /DELAYLOAD:NODE.EXE → 非 MSVC 剥离
- MinGW-W64 8.1 无 winpthreads，std::mutex 不存在 → add_compile_definitions NAPI_HAS_THREADS=0（官方开关，单线程同步渲染无影响）
- 帧协议：Canvas 绘制全部经离屏 FBO（off 模式 1x），读回默认 framebuffer 前必须 ctx.resolve()（=present()），与示例 12 协议一致
- glReadPixels y 原点在左下：canvas 坐标（y 向下）读回须翻转 fh-1-y（同示例 12 readPx）
- 本机隐藏窗口 framebuffer 非对称缩放：请求 64x64 得 120x64（宽 1.875x 高 1x，显示环境 DPI 所致）；绘制/读回按 framebuffer 尺寸自洽，Node API 暴露 Canvas 语义即可

## 阶段
- [x] 阶段1/4：napi/ 包骨架 + 构建链打通（最小 addon 编译、加载、隐藏窗口 clear/fillRect/resolve 读回像素验证通过）
- [x] 阶段2/4：完整 C++ API 映射（renderer.h / renderer_wrap / bmp / addon 注册），冒烟验证通过：矩形/路径/文本/图像/变换/BMP
- [x] 阶段3/4：JS 包装层（lib/index.js 方法绑定）+ smoke + node:test 自动化测试（npm test 10 项全绿）
- [x] 阶段4/4：C++ 原生全量回归（build.ps1 全量编译 + 09-12 -t 全绿；01/08 为交互 demo 无 -t 模式）+ README Node.js 章节 + 最终提交

# DirectWrite + Skia 支持（Windows 文本后端增强 + 可选 2D 图形后端）

## 目标
- DirectWrite：Windows 第三个矢量文本后端（与 GDI/FreeType 并列，**集成不替换**）。
  宏 `WBWOPENGAL_API_FONT_DWRITE`；build.ps1 `-Backend dwrite`；根 CMake option `WBWOPENGAL_API_FONT_DWRITE`
- Skia：可选 2D 图形后端（软件栅格化 Surface → RGBA → 现有管线 drawImage 合成，集成不替换）。
  vcpkg 引入（`skia` 端口），CMake option `WBWOPENGAL_API_SKIA` 默认 OFF；封装层 `include/wbwopenglapi_skia.hpp`
- 兼容性：Windows 8.1+（DWrite 1.x + `IDWriteGeometrySink`，无 d3d 依赖）；不破坏已勾选功能与
  GDI/FreeType/HarfBuzz 路径

## 现状分析（2026-08-19）
- plan.md 现有两章（线宽修复、Node-API 四阶段）均已完成；无"惰性/暂缓"标记条目
- 渲染管线：include/wbwopenglapi.hpp（2397 行 header-only）——`detail::FontFace`（行 469，
  GDI/FreeType 双后端 `#ifdef WBWOPENGAL_API_FONT_FREETYPE` 切换），字形空间 1/64 像素、y 向上、
  基线 y=0（行 420）；`Canvas`（行 1198）fillText/strokeText/measureText；GL 3.3 core 管线
- 构建系统：build.ps1（MinGW g++ 8.1、-Backend gdi|freetype、-HarfBuzz）、根 CMakeLists.txt
  （CMake 3.16、option FREETYPE/HARFBUZZ、examples glob）、napi/（cmake-js 7.4 + cmake-runtime）、
  .github/workflows/release.yml（7 平台矩阵）；语言仅 C++17
- **最小链路验证（本步）**：MinGW-w64 8.1 自带 dwrite.h/dwrite_2.h/d2d1_1.h 与 libdwrite.a；
  dt_smoke（工厂/系统字体/GetGlyphIndices/GetGlyphRunOutline/自定义 sink）编译链接运行 exit=0
- 缺失信息与默认假设：
  - Skia 用途 → 软件栅格化（RasterSurface）Surface → RGBA → `wbwopenglapi::Image` →
    `ctx.drawImage` 合成（零 GPU 上下文丢失风险，headless/CI 可跑）
  - DWrite 度量语义 → 与 FreeType 对齐：ascender 正/descender 负、1/64 像素
  - DPI → 字形空间为逻辑像素（与 DPI 无关）；HiDPI 由 Canvas framebuffer 层适配（既有机制）

## 技术方案
- **DirectWrite**：头文件方式（系统头）+ 链接 `-ldwrite`（MinGW 自带 import lib）。
  接入点 `detail::FontFace` 加第三个后端分支（`#elif defined(WBWOPENGAL_API_FONT_DWRITE)`）：
  - 字体：file 空 → 系统字体集合 Segoe UI/Arial/Microsoft YaHei UI；非空 → CreateFontFileReference + CreateFontFace
  - 字形：GetGlyphIndices → GetGlyphRunOutline（emSize=sizePx → 像素单位，收集时 ×64 对齐 1/64
    字形空间；y 轴 DWrite 向下 → 取反为向上）
  - advance：GetDesignGlyphAdvances × sizePx/upem×64；升/降：DWRITE_FONT_METRICS × sizePx/upem×64
  - shape()/fontFeatures 不支持（同 GDI 语义）；缺字形返回空轮廓（同 GDI）
  - 仅含 d2d1_1.h 的 IDWriteGeometrySink 接口定义（Win8.1 起 IDWriteGeometrySink =
    ID2D1SimplifiedGeometrySink 别名），sink 由本库实现，**不链接 d2d1/d3d11**
- **Skia 引入：vcpkg**（唯一可维护路径）——
  - 预编译库：Google 无官方 Windows 预编译发布（第三方 ABI 风险）✗
  - 源码子模块：需 depot_tools/GN/ninja 新构建工具 + 1GB+ 下载（违约束、网络受限）✗
  - vcpkg：与 CMake 原生一体（find_package(skia CONFIG)），MSVC/Clang 官方支持 ✓
    **代价**：MinGW 8.1 无法构建 skia（需 MSVC）→ 实际编译验证放 GitHub Actions
    （windows-msvc + vcpkg 并行安装），本机仅配置层/无 Skia 时的编译隔离/文档验证
- **Skia 后端：RasterSurface（软件）**——对接现有 GL 管线零风险（无 GrDirectContext/GL 上下文
  丢失问题）、headless 可跑；D3D11 GPU 后端需全新纹理/交换链管线，与"集成不替换"不符
- **工具链约束**：不引入现有构建方式之外的新工具；vcpkg 仅为 Skia 可选构建的依赖获取器，
  默认构建链（build.ps1 / cmake 无 vcpkg）不变

## 步骤（每步：更新本文 → git add（仅该步文件）→ commit → push）
- [x] 步 0/4：现状分析 + 技术方案 + 最小链路验证（dt_smoke：MinGW dwrite 头/链接/几何 sink exit=0）
- [x] 步 1/4：构建配置：build.ps1 `-Backend dwrite`（`-ldwrite`）；根 CMakeLists
      `WBWOPENGAL_API_FONT_DWRITE` option（Windows-only，与 FreeType 互斥）
      → 验证：dwrite 后端编译 05_text 通过；09_text_lines -t 回归 7 项全绿
      （本步宏未接入头文件前回落 GDI 路径，故过渡期保留 gdi32 链接）
- [x] 步 2/4：DirectWrite 封装层：FontFace 第三后端分支（工厂/系统字体默认序列/文件字体/
      度量/字形轮廓收集器 DwOutlineSink/advance）+ 字形回退链
      → 验证：三个后端全量编译通过；09-12 -t 回归 dwrite/gdi/freetype 全绿
      排障：① dwrite.h 中 GetGlyphRunOutline 为 8 参版本（glyphCount 独立传）；
            GetGlyphIndices 直接接受 UCS-4 码点 ② ID2D1SimplifiedGeometrySink::Close
            返回 HRESULT ③ Segoe UI 无 CJK 字形且 DWrite 单字体不回退 → 实现
            主字体+双回退链（Segoe UI -> Arial -> 微软雅黑）
- [x] 步 3/4：整合 + 示例 13_dwrite_text（仿 05_text，含 -t 回归；无 DWrite 宏时
      等同 05 可在三后端编译）+ README 构建章节/示例表/技术要点更新
      → 验证：13 在 dwrite/gdi/freetype 三后端 -t 全绿；09-12 dwrite/gdi/freetype 回归全绿
      排障：build 目录 glfw3.dll 曾被 12_antialias 残留进程占用（进程退出后自动恢复）
- [x] 步 4/4：Skia：vcpkg.json manifest（仅 skia）+ `WBWOPENGAL_API_SKIA` option
      （find_package(skia CONFIG) REQUIRED）+ include/wbwopenglapi_skia.hpp
      （RasterSurface 封装：矩形/圆/路径/文本/变换/toRGBA top-down）
      + 示例 14_skia（headless 像素校验 + BMP 导出正确行序范式）
      + .github/workflows/skia-ci.yml（windows-msvc + vcpkg, push main 触发）
      → 本机验证：无 skia 时 build.ps1 全量跳过 14 不受影响；-Example 14_skia
      给出指引错误；CMake GLOB 排除 14 后三后端正常；SKIA=ON 无 vcpkg 时
      find_package 正确 FATAL
      遗留风险：wbwopenglapi_skia.hpp/14_skia.cpp 的真实编译与像素校验依赖
      skia-ci.yml 的 MSVC+vcpkg job（首次安装 skia 端口约 15-30 分钟），
      本机 MinGW 8.1 无法验证

# 修复任务：BMP 导出方向颠倒

## 目标
- 修复 12_antialias 示例 test/*.bmp 导出上下颠倒（GL 与 BMP 同为自下而上，
  旧实现画蛇添足再翻转一次）
- 排查并修复 napi/ 的 toBMP（如存在同类问题）
- 正确范式已由 14_skia.cpp 的 saveBmp 确立（内存 top-down -> BMP 自下而上翻转）

## 步骤
- [x] 修复 12_antialias.cpp saveBmp：glReadPixels 与 BMP 同为自下而上（行 0 = 图像
      底部），旧代码 (fh-1-y) 画蛇添足翻转导致导出上下颠倒 → 逐行直拷
- [x] 修复 napi/src/bmp.cc encodeBmp 同类问题（bmp.cc:70 同样多翻一次）
      → node 像素验证：顶部红色/底部背景正确
- [x] 测试增强：napi/test/basic.test.mjs toBMP 增加行序断言（按 BMP 头实际尺寸
      解析，兼容 HiDPI framebuffer 缩放）；npm test 10 项全绿 + smoke 通过
- [x] 验证：12_antialias -t 全绿；test/*.bmp 像素抽查（圆心绿/斜线红/顶部背景）
      符合 top-down 语义
      排障：BMP 解析曾用 PowerShell 浮点除法算 stride 误读错位（文件 stride=2400
      本无填充）；BMP 头尺寸 120x16 = HiDPI framebuffer（画布 16x16 逻辑），
      测试按头解析

## 排障记录
（按步追加）

#pragma once
//
// wbwopenglapi - header-only C++17 库
// 目标: 让 C++ 的 OpenGL 编程体验接近 JavaScript Canvas 2D API
//
// 依赖: GLFW 3.3+ / GLAD (gl:core=3.3) / (可选) FreeType / (可选) HarfBuzz / (可选) DirectWrite
//   字体后端选择:
//     - 定义宏 WBWOPENGAL_API_FONT_FREETYPE: 使用 FreeType (Linux 必需, Windows 可选)
//     - 定义宏 WBWOPENGAL_API_FONT_DWRITE (仅 Windows 8.1+): 使用 DirectWrite 系统库
//     - 未定义且 _WIN32: 使用系统 GDI 矢量字体 (Windows 默认)
//     - 未定义且非 Windows: 编译错误
//   OpenType 特性/连体（fontFeatures + HarfBuzz 整形）:
//     - 需同时定义 WBWOPENGAL_API_FONT_FREETYPE 与 WBWOPENGAL_API_FONT_HARFBUZZ
//     - GDI 后端不支持 GSUB 特性（fontFeatures 惰性，仅 FreeType+HarfBuzz 生效）
//     - DirectWrite 后端不支持 GSUB 特性（同 GDI 语义）
//
// 使用示例:
//   wbwopenglapi::Window win(800, 600, "Demo");
//   wbwopenglapi::Canvas ctx(win);
//   while (!win.shouldClose()) {
//       ctx.clear("#f5f5f5");
//       ctx.fillStyle("#ff8000");
//       ctx.fillRect(50, 50, 200, 150);
//       win.pollEvents();
//       win.swapBuffers();
//   }

// 顺序要求: 先包含 GLAD (gl.h)，再包含 GLFW；GLFW_INCLUDE_NONE 防止 glfw3.h
// 引入系统 GL/gl.h 与 GLAD 冲突
//
// 字体后端平台头文件（须在 GL 头之前，避免 windows.h 与 GL 宏冲突）
#if defined(WBWOPENGAL_API_FONT_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftoutln.h> // FT_Outline_Decompose（freetype.h 不自动包含）
#if defined(WBWOPENGAL_API_FONT_HARFBUZZ)
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>
#endif
#elif defined(WBWOPENGAL_API_FONT_DWRITE) && defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// DirectWrite 字体后端（Windows 8.1+ 系统库，链接 -ldwrite）:
// IDWriteGeometrySink 自 Win8.1 起为 ID2D1SimplifiedGeometrySink 的别名（见 dwrite.h），
// 此处仅引入 d2d1_1.h 取接口定义；几何 sink 由本库实现，不链接 d2d1/d3d11
#include <dwrite.h>
#include <d2d1_1.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#error "wbwopenglapi: 非 Windows 平台必须定义 WBWOPENGAL_API_FONT_FREETYPE"
#endif

#define GLFW_INCLUDE_NONE
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// PNG 读写（WBWOPENGAL_API_PNG）依赖系统 zlib；须在命名空间外包含（zlib.h 为 C 头）
#if defined(WBWOPENGAL_API_PNG)
#include <zlib.h>
#endif

// 配套头文件：
//   lines.hpp - 像素级直线光栅化算法（DDA / Bresenham / Wu，无 GL 依赖）
//   aa.hpp    - 抗锯齿离屏 FBO RAII 与 FXAA/MLAA 片元着色器
#include "wbwopenglapi_lines.hpp"
#include "wbwopenglapi_aa.hpp"

namespace wbwopenglapi {

namespace detail {

// =====================================================================
// GL 资源 RAII 封装
// =====================================================================

// 编译着色器，失败抛出带日志的异常
inline GLuint compileShader(GLenum type, const char* src) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(id, sizeof(log), nullptr, log);
        glDeleteShader(id);
        throw std::runtime_error(std::string("wbwopenglapi: 着色器编译失败: ") + log);
    }
    return id;
}

class Shader {
public:
    Shader(GLenum type, const char* src) : id_(compileShader(type, src)) {}
    ~Shader() {
        if (id_) {
            glDeleteShader(id_);
        }
    }
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
};

class Program {
public:
    Program(const char* vsSrc, const char* fsSrc) {
        Shader vs(GL_VERTEX_SHADER, vsSrc);
        Shader fs(GL_FRAGMENT_SHADER, fsSrc);
        id_ = glCreateProgram();
        glAttachShader(id_, vs.id());
        glAttachShader(id_, fs.id());
        glLinkProgram(id_);
        GLint ok = 0;
        glGetProgramiv(id_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
            glDeleteProgram(id_);
            id_ = 0;
            throw std::runtime_error(std::string("wbwopenglapi: 着色器链接失败: ") + log);
        }
    }
    ~Program() {
        if (id_) {
            glDeleteProgram(id_);
        }
    }
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    void use() const { glUseProgram(id_); }
    GLint uniform(const char* name) const { return glGetUniformLocation(id_, name); }

private:
    GLuint id_ = 0;
};

class VertexArray {
public:
    VertexArray() { glGenVertexArrays(1, &id_); }
    ~VertexArray() {
        if (id_) {
            glDeleteVertexArrays(1, &id_);
        }
    }
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    void bind() const { glBindVertexArray(id_); }

private:
    GLuint id_ = 0;
};

class VertexBuffer {
public:
    VertexBuffer() { glGenBuffers(1, &id_); }
    ~VertexBuffer() {
        if (id_) {
            glDeleteBuffers(1, &id_);
        }
    }
    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;
    void bind() const { glBindBuffer(GL_ARRAY_BUFFER, id_); }
    // 上传顶点数据：首次分配，之后增量更新
    // （重复 glBufferData 重新分配在部分驱动上会丢失后续绘制状态）
    void upload(const void* data, GLsizeiptr bytes) {
        bind();
        if (bytes > capacity_) {
            glBufferData(GL_ARRAY_BUFFER, bytes, data, GL_DYNAMIC_DRAW);
            capacity_ = bytes;
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, data);
        }
    }

private:
    GLuint id_ = 0;
    GLsizeiptr capacity_ = 0;
};

class Texture {
public:
    Texture() { glGenTextures(1, &id_); }
    ~Texture() {
        if (id_) {
            glDeleteTextures(1, &id_);
        }
    }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    void bind() const { glBindTexture(GL_TEXTURE_2D, id_); }
    // 上传 RGBA8 图像（行序自上而下；GL 纹素原点在左下，采样时 v 翻转）
    void upload(int w, int h, const uint8_t* px) {
        bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

private:
    GLuint id_ = 0;
};

// =====================================================================
// 2D 几何工具
// =====================================================================

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

// 将折线展开为有宽度的三角带顶点序列（GL_TRIANGLES）
//   - 端点: butt（平头，直接法线偏移）
//   - 连接: miter（角平分线交点），超出 miterLimit 时退化为 bevel
//   - closed: 首尾相连
inline std::vector<Vec2> buildStrokeStrip(const std::vector<Vec2>& pts, bool closed,
                                          float width) {
    std::vector<Vec2> out;
    const size_t n = pts.size();
    if (n < 2 || width <= 0.0f) {
        return out;
    }
    const float half = width * 0.5f;
    const float miterLimit = 10.0f;

    struct Seg {
        Vec2 dir;
        Vec2 n; // 左侧法线
    };
    std::vector<Seg> segs;
    const size_t segCount = closed ? n : n - 1;
    segs.reserve(segCount);
    for (size_t i = 0; i < segCount; ++i) {
        const Vec2& a = pts[i % n];
        const Vec2& b = pts[(i + 1) % n];
        Vec2 d{b.x - a.x, b.y - a.y};
        float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 1e-6f) {
            d = {1.0f, 0.0f};
        } else {
            d = {d.x / len, d.y / len};
        }
        segs.push_back({d, {-d.y, d.x}});
    }

    // 求点 i 处某一侧的偏移量（向量），首尾端点退化为 butt
    auto offsetAt = [&](size_t i, float side) -> Vec2 {
        // side: +1 左侧, -1 右侧
        if (segCount == 1) {
            const Vec2& n = segs[0].n;
            return {n.x * side * half, n.y * side * half};
        }
        const bool isEnd = !closed && (i == 0 || i == n - 1);
        if (isEnd) {
            const Vec2& n = segs[i == 0 ? 0 : segCount - 1].n;
            return {n.x * side * half, n.y * side * half};
        }
        const Seg& in = segs[(i + segCount - 1) % segCount];
        const Seg& out = segs[i % segCount];
        Vec2 n1{in.n.x * side, in.n.y * side};
        Vec2 n2{out.n.x * side, out.n.y * side};
        Vec2 m{n1.x + n2.x, n1.y + n2.y};
        float ml = std::sqrt(m.x * m.x + m.y * m.y);
        if (ml < 1e-6f) {
            return {n1.x * half, n1.y * half}; // 180° 折返
        }
        m = {m.x / ml, m.y / ml};
        float d = m.x * n1.x + m.y * n1.y; // dot(m, n1)
        if (d <= 0.0f) {
            return {n1.x * half, n1.y * half}; // 尖锐内角，退化为平头
        }
        float scale = half / d;
        if (scale > half * miterLimit) {
            return {n1.x * half, n1.y * half}; // 超出 miterLimit，bevel 退化
        }
        return {m.x * scale, m.y * scale};
    };

    out.reserve(segCount * 6);
    for (size_t i = 0; i < segCount; ++i) {
        const Vec2& a = pts[i % n];
        const Vec2& b = pts[(i + 1) % n];
        Vec2 oaL = offsetAt(i % n, +1.0f);
        Vec2 oaR = offsetAt(i % n, -1.0f);
        Vec2 obL = offsetAt((i + 1) % n, +1.0f);
        Vec2 obR = offsetAt((i + 1) % n, -1.0f);
        Vec2 al{a.x + oaL.x, a.y + oaL.y};
        Vec2 ar{a.x + oaR.x, a.y + oaR.y};
        Vec2 bl{b.x + obL.x, b.y + obL.y};
        Vec2 br{b.x + obR.x, b.y + obR.y};
        out.push_back(al);
        out.push_back(ar);
        out.push_back(bl);
        out.push_back(ar);
        out.push_back(bl);
        out.push_back(br);
    }
    return out;
}

// =====================================================================
// 路径：命令序列 -> 子路径折线（贝塞尔 de Casteljau 细分）
// =====================================================================

inline constexpr double kPi = 3.14159265358979323846;

enum class PathCmd { MoveTo, LineTo, QuadraticTo, CubicTo, Close };

struct PathSeg {
    PathCmd cmd;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    float x3 = 0.0f, y3 = 0.0f;
};

struct SubPath {
    std::vector<Vec2> points;
    bool closed = false;
};

// de Casteljau 递归细分（depth 级），追加折线点（不含首点）
inline void splitQuad(const Vec2& p0, const Vec2& p1, const Vec2& p2,
                      std::vector<Vec2>& out, int depth) {
    if (depth <= 0) {
        out.push_back(p2);
        return;
    }
    Vec2 a{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
    Vec2 b{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
    Vec2 m{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    splitQuad(p0, a, m, out, depth - 1);
    splitQuad(m, b, p2, out, depth - 1);
}

inline void splitCubic(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3,
                       std::vector<Vec2>& out, int depth) {
    if (depth <= 0) {
        out.push_back(p3);
        return;
    }
    Vec2 a{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
    Vec2 b{(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f};
    Vec2 c{(p2.x + p3.x) * 0.5f, (p2.y + p3.y) * 0.5f};
    Vec2 ab{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    Vec2 bc{(b.x + c.x) * 0.5f, (b.y + c.y) * 0.5f};
    Vec2 m{(ab.x + bc.x) * 0.5f, (ab.y + bc.y) * 0.5f};
    splitCubic(p0, a, ab, m, out, depth - 1);
    splitCubic(m, bc, c, p3, out, depth - 1);
}

// 把命令序列展开为子路径折线（fill/stroke 共用）
inline std::vector<SubPath> flattenPath(const std::vector<PathSeg>& segs) {
    std::vector<SubPath> subs;
    SubPath cur;
    auto commit = [&]() {
        if (!cur.points.empty()) {
            subs.push_back(cur);
        }
        cur = SubPath{};
    };
    for (const PathSeg& s : segs) {
        switch (s.cmd) {
        case PathCmd::MoveTo: {
            commit();
            cur.points.push_back({s.x1, s.y1});
            break;
        }
        case PathCmd::LineTo: {
            if (cur.points.empty()) {
                cur.points.push_back({s.x1, s.y1});
            }
            cur.points.push_back({s.x1, s.y1});
            break;
        }
        case PathCmd::QuadraticTo: {
            if (cur.points.empty()) {
                cur.points.push_back({s.x1, s.y1});
            }
            const Vec2 p0 = cur.points.back();
            splitQuad(p0, {s.x1, s.y1}, {s.x2, s.y2}, cur.points, 6);
            break;
        }
        case PathCmd::CubicTo: {
            if (cur.points.empty()) {
                cur.points.push_back({s.x1, s.y1});
            }
            const Vec2 p0 = cur.points.back();
            splitCubic(p0, {s.x1, s.y1}, {s.x2, s.y2}, {s.x3, s.y3}, cur.points, 6);
            break;
        }
        case PathCmd::Close: {
            if (!cur.points.empty()) {
                cur.closed = true;
                commit();
            }
            break;
        }
        }
    }
    commit();
    return subs;
}

// =====================================================================
// 文本：字形轮廓（统一抽象，双后端输出同构 PathSeg 序列）
// 字形空间坐标: 1/64 像素, y 轴向上, 基线 y=0（画布坐标 = (tx + px/64, ty - py/64)）
// =====================================================================

struct Glyph {
    std::vector<PathSeg> outline; // 字形轮廓（MoveTo 分隔轮廓环）
    double advanceX = 0.0;        // 前进宽度（1/64 像素）
};

// 整形后的字形放置（HarfBuzz 输出；单位 1/64 像素）
struct ShapedGlyph {
    uint32_t index = 0;  // 字形索引（HarfBuzz 路径）；回退路径不用
    double advanceX = 0.0;
    double dx = 0.0;     // GPOS 横向偏移
    double dy = 0.0;     // GPOS 纵向偏移（字形空间 y 向上）
};

// UTF-8 解码：从 s[i] 处取一个码点，i 前移到下一个字符
inline uint32_t utf8Next(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        i += 1;
        return c;
    }
    int n = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
        n = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3;
        cp = c & 0x07;
    } else {
        i += 1;
        return 0xFFFD;
    }
    if (i + 1 + n > s.size()) {
        i += 1;
        return 0xFFFD;
    }
    for (int k = 0; k < n; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + 1 + k]) & 0x3F);
    }
    i += 1 + n;
    return cp;
}

class FontFace {
public:
    explicit FontFace(int sizePx) : FontFace("", sizePx) {}
    // file 为空时使用默认字体（GDI/DirectWrite 用系统默认字体序列；FreeType 用内置字体目录）
    FontFace(const std::string& file, int sizePx) { init(file, sizePx); }
    ~FontFace() { destroy(); }
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    int sizePx() const { return sizePx_; }
    // 上升/下降（1/64 像素；descender 为负）
    double ascender() const { return ascender_; }
    double descender() const { return descender_; }

    // 加载字形轮廓（缺字形返回空 outline）
    Glyph loadGlyph(uint32_t cp) const;
    // 按字形索引加载（连体等无码点映射的字形；GDI/DirectWrite 后端恒返回空）
    Glyph loadGlyphIndex(uint32_t idx) const;
    // HarfBuzz 整形（仅 FreeType+HarfBuzz 编译配置；否则返回空）
    std::vector<ShapedGlyph> shape(
        const std::string& text,
        const std::vector<std::pair<std::string, bool>>& feats) const;

private:
    void init(const std::string& file, int sizePx);
    void destroy();

#ifdef WBWOPENGAL_API_FONT_FREETYPE
    FT_Library lib_ = nullptr;
    FT_Face face_ = nullptr;
#if defined(WBWOPENGAL_API_FONT_HARFBUZZ)
    hb_font_t* hbFont_ = nullptr;
#endif
#elif defined(WBWOPENGAL_API_FONT_DWRITE)
    IDWriteFactory* dwFact_ = nullptr;
    IDWriteFontFace* dwFace_ = nullptr;
    // 字形回退链（Segoe UI -> Arial -> 微软雅黑），主字体缺字形时顺次尝试
    IDWriteFontFace* dwFallbacks_[2] = {nullptr, nullptr};
    // 设计单位/em（getDesignGlyphMetrics 输出的设计单位 -> 1/64 像素的换算基准）
    double upem_ = 64.0;
#else
    HDC dc_ = nullptr;
    HFONT font_ = nullptr;
#endif
    int sizePx_ = 16;
    double ascender_ = 0.0;
    double descender_ = 0.0;
};

} // namespace detail

// =====================================================================
// Color - RGBA 颜色 (0..1)，支持 CSS 字符串解析
// =====================================================================
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}
};

// Canvas 2D 渐变（createLinearGradient / createRadialGradient 的返回值）。
// 渐变坐标位于用户空间（绘制时受当前变换影响）；stops 按 offset 升序。
// Canvas 语义：addColorStop 的 offset 不在 [0,1] 时忽略。
inline Color parseColor(const std::string& css);
struct Gradient {
    bool radial = false;        // false=线性(x0,y0)->(x1,y1)；true=径向两圆焦点
    double x0 = 0, y0 = 0, r0 = 0; // 起点（线性）/内圆（径向）
    double x1 = 0, y1 = 0, r1 = 0; // 终点（线性）/外圆（径向）
    std::vector<std::pair<double, Color>> stops; // (offset, color) 升序，最多 8 个

    void addColorStop(double offset, const Color& c) {
        if (offset < 0.0 || offset > 1.0) {
            return;
        }
        auto it = std::lower_bound(
            stops.begin(), stops.end(), offset,
            [](const std::pair<double, Color>& s, double o) { return s.first < o; });
        stops.insert(it, std::make_pair(offset, c));
        if (stops.size() > 8) {
            stops.resize(8); // shader 数组上限 8（超出丢弃末尾，与浏览器近一致）
        }
    }
    void addColorStop(double offset, const std::string& css) {
        addColorStop(offset, parseColor(css));
    }
};

// =====================================================================
// 文本布局枚举
// =====================================================================
enum class TextAlign { Left, Center, Right };
enum class TextBaseline { Top, Middle, Alphabetic, Bottom };

// 解析 CSS 颜色字符串:
//   "#RGB" "#RRGGBB" "#RRGGBBAA" "rgb(r,g,b)" "rgba(r,g,b,a)" 及常用颜色名
// 解析失败抛出 std::invalid_argument
inline Color parseColor(const std::string& css) {
    auto strip = [](const std::string& s) -> std::string {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) {
            return "";
        }
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };
    const std::string s = strip(css);
    if (s.empty()) {
        throw std::invalid_argument("wbwopenglapi: 空颜色字符串");
    }

    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    if (s[0] == '#') {
        const size_t n = s.size() - 1;
        int vals[4] = {0, 0, 0, 255};
        if (n == 3 || n == 4) {
            for (size_t i = 0; i < n; ++i) {
                int v = hexVal(s[1 + i]);
                if (v < 0) {
                    throw std::invalid_argument("wbwopenglapi: 非法十六进制颜色: " + css);
                }
                vals[i] = v * 17;
            }
        } else if (n == 6 || n == 8) {
            for (size_t i = 0; i < n / 2; ++i) {
                int hi = hexVal(s[1 + 2 * i]);
                int lo = hexVal(s[2 + 2 * i]);
                if (hi < 0 || lo < 0) {
                    throw std::invalid_argument("wbwopenglapi: 非法十六进制颜色: " + css);
                }
                vals[i] = hi * 16 + lo;
            }
        } else {
            throw std::invalid_argument("wbwopenglapi: 非法十六进制颜色: " + css);
        }
        return Color(vals[0] / 255.0f, vals[1] / 255.0f, vals[2] / 255.0f,
                     vals[3] / 255.0f);
    }

    if (s.rfind("rgb", 0) == 0) {
        size_t lp = s.find('(');
        size_t rp = s.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp) {
            throw std::invalid_argument("wbwopenglapi: 非法 rgb 颜色: " + css);
        }
        std::vector<double> nums;
        std::istringstream in(s.substr(lp + 1, rp - lp - 1));
        std::string token;
        while (std::getline(in, token, ',')) {
            token = strip(token);
            if (token.empty()) {
                throw std::invalid_argument("wbwopenglapi: 非法 rgb 颜色: " + css);
            }
            bool pct = token.back() == '%';
            if (pct) {
                token.pop_back();
            }
            char* end = nullptr;
            double v = std::strtod(token.c_str(), &end);
            if (end == token.c_str()) {
                throw std::invalid_argument("wbwopenglapi: 非法 rgb 颜色: " + css);
            }
            if (pct) {
                v = v * 255.0 / 100.0;
            }
            nums.push_back(v);
        }
        const bool hasAlpha = s.rfind("rgba", 0) == 0;
        const size_t need = hasAlpha ? 4 : 3;
        if (nums.size() != need) {
            throw std::invalid_argument("wbwopenglapi: rgb 分量数量错误: " + css);
        }
        auto comp = [&](size_t i) {
            double v = nums[i];
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            return static_cast<float>(v / 255.0);
        };
        if (hasAlpha) {
            double a = nums[3];
            if (a < 0) a = 0;
            if (a > 1) a = 1;
            return Color(comp(0), comp(1), comp(2), static_cast<float>(a));
        }
        return Color(comp(0), comp(1), comp(2), 1.0f);
    }

    // 常用颜色名（大小写不敏感）
    static const std::unordered_map<std::string, Color> named = {
        {"black", {0.0f, 0.0f, 0.0f}},       {"white", {1.0f, 1.0f, 1.0f}},
        {"red", {1.0f, 0.0f, 0.0f}},         {"green", {0.0f, 0.5f, 0.0f}},
        {"lime", {0.0f, 1.0f, 0.0f}},        {"blue", {0.0f, 0.0f, 1.0f}},
        {"yellow", {1.0f, 1.0f, 0.0f}},      {"cyan", {0.0f, 1.0f, 1.0f}},
        {"aqua", {0.0f, 1.0f, 1.0f}},        {"magenta", {1.0f, 0.0f, 1.0f}},
        {"fuchsia", {1.0f, 0.0f, 1.0f}},     {"silver", {0.75f, 0.75f, 0.75f}},
        {"gray", {0.5f, 0.5f, 0.5f}},        {"grey", {0.5f, 0.5f, 0.5f}},
        {"maroon", {0.5f, 0.0f, 0.0f}},      {"olive", {0.5f, 0.5f, 0.0f}},
        {"navy", {0.0f, 0.0f, 0.5f}},        {"teal", {0.0f, 0.5f, 0.5f}},
        {"orange", {1.0f, 0.65f, 0.0f}},     {"purple", {0.5f, 0.0f, 0.5f}},
        {"pink", {1.0f, 0.75f, 0.8f}},       {"brown", {0.65f, 0.16f, 0.16f}},
        {"transparent", {0.0f, 0.0f, 0.0f, 0.0f}},
    };
    std::string low;
    low.reserve(s.size());
    for (char c : s) {
        low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    auto it = named.find(low);
    if (it != named.end()) {
        return it->second;
    }
    throw std::invalid_argument("wbwopenglapi: 无法解析颜色: " + css);
}

// =====================================================================
// Image - 位图图像（RGBA，行序自上而下）与纯标准库 BMP 解码
// =====================================================================
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width * height * 4
};

// 解码未压缩 BMP（24/32 位，BI_RGB）。行序统一转为自上而下。
// 解码失败抛出 std::runtime_error。
inline Image loadBMP(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("wbwopenglapi: 无法打开图像: " + path);
    }
    auto readU16 = [&f]() -> uint16_t {
        uint16_t v = 0;
        for (int i = 0; i < 2; ++i) {
            v |= static_cast<uint16_t>(f.get()) << (8 * i);
        }
        return v;
    };
    auto readU32 = [&f]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<uint32_t>(f.get()) << (8 * i);
        }
        return v;
    };
    auto readS32 = [&f, &readU32]() -> int32_t {
        return static_cast<int32_t>(readU32());
    };

    if (readU16() != 0x4D42) { // "BM"
        throw std::runtime_error("wbwopenglapi: 不是 BMP 文件: " + path);
    }
    readU32();               // bfSize
    readU32();               // bfReserved
    const uint32_t off = readU32(); // bfOffBits
    const uint32_t hdr = readU32(); // biSize
    if (hdr < 40) {
        throw std::runtime_error("wbwopenglapi: 不支持的 BMP 头: " + path);
    }
    const int32_t w = readS32();
    const int32_t hRaw = readS32();
    readU16();               // biPlanes
    const uint16_t bpp = readU16();
    const uint32_t comp = readU32();
    if (w <= 0 || hRaw == 0) {
        throw std::runtime_error("wbwopenglapi: BMP 尺寸非法: " + path);
    }
    if (comp != 0) {
        throw std::runtime_error("wbwopenglapi: 仅支持未压缩(BI_RGB) BMP: " + path);
    }
    if (bpp != 24 && bpp != 32) {
        throw std::runtime_error("wbwopenglapi: 仅支持 24/32 位 BMP: " + path);
    }
    Image img;
    img.width = w;
    img.height = (hRaw < 0) ? -hRaw : hRaw;
    img.rgba.reserve(static_cast<size_t>(img.width) * img.height * 4);
    const int bytesPerPx = bpp / 8;
    const int rowBytes = ((w * bpp + 31) / 32) * 4;
    const bool topDown = hRaw < 0; // 负高度: 行序自上而下
    std::vector<uint8_t> row(rowBytes);
    for (int y = 0; y < img.height; ++y) {
        const int srcY = topDown ? y : img.height - 1 - y;
        f.seekg(static_cast<std::streamoff>(off) + static_cast<long long>(srcY) * rowBytes);
        f.read(reinterpret_cast<char*>(row.data()), rowBytes);
        for (int x = 0; x < img.width; ++x) {
            const uint8_t* p = row.data() + x * bytesPerPx;
            img.rgba.push_back(p[2]); // BGR(A) -> RGBA
            img.rgba.push_back(p[1]);
            img.rgba.push_back(p[0]);
            img.rgba.push_back(bpp == 32 ? p[3] : 255);
        }
    }
    return img;
}

// =====================================================================
// PNG（WBWOPENGAL_API_PNG 启用；依赖系统 zlib：Windows MinGW 自带 libz.a、
// Linux/macOS 系统 libz，零第三方下载。未启用时本段不编译）
//   解码：8-bit 非隔行，colortype 0/2/3/4/6（灰度/真彩/调色板/灰度+alpha/RGBA），
//         含 tRNS 透明色与 5 种滤波行（None/Sub/Up/Average/Paeth）
//   编码：RGBA 8-bit（行 filter 0），zlib deflate
// =====================================================================
#ifdef WBWOPENGAL_API_PNG
namespace detail {

// PNG 标准 CRC32（多项式 0xEDB88320，查表法）
inline uint32_t pngCrc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static const bool ready = [] {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        return true;
    }();
    (void)ready;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

inline uint32_t pngBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
           uint32_t(p[3]);
}

// 追加 PNG 块（length + type + data + crc；length 为 data 长度，大端）
inline void pngWriteChunk(std::vector<uint8_t>& out, const char type[4],
                          const uint8_t* data, size_t len) {
    auto pushBE = [&out](uint32_t v) {
        out.push_back(static_cast<uint8_t>(v >> 24));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
    };
    pushBE(static_cast<uint32_t>(len));
    const size_t typeAt = out.size();
    out.insert(out.end(), type, type + 4);
    if (len > 0) {
        out.insert(out.end(), data, data + len);
    }
    pushBE(pngCrc32(out.data() + typeAt, 4 + len));
}

// 读取下一 PNG 块；返回 false 表示已越界。校验 CRC，损坏抛异常。
struct PngChunk {
    uint8_t type[4];
    const uint8_t* data;
    size_t len;
};

inline bool pngNextChunk(const uint8_t* p, size_t size, size_t& pos, PngChunk& c) {
    if (pos + 12 > size) {
        return false;
    }
    c.len = pngBE32(p + pos);
    c.type[0] = p[pos + 4];
    c.type[1] = p[pos + 5];
    c.type[2] = p[pos + 6];
    c.type[3] = p[pos + 7];
    if (pos + 12 + c.len > size) {
        throw std::runtime_error("wbwopenglapi: PNG 块长度越界");
    }
    c.data = p + pos + 8;
    const uint32_t crc = pngBE32(p + pos + 8 + c.len);
    if (pngCrc32(p + pos + 4, 4 + c.len) != crc) {
        throw std::runtime_error("wbwopenglapi: PNG CRC 校验失败");
    }
    pos += 12 + c.len;
    return true;
}

// zlib 流解压（PNG IDAT 为 RFC1950 zlib 格式）
inline std::vector<uint8_t> pngInflate(const uint8_t* data, size_t size) {
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        throw std::runtime_error("wbwopenglapi: zlib 初始化失败");
    }
    zs.next_in = const_cast<uint8_t*>(data);
    zs.avail_in = static_cast<uInt>(size);
    std::vector<uint8_t> out;
    std::vector<uint8_t> buf(64 * 1024);
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        zs.next_out = buf.data();
        zs.avail_out = static_cast<uInt>(buf.size());
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            throw std::runtime_error("wbwopenglapi: PNG 数据解压失败");
        }
        out.insert(out.end(), buf.data(), buf.data() + buf.size() - zs.avail_out);
        if (ret == Z_OK && zs.avail_in == 0 && zs.avail_out > 0) {
            inflateEnd(&zs);
            throw std::runtime_error("wbwopenglapi: PNG 数据不完整");
        }
    }
    inflateEnd(&zs);
    return out;
}

// 单行滤波重建（bpp = 每像素字节数；filter 非法抛异常）
inline void pngUnfilterRow(const uint8_t* src, uint8_t* dst, const uint8_t* prev,
                           size_t rowBytes, int bpp, int filter) {
    for (size_t x = 0; x < rowBytes; ++x) {
        const uint8_t left = x >= static_cast<size_t>(bpp) ? dst[x - bpp] : 0;
        const uint8_t up = prev[x];
        const uint8_t upleft = x >= static_cast<size_t>(bpp) ? prev[x - bpp] : 0;
        const uint8_t v = src[x];
        switch (filter) {
            case 0: // None
                dst[x] = v;
                break;
            case 1: // Sub
                dst[x] = static_cast<uint8_t>(v + left);
                break;
            case 2: // Up
                dst[x] = static_cast<uint8_t>(v + up);
                break;
            case 3: // Average
                dst[x] = static_cast<uint8_t>(v + ((left + up) >> 1));
                break;
            case 4: { // Paeth
                const int p = static_cast<int>(left) + up - upleft;
                const int pa = std::abs(p - left);
                const int pb = std::abs(p - up);
                const int pc = std::abs(p - upleft);
                const uint8_t pred = (pa <= pb && pa <= pc) ? left
                                     : (pb <= pc)           ? up
                                                           : upleft;
                dst[x] = static_cast<uint8_t>(v + pred);
                break;
            }
            default:
                throw std::runtime_error("wbwopenglapi: 未知 PNG 滤波类型");
        }
    }
}

} // namespace detail

// 从内存解码 PNG；失败抛 std::runtime_error
inline Image loadPNG(const uint8_t* data, size_t size) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8 || std::memcmp(data, sig, 8) != 0) {
        throw std::runtime_error("wbwopenglapi: 不是 PNG 文件");
    }
    int width = 0, height = 0, bitDepth = 0, colorType = -1;
    std::vector<uint8_t> idat, plte, trns;
    size_t pos = 8;
    detail::PngChunk c;
    bool haveIhdr = false;
    while (detail::pngNextChunk(data, size, pos, c)) {
        if (std::memcmp(c.type, "IHDR", 4) == 0) {
            if (c.len < 13) {
                throw std::runtime_error("wbwopenglapi: PNG IHDR 长度非法");
            }
            width = static_cast<int>(detail::pngBE32(c.data));
            height = static_cast<int>(detail::pngBE32(c.data + 4));
            bitDepth = c.data[8];
            colorType = c.data[9];
            if (c.data[10] != 0) {
                throw std::runtime_error("wbwopenglapi: PNG 压缩方法非 0");
            }
            if (c.data[11] != 0) {
                throw std::runtime_error("wbwopenglapi: PNG 滤波方法非 0");
            }
            if (c.data[12] != 0) {
                throw std::runtime_error("wbwopenglapi: 不支持隔行(Adam7) PNG");
            }
            haveIhdr = true;
        } else if (std::memcmp(c.type, "PLTE", 4) == 0) {
            plte.assign(c.data, c.data + c.len);
        } else if (std::memcmp(c.type, "tRNS", 4) == 0) {
            trns.assign(c.data, c.data + c.len);
        } else if (std::memcmp(c.type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), c.data, c.data + c.len);
        } else if (std::memcmp(c.type, "IEND", 4) == 0) {
            break;
        }
    }
    if (!haveIhdr || width <= 0 || height <= 0) {
        throw std::runtime_error("wbwopenglapi: PNG 缺少 IHDR 或尺寸非法");
    }
    if (bitDepth != 8) {
        throw std::runtime_error("wbwopenglapi: 仅支持 8-bit PNG");
    }
    int channels = 0;
    if (colorType == 0) {
        channels = 1;
    } else if (colorType == 2) {
        channels = 3;
    } else if (colorType == 3) {
        channels = 1;
    } else if (colorType == 4) {
        channels = 2;
    } else if (colorType == 6) {
        channels = 4;
    } else {
        throw std::runtime_error("wbwopenglapi: 不支持的 PNG 颜色类型");
    }
    if (colorType == 3 && plte.empty()) {
        throw std::runtime_error("wbwopenglapi: 调色板 PNG 缺少 PLTE");
    }
    if (plte.size() % 3 != 0) {
        throw std::runtime_error("wbwopenglapi: PNG PLTE 长度非法");
    }
    if (idat.empty()) {
        throw std::runtime_error("wbwopenglapi: PNG 缺少 IDAT");
    }
    const size_t rowBytes = static_cast<size_t>(width) * channels;
    const size_t rawSize = static_cast<size_t>(height) * (1 + rowBytes);
    const std::vector<uint8_t> raw = detail::pngInflate(idat.data(), idat.size());
    if (raw.size() < rawSize) {
        throw std::runtime_error("wbwopenglapi: PNG 数据不完整");
    }
    Image img;
    img.width = width;
    img.height = height;
    img.rgba.reserve(static_cast<size_t>(width) * height * 4);
    std::vector<uint8_t> prev(rowBytes, 0), row(rowBytes);
    for (int y = 0; y < height; ++y) {
        const size_t at = static_cast<size_t>(y) * (1 + rowBytes);
        detail::pngUnfilterRow(raw.data() + at + 1, row.data(), prev.data(),
                               rowBytes, channels, raw[at]);
        const uint8_t* p = row.data();
        for (int x = 0; x < width; ++x) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (colorType == 0) {
                r = g = b = p[x];
                if (trns.size() >= 2 && (trns[0] << 8 | trns[1]) == p[x]) {
                    a = 0;
                }
            } else if (colorType == 2) {
                r = p[x * 3];
                g = p[x * 3 + 1];
                b = p[x * 3 + 2];
                if (trns.size() >= 3 && r == trns[0] && g == trns[1] &&
                    b == trns[2]) {
                    a = 0;
                }
            } else if (colorType == 3) {
                const uint8_t idx = p[x];
                if (static_cast<size_t>(idx) * 3 + 2 < plte.size()) {
                    r = plte[idx * 3];
                    g = plte[idx * 3 + 1];
                    b = plte[idx * 3 + 2];
                }
                if (static_cast<size_t>(idx) < trns.size()) {
                    a = trns[idx];
                }
            } else if (colorType == 4) {
                r = g = b = p[x * 2];
                a = p[x * 2 + 1];
            } else { // 6
                r = p[x * 4];
                g = p[x * 4 + 1];
                b = p[x * 4 + 2];
                a = p[x * 4 + 3];
            }
            img.rgba.push_back(r);
            img.rgba.push_back(g);
            img.rgba.push_back(b);
            img.rgba.push_back(a);
        }
        prev.swap(row);
    }
    return img;
}

// 从文件解码 PNG；失败抛 std::runtime_error
inline Image loadPNG(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("wbwopenglapi: 无法打开图像: " + path);
    }
    std::vector<uint8_t> buf;
    buf.reserve(256 * 1024);
    int ch;
    while ((ch = f.get()) != EOF) {
        buf.push_back(static_cast<uint8_t>(ch));
    }
    return loadPNG(buf.data(), buf.size());
}

// 编码 Image 为 PNG 字节（RGBA 8-bit，filter 0）；失败抛 std::runtime_error
inline std::vector<uint8_t> toPNG(const Image& img) {
    if (img.width <= 0 || img.height <= 0 ||
        img.rgba.size() < static_cast<size_t>(img.width) * img.height * 4) {
        throw std::runtime_error("wbwopenglapi: Image 数据非法，无法编码 PNG");
    }
    const size_t rowBytes = static_cast<size_t>(img.width) * 4;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(img.height) * (1 + rowBytes));
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0); // filter None
        raw.insert(raw.end(), img.rgba.data() + static_cast<size_t>(y) * rowBytes,
                   img.rgba.data() + static_cast<size_t>(y + 1) * rowBytes);
    }
    const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> comp(bound);
    uLongf compLen = bound;
    if (compress2(comp.data(), &compLen, raw.data(), static_cast<uLong>(raw.size()),
                  6) != Z_OK) {
        throw std::runtime_error("wbwopenglapi: PNG deflate 失败");
    }
    std::vector<uint8_t> out;
    out.reserve(8 + 25 + compLen + 12);
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), sig, sig + 8);
    uint8_t ihdr[13] = {0};
    auto be32 = [](uint8_t* q, uint32_t v) {
        q[0] = static_cast<uint8_t>(v >> 24);
        q[1] = static_cast<uint8_t>(v >> 16);
        q[2] = static_cast<uint8_t>(v >> 8);
        q[3] = static_cast<uint8_t>(v);
    };
    be32(ihdr, static_cast<uint32_t>(img.width));
    be32(ihdr + 4, static_cast<uint32_t>(img.height));
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // color type: RGBA
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    detail::pngWriteChunk(out, "IHDR", ihdr, 13);
    detail::pngWriteChunk(out, "IDAT", comp.data(), compLen);
    detail::pngWriteChunk(out, "IEND", nullptr, 0);
    return out;
}

// 编码 Image 并写入文件；失败抛 std::runtime_error
inline void savePNG(const Image& img, const std::string& path) {
    const std::vector<uint8_t> png = toPNG(img);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("wbwopenglapi: 无法写入图像: " + path);
    }
    f.write(reinterpret_cast<const char*>(png.data()),
            static_cast<std::streamsize>(png.size()));
}

#endif // WBWOPENGAL_API_PNG

// =====================================================================
// FontFace 实现（inline 成员函数；GDI / FreeType 双后端）
// =====================================================================

namespace detail {

// 默认字体文件候选（FreeType 用；GDI 后端忽略）
static inline std::string defaultFontFile() {
#ifdef _WIN32
    const char* cands[] = {
        "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\arial.ttf", "C:\\Windows\\Fonts\\segoeui.ttf",
    };
#else
    const char* cands[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    };
#endif
    for (const char* c : cands) {
        std::ifstream f(c);
        if (f.good()) {
            return c;
        }
    }
    return "";
}

inline void FontFace::init(const std::string& file, int sizePx) {
    sizePx_ = sizePx;
#ifdef WBWOPENGAL_API_FONT_FREETYPE
    if (FT_Init_FreeType(&lib_) != 0) {
        throw std::runtime_error("wbwopenglapi: FreeType 初始化失败");
    }
    std::string path = file;
    if (path.empty()) {
        path = defaultFontFile();
    }
    if (path.empty() || FT_New_Face(lib_, path.c_str(), 0, &face_) != 0) {
        FT_Done_FreeType(lib_);
        lib_ = nullptr;
        throw std::runtime_error("wbwopenglapi: 无法加载字体文件 \"" + path + "\"");
    }
    if (FT_Set_Char_Size(face_, 0, sizePx * 64, 72, 72) != 0) {
        destroy();
        throw std::runtime_error("wbwopenglapi: 设置字号失败");
    }
    ascender_ = static_cast<double>(face_->size->metrics.ascender);
    descender_ = static_cast<double>(face_->size->metrics.descender);
#if defined(WBWOPENGAL_API_FONT_HARFBUZZ)
    // hb_ft_font 继承 FT 的缩放（1/64 像素单位），position 与 advanceX 单位一致
    hbFont_ = hb_ft_font_create_referenced(face_);
#endif
#elif defined(WBWOPENGAL_API_FONT_DWRITE)
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&dwFact_)))) {
        throw std::runtime_error("wbwopenglapi: DirectWrite 工厂创建失败");
    }
    if (file.empty()) {
        // 系统字体集合默认字体（回退序列与 GDI 后端一致: Segoe UI -> Arial -> 微软雅黑；
        // 主字体缺字形时由 loadGlyph 顺次尝试回退字体，见 dwFallbacks_）
        IDWriteFontCollection* col = nullptr;
        if (SUCCEEDED(dwFact_->GetSystemFontCollection(&col, TRUE))) {
            const wchar_t* families[] = {L"Segoe UI", L"Arial", L"Microsoft YaHei UI"};
            IDWriteFontFace** slot = &dwFace_;
            for (const wchar_t* fn : families) {
                UINT32 idx = 0;
                BOOL exists = FALSE;
                if (SUCCEEDED(col->FindFamilyName(fn, &idx, &exists)) && exists) {
                    IDWriteFontFamily* fam = nullptr;
                    if (SUCCEEDED(col->GetFontFamily(idx, &fam))) {
                        IDWriteFont* font = nullptr;
                        if (SUCCEEDED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                                                                DWRITE_FONT_STRETCH_NORMAL,
                                                                DWRITE_FONT_STYLE_NORMAL,
                                                                &font))) {
                            font->CreateFontFace(slot);
                            font->Release();
                            if (*slot && slot != &dwFallbacks_[1]) {
                                slot = (slot == &dwFace_) ? &dwFallbacks_[0] : &dwFallbacks_[1];
                            }
                        }
                        fam->Release();
                    }
                }
            }
            col->Release();
        }
        if (!dwFace_) {
            destroy();
            throw std::runtime_error("wbwopenglapi: DirectWrite 未找到可用系统字体");
        }
    } else {
        // UTF-8 -> UTF-16 文件路径
        std::wstring wpath;
        const int wn = MultiByteToWideChar(CP_UTF8, 0, file.c_str(), -1, nullptr, 0);
        if (wn <= 0) {
            destroy();
            throw std::runtime_error("wbwopenglapi: 字体路径编码转换失败");
        }
        wpath.resize(static_cast<size_t>(wn) - 1);
        MultiByteToWideChar(CP_UTF8, 0, file.c_str(), -1, &wpath[0], wn);
        IDWriteFontFile* ff = nullptr;
        if (FAILED(dwFact_->CreateFontFileReference(wpath.c_str(), nullptr, &ff))) {
            destroy();
            throw std::runtime_error("wbwopenglapi: DirectWrite 无法打开字体文件 \"" + file + "\"");
        }
        const HRESULT hr = dwFact_->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &ff, 0,
                                                   DWRITE_FONT_SIMULATIONS_NONE, &dwFace_);
        ff->Release();
        if (FAILED(hr) || !dwFace_) {
            destroy();
            throw std::runtime_error("wbwopenglapi: DirectWrite 无法创建字体 \"" + file + "\"");
        }
    }
    // 度量: 设计单位 -> 1/64 像素（× sizePx / upem × 64；descender 为负，与 GDI 语义一致）
    DWRITE_FONT_METRICS m = {};
    dwFace_->GetMetrics(&m);
    if (m.designUnitsPerEm > 0) {
        upem_ = static_cast<double>(m.designUnitsPerEm);
    }
    const double sc = static_cast<double>(sizePx) * 64.0 / upem_;
    ascender_ = static_cast<double>(m.ascent) * sc;
    descender_ = -static_cast<double>(m.descent) * sc;
#else
    (void)file;
    dc_ = CreateCompatibleDC(nullptr);
    if (!dc_) {
        throw std::runtime_error("wbwopenglapi: 创建字体 DC 失败");
    }
    const wchar_t* faces[] = {L"Segoe UI", L"Arial", L"Microsoft YaHei UI"};
    for (const wchar_t* fn : faces) {
        font_ = CreateFontW(-sizePx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, fn);
        if (font_) {
            break;
        }
    }
    if (!font_) {
        DeleteDC(dc_);
        dc_ = nullptr;
        throw std::runtime_error("wbwopenglapi: 创建字体失败");
    }
    SelectObject(dc_, font_);
    SetMapMode(dc_, MM_TEXT);
    OUTLINETEXTMETRICW otm = {};
    if (GetOutlineTextMetricsW(dc_, sizeof(otm), &otm) != 0) {
        ascender_ = static_cast<double>(otm.otmAscent) * 64.0;
        descender_ = -static_cast<double>(otm.otmDescent) * 64.0;
    }
#endif
}

inline void FontFace::destroy() {
#ifdef WBWOPENGAL_API_FONT_FREETYPE
#if defined(WBWOPENGAL_API_FONT_HARFBUZZ)
    if (hbFont_) {
        hb_font_destroy(hbFont_);
        hbFont_ = nullptr;
    }
#endif
    if (face_) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }
    if (lib_) {
        FT_Done_FreeType(lib_);
        lib_ = nullptr;
    }
#elif defined(WBWOPENGAL_API_FONT_DWRITE)
    if (dwFace_) {
        dwFace_->Release();
        dwFace_ = nullptr;
    }
    for (IDWriteFontFace*& f : dwFallbacks_) {
        if (f) {
            f->Release();
            f = nullptr;
        }
    }
    if (dwFact_) {
        dwFact_->Release();
        dwFact_ = nullptr;
    }
#else
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (dc_) {
        DeleteDC(dc_);
        dc_ = nullptr;
    }
#endif
}

#ifdef WBWOPENGAL_API_FONT_FREETYPE

// FT_Outline_Decompose 回调收集器
struct OutlineToSegs {
    std::vector<PathSeg>& out;
    FT_Outline_Funcs cbs;
    explicit OutlineToSegs(std::vector<PathSeg>& o) : out(o) {
        cbs.move_to = &OutlineToSegs::moveTo;
        cbs.line_to = &OutlineToSegs::lineTo;
        cbs.conic_to = &OutlineToSegs::conicTo;
        cbs.cubic_to = &OutlineToSegs::cubicTo;
        cbs.shift = 0;
        cbs.delta = 0;
    }
    static int moveTo(const FT_Vector* to, void* user) {
        auto* s = static_cast<OutlineToSegs*>(user);
        s->out.push_back({PathCmd::MoveTo, static_cast<float>(to->x),
                          static_cast<float>(to->y)});
        return 0;
    }
    static int lineTo(const FT_Vector* to, void* user) {
        auto* s = static_cast<OutlineToSegs*>(user);
        s->out.push_back({PathCmd::LineTo, static_cast<float>(to->x),
                          static_cast<float>(to->y)});
        return 0;
    }
    static int conicTo(const FT_Vector* c, const FT_Vector* to, void* user) {
        auto* s = static_cast<OutlineToSegs*>(user);
        s->out.push_back({PathCmd::QuadraticTo, static_cast<float>(c->x),
                          static_cast<float>(c->y), static_cast<float>(to->x),
                          static_cast<float>(to->y)});
        return 0;
    }
    static int cubicTo(const FT_Vector* c1, const FT_Vector* c2,
                       const FT_Vector* to, void* user) {
        auto* s = static_cast<OutlineToSegs*>(user);
        s->out.push_back({PathCmd::CubicTo, static_cast<float>(c1->x),
                          static_cast<float>(c1->y), static_cast<float>(c2->x),
                          static_cast<float>(c2->y), static_cast<float>(to->x),
                          static_cast<float>(to->y)});
        return 0;
    }
};

inline Glyph FontFace::loadGlyph(uint32_t cp) const {
    return loadGlyphIndex(FT_Get_Char_Index(face_, cp));
}

inline Glyph FontFace::loadGlyphIndex(uint32_t idx) const {
    Glyph g;
    if (idx == 0) {
        return g;
    }
    if (FT_Load_Glyph(face_, static_cast<FT_UInt>(idx), FT_LOAD_NO_BITMAP) != 0) {
        return g;
    }
    FT_Outline* ol = &face_->glyph->outline;
    if (ol->n_points > 0) {
        OutlineToSegs conv(g.outline);
        FT_Outline_Decompose(ol, &conv.cbs, &conv);
    }
    g.advanceX = static_cast<double>(face_->glyph->advance.x);
    return g;
}

#if defined(WBWOPENGAL_API_FONT_HARFBUZZ)

inline std::vector<ShapedGlyph> FontFace::shape(
    const std::string& text,
    const std::vector<std::pair<std::string, bool>>& feats) const {
    std::vector<ShapedGlyph> out;
    if (!hbFont_ || text.empty()) {
        return out;
    }
    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0,
                       static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buf);
    std::vector<hb_feature_t> hbFeats;
    hbFeats.reserve(feats.size());
    for (const auto& f : feats) {
        if (f.first.size() != 4) {
            continue;
        }
        hb_feature_t hf = {};
        hf.tag = HB_TAG(static_cast<unsigned char>(f.first[0]),
                        static_cast<unsigned char>(f.first[1]),
                        static_cast<unsigned char>(f.first[2]),
                        static_cast<unsigned char>(f.first[3]));
        hf.value = f.second ? 1u : 0u;
        hf.start = 0;
        hf.end = static_cast<unsigned int>(-1);
        hbFeats.push_back(hf);
    }
    hb_shape(hbFont_, buf, hbFeats.data(), static_cast<unsigned int>(hbFeats.size()));
    const unsigned int n = hb_buffer_get_length(buf);
    out.reserve(n);
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, nullptr);
    const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);
    for (unsigned int k = 0; k < n; ++k) {
        ShapedGlyph sg;
        sg.index = infos[k].codepoint;
        if (pos) {
            sg.dx = static_cast<double>(pos[k].x_offset);
            sg.dy = static_cast<double>(pos[k].y_offset);
            sg.advanceX = static_cast<double>(pos[k].x_advance);
        }
        out.push_back(sg);
    }
    hb_buffer_destroy(buf);
    return out;
}

#else // 无 HarfBuzz: shape 不可用（Canvas 的 shapingActive() 恒为 false）

inline std::vector<ShapedGlyph> FontFace::shape(
    const std::string&, const std::vector<std::pair<std::string, bool>>&) const {
    return {};
}

#endif

#elif defined(WBWOPENGAL_API_FONT_DWRITE)

// IDWriteGeometrySink 实现: 收集 GetGlyphRunOutline 输出的绝对坐标轮廓
// （像素单位、y 向下）-> PathSeg（1/64 像素、y 向上，与 FreeType/GDI 统一）。
// 注意: 仅实现接口签名，D2D1_FILL_MODE/D2D1_PATH_SEGMENT 不参与收集
struct DwOutlineSink : IDWriteGeometrySink {
    std::vector<PathSeg>& out;
    explicit DwOutlineSink(std::vector<PathSeg>& o) : out(o) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) override {
        out.push_back({PathCmd::MoveTo, start.x * 64.0f, -start.y * 64.0f});
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* pts, UINT n) override {
        for (UINT i = 0; i < n; ++i) {
            out.push_back({PathCmd::LineTo, pts[i].x * 64.0f, -pts[i].y * 64.0f});
        }
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* segs, UINT n) override {
        for (UINT i = 0; i < n; ++i) {
            out.push_back({PathCmd::CubicTo,
                           segs[i].point1.x * 64.0f, -segs[i].point1.y * 64.0f,
                           segs[i].point2.x * 64.0f, -segs[i].point2.y * 64.0f,
                           segs[i].point3.x * 64.0f, -segs[i].point3.y * 64.0f});
        }
    }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override {}
    STDMETHODIMP Close() override { return S_OK; }
};

inline Glyph FontFace::loadGlyph(uint32_t cp) const {
    Glyph g;
    if (!dwFace_) {
        return g;
    }
    // 主字体缺字形（gidx==0）时顺次尝试回退链（Segoe UI -> Arial -> 微软雅黑）
    IDWriteFontFace* f = dwFace_;
    UINT16 gidx = 0;
    for (;;) {
        if (FAILED(f->GetGlyphIndices(&cp, 1, &gidx))) {
            gidx = 0;
        }
        if (gidx != 0) {
            break;
        }
        f = (f == dwFace_) ? dwFallbacks_[0]
                           : (f == dwFallbacks_[0]) ? dwFallbacks_[1] : nullptr;
        if (!f) {
            break;
        }
    }
    if (!f || gidx == 0) {
        return g; // 全链缺字形（与 GDI 语义一致: 返回空轮廓）
    }
    DwOutlineSink sink(g.outline);
    // emSize = sizePx（像素单位）; isSideways/isRightToLeft = FALSE
    if (FAILED(f->GetGlyphRunOutline(static_cast<FLOAT>(sizePx_), &gidx, nullptr,
                                     nullptr, 1, FALSE, FALSE, &sink))) {
        g.outline.clear();
        return g;
    }
    // advance: 设计单位 -> 1/64 像素（× sizePx / upem × 64；upem 按实际字体取）
    DWRITE_GLYPH_METRICS gm = {};
    if (SUCCEEDED(f->GetDesignGlyphMetrics(&gidx, 1, &gm, FALSE))) {
        DWRITE_FONT_METRICS m = {};
        f->GetMetrics(&m);
        if (m.designUnitsPerEm > 0) {
            g.advanceX = static_cast<double>(gm.advanceWidth) *
                         static_cast<double>(sizePx_) * 64.0 /
                         static_cast<double>(m.designUnitsPerEm);
        }
    }
    return g;
}

// DWrite 后端无字形索引/GSUB 接口（连体与特性不可用，同 GDI 语义）
inline Glyph FontFace::loadGlyphIndex(uint32_t) const { return {}; }

inline std::vector<ShapedGlyph> FontFace::shape(
    const std::string&, const std::vector<std::pair<std::string, bool>>&) const {
    return {};
}

#else // GDI 后端

// POINTFX 16.16 定点 -> 1/64 像素值（GDI 输出单位为像素，×64 与 FreeType 统一）
static inline double pointFx(const POINTFX& p, bool y) {
    const FIXED& f = y ? p.y : p.x;
    return (static_cast<double>(f.value) + static_cast<double>(f.fract) / 65536.0) * 64.0;
}

inline Glyph FontFace::loadGlyph(uint32_t cp) const {
    Glyph g;
    GLYPHMETRICS gm = {};
    MAT2 mat = {{0, 1}, {0, 0}, {0, 0}, {0, 1}}; // 恒等变换（1/64 像素单位）
    const DWORD sz = GetGlyphOutlineW(dc_, static_cast<WCHAR>(cp), GGO_NATIVE,
                                      &gm, 0, nullptr, &mat);
    if (sz == GDI_ERROR) {
        return g;
    }
    if (sz > 0) {
        std::vector<BYTE> buf(sz);
        if (GetGlyphOutlineW(dc_, static_cast<WCHAR>(cp), GGO_NATIVE, &gm,
                             sz, buf.data(), &mat) == GDI_ERROR) {
            return g;
        }
        const BYTE* end = buf.data() + buf.size();
        const TTPOLYGONHEADER* hdr =
            reinterpret_cast<const TTPOLYGONHEADER*>(buf.data());
        while (reinterpret_cast<const BYTE*>(hdr) < end) {
            g.outline.push_back({PathCmd::MoveTo,
                                 static_cast<float>(pointFx(hdr->pfxStart, false)),
                                 static_cast<float>(pointFx(hdr->pfxStart, true))});
            const BYTE* p =
                reinterpret_cast<const BYTE*>(hdr) + sizeof(TTPOLYGONHEADER);
            const BYTE* hdrEnd = reinterpret_cast<const BYTE*>(hdr) + hdr->cb;
            while (p < hdrEnd) {
                const TTPOLYCURVE* curve = reinterpret_cast<const TTPOLYCURVE*>(p);
                if (curve->wType == TT_PRIM_LINE) {
                    for (DWORD i = 0; i < curve->cpfx; ++i) {
                        g.outline.push_back({PathCmd::LineTo,
                                             static_cast<float>(pointFx(curve->apfx[i], false)),
                                             static_cast<float>(pointFx(curve->apfx[i], true))});
                    }
                } else if (curve->wType == TT_PRIM_QSPLINE) {
                    DWORD i = 0;
                    while (i + 1 < curve->cpfx) {
                        g.outline.push_back({PathCmd::QuadraticTo,
                                             static_cast<float>(pointFx(curve->apfx[i], false)),
                                             static_cast<float>(pointFx(curve->apfx[i], true)),
                                             static_cast<float>(pointFx(curve->apfx[i + 1], false)),
                                             static_cast<float>(pointFx(curve->apfx[i + 1], true))});
                        i += 2;
                    }
                    if (i < curve->cpfx) { // 剩余 1 点：控制点=终点
                        g.outline.push_back({PathCmd::QuadraticTo,
                                             static_cast<float>(pointFx(curve->apfx[i], false)),
                                             static_cast<float>(pointFx(curve->apfx[i], true)),
                                             static_cast<float>(pointFx(curve->apfx[i], false)),
                                             static_cast<float>(pointFx(curve->apfx[i], true))});
                    }
                }
                p += sizeof(TTPOLYCURVE) - sizeof(POINTFX) +
                     curve->cpfx * sizeof(POINTFX);
            }
            hdr = reinterpret_cast<const TTPOLYGONHEADER*>(hdrEnd);
        }
    }
    g.advanceX = static_cast<double>(gm.gmCellIncX) * 64.0;
    return g;
}

// GDI 后端无字形索引/GSUB 接口（连体与特性不可用）
inline Glyph FontFace::loadGlyphIndex(uint32_t) const { return {}; }

inline std::vector<ShapedGlyph> FontFace::shape(
    const std::string&, const std::vector<std::pair<std::string, bool>>&) const {
    return {};
}

#endif // WBWOPENGAL_API_FONT_FREETYPE

} // namespace detail

namespace detail {
struct GlfwLife {
    GlfwLife() {
        glfwSetErrorCallback(glfwErrorCallback);
        if (!glfwInit()) {
            throw std::runtime_error("wbwopenglapi: glfwInit 失败");
        }
    }
    ~GlfwLife() { glfwTerminate(); }

    static void glfwErrorCallback(int code, const char* desc) {
        lastGlfwError() = std::string(desc) + " (错误码 " + std::to_string(code) + ")";
    }
    static std::string& lastGlfwError() {
        static std::string err;
        return err;
    }
};

inline std::shared_ptr<GlfwLife>& glfwLife() {
    static std::shared_ptr<GlfwLife> life;
    if (!life) {
        life = std::make_shared<GlfwLife>();
    }
    return life;
}

} // namespace detail

// =====================================================================
// Window - GLFW 窗口与 OpenGL 上下文（RAII）
// =====================================================================
class Window {
public:
    // 创建窗口并初始化 GL 3.3 core 上下文（GLAD 加载 + 版本校验）
    // visible=false 创建隐藏窗口（无头渲染，供 Node.js 绑定等使用）
    Window(int w, int h, const std::string& title, bool resizable = true,
           bool visible = true) {
        glfwLife_ = detail::glfwLife();

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

        window_ = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
        if (!window_) {
            throw std::runtime_error("wbwopenglapi: 窗口创建失败 - " +
                                     detail::GlfwLife::lastGlfwError());
        }
        glfwMakeContextCurrent(window_);

        if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
            throw std::runtime_error("wbwopenglapi: GLAD 加载 OpenGL 函数失败");
        }

        // 校验 OpenGL 版本 >= 3.3（core）
        int major = 0, minor = 0;
        sscanf(reinterpret_cast<const char*>(glGetString(GL_VERSION)), "%d.%d", &major, &minor);
        if (major < 3 || (major == 3 && minor < 3)) {
            std::string ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            glfwDestroyWindow(window_);
            window_ = nullptr;
            throw std::runtime_error(
                "wbwopenglapi: 需要 OpenGL 3.3+, 当前驱动仅支持 " + ver);
        }

        glfwSwapInterval(1); // 垂直同步

        // 事件回调钩子：GLFW 回调 -> 用户指针取回 Window -> std::function 分发
        glfwSetWindowUserPointer(window_, this);
        glfwSetKeyCallback(window_, windowKeyCallback);
        glfwSetCharCallback(window_, windowCharCallback);
        glfwSetMouseButtonCallback(window_, windowMouseButtonCallback);
        glfwSetCursorPosCallback(window_, windowCursorPosCallback);
        glfwSetScrollCallback(window_, windowScrollCallback);
        glfwSetCursorEnterCallback(window_, windowCursorEnterCallback);
        glfwSetFramebufferSizeCallback(window_, windowFramebufferSizeCallback);
        glfwSetWindowCloseCallback(window_, windowCloseCallback);
    }

    ~Window() {
        if (window_) {
            glfwDestroyWindow(window_);
        }
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // ---- 尺寸 ----
    int width() const {
        int w = 0, h = 0;
        glfwGetWindowSize(window_, &w, &h);
        return w;
    }
    int height() const {
        int w = 0, h = 0;
        glfwGetWindowSize(window_, &w, &h);
        return h;
    }
    // 实际像素尺寸（HiDPI 下可能与逻辑尺寸不同，绘制坐标基于它）
    int framebufferWidth() const {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        return w;
    }
    int framebufferHeight() const {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        return h;
    }

    // ---- 事件循环 ----
    bool shouldClose() const { return glfwWindowShouldClose(window_) != 0; }
    void pollEvents() { glfwPollEvents(); }
    // 提交帧（swapBuffers 前自动调用已注册 Canvas 的抗锯齿 resolve，定义见类外）
    void swapBuffers();
    void close() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

    // ---- 输入轮询 ----
    bool keyPressed(int key) const { return glfwGetKey(window_, key) == GLFW_PRESS; }
    bool mousePressed(int button) const {
        return glfwGetMouseButton(window_, button) == GLFW_PRESS;
    }
    void mousePosition(double& x, double& y) const { glfwGetCursorPos(window_, &x, &y); }

    // ---- 事件回调（GLFW 回调注册；与轮询 API 并存，向后兼容）----
    // 各回调参数与 GLFW 一致；用户指针经窗口用户指针取回本 Window 实例分发。
    // 通过 nativeHandle() 自行注册 GLFW 回调会覆盖这里的钩子（不推荐混用）。
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using CharCallback = std::function<void(unsigned int codepoint)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using CursorPosCallback = std::function<void(double x, double y)>;
    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
    using CursorEnterCallback = std::function<void(int entered)>;
    using FramebufferSizeCallback = std::function<void(int width, int height)>;
    using CloseCallback = std::function<void()>;

    void setKeyCallback(KeyCallback cb) { keyCb_ = std::move(cb); }
    void setCharCallback(CharCallback cb) { charCb_ = std::move(cb); }
    void setMouseButtonCallback(MouseButtonCallback cb) { mouseButtonCb_ = std::move(cb); }
    void setCursorPosCallback(CursorPosCallback cb) { cursorPosCb_ = std::move(cb); }
    void setScrollCallback(ScrollCallback cb) { scrollCb_ = std::move(cb); }
    void setCursorEnterCallback(CursorEnterCallback cb) { cursorEnterCb_ = std::move(cb); }
    void setFramebufferSizeCallback(FramebufferSizeCallback cb) {
        framebufferSizeCb_ = std::move(cb);
    }
    void setCloseCallback(CloseCallback cb) { closeCb_ = std::move(cb); }

    GLFWwindow* nativeHandle() const { return window_; }

    // 内部：Canvas 构造/析构时注册注销（用于 swapBuffers 前触发 present）
    void attachCanvas(class Canvas* c) { attached_.push_back(c); }
    void detachCanvas(class Canvas* c) {
        for (size_t i = 0; i < attached_.size(); ++i) {
            if (attached_[i] == c) {
                attached_.erase(attached_.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
        }
    }

private:
    // GLFW 回调分发（静态；经窗口用户指针取回 Window 实例）
    static Window* windowOf(GLFWwindow* w) {
        return static_cast<Window*>(glfwGetWindowUserPointer(w));
    }
    static void windowKeyCallback(GLFWwindow* w, int key, int scancode, int action,
                                  int mods) {
        if (Window* self = windowOf(w)) {
            if (self->keyCb_) {
                self->keyCb_(key, scancode, action, mods);
            }
        }
    }
    static void windowCharCallback(GLFWwindow* w, unsigned int codepoint) {
        if (Window* self = windowOf(w)) {
            if (self->charCb_) {
                self->charCb_(codepoint);
            }
        }
    }
    static void windowMouseButtonCallback(GLFWwindow* w, int button, int action,
                                          int mods) {
        if (Window* self = windowOf(w)) {
            if (self->mouseButtonCb_) {
                self->mouseButtonCb_(button, action, mods);
            }
        }
    }
    static void windowCursorPosCallback(GLFWwindow* w, double x, double y) {
        if (Window* self = windowOf(w)) {
            if (self->cursorPosCb_) {
                self->cursorPosCb_(x, y);
            }
        }
    }
    static void windowScrollCallback(GLFWwindow* w, double x, double y) {
        if (Window* self = windowOf(w)) {
            if (self->scrollCb_) {
                self->scrollCb_(x, y);
            }
        }
    }
    static void windowCursorEnterCallback(GLFWwindow* w, int entered) {
        if (Window* self = windowOf(w)) {
            if (self->cursorEnterCb_) {
                self->cursorEnterCb_(entered);
            }
        }
    }
    static void windowFramebufferSizeCallback(GLFWwindow* w, int width, int height) {
        if (Window* self = windowOf(w)) {
            if (self->framebufferSizeCb_) {
                self->framebufferSizeCb_(width, height);
            }
        }
    }
    static void windowCloseCallback(GLFWwindow* w) {
        if (Window* self = windowOf(w)) {
            if (self->closeCb_) {
                self->closeCb_();
            }
        }
    }

    GLFWwindow* window_ = nullptr;
    std::shared_ptr<detail::GlfwLife> glfwLife_;
    std::vector<class Canvas*> attached_;
    KeyCallback keyCb_;
    CharCallback charCb_;
    MouseButtonCallback mouseButtonCb_;
    CursorPosCallback cursorPosCb_;
    ScrollCallback scrollCb_;
    CursorEnterCallback cursorEnterCb_;
    FramebufferSizeCallback framebufferSizeCb_;
    CloseCallback closeCb_;
};

// =====================================================================
// Canvas - 绘制上下文（即 "ctx"），一次创建、每帧绘制
// =====================================================================
class Canvas {
public:
    friend class Window; // Window::swapBuffers 需要触发 Canvas::present
    explicit Canvas(Window& win) : window_(win) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        updateViewport();

        program_ = std::make_unique<detail::Program>(kSolidVS, kSolidFS);
        vao_ = std::make_unique<detail::VertexArray>();
        vbo_ = std::make_unique<detail::VertexBuffer>();
        vao_->bind();
        vbo_->bind(); // 必须先把 VBO 绑定为当前 GL_ARRAY_BUFFER，属性格式才会记录到 VAO
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(detail::Vec2),
                              reinterpret_cast<void*>(0));
        glBindVertexArray(0);

        // 纹理通道（drawImage）
        texProgram_ = std::make_unique<detail::Program>(kTexVS, kTexFS);
        texVao_ = std::make_unique<detail::VertexArray>();
        texVbo_ = std::make_unique<detail::VertexBuffer>();
        tex_ = std::make_unique<detail::Texture>();
        texVao_->bind();
        texVbo_->bind();
        const size_t stride = sizeof(TexVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(stride),
                              reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(stride),
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glBindVertexArray(0);

        resetTransform();
        window_.attachCanvas(this);
    }

    ~Canvas() { window_.detachCanvas(this); }

    // 清屏（Canvas 无背景概念，此为便捷扩展）
    void clear(const Color& c) {
        ensureFrame();
        updateViewport();
        glClearColor(c.r, c.g, c.b, c.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    void clear(const std::string& css) { clear(parseColor(css)); }

    int width() const { return window_.framebufferWidth(); }
    int height() const { return window_.framebufferHeight(); }

    // ---------------- 样式属性 ----------------

    void fillStyle(const Color& c) {
        fillStyle_ = c;
        fillGrad_.reset();
    }
    void fillStyle(const std::string& css) { fillStyle(parseColor(css)); }
    void fillStyle(const Gradient& g) { fillGrad_ = std::make_shared<Gradient>(g); }
    void strokeStyle(const Color& c) {
        strokeStyle_ = c;
        strokeGrad_.reset();
    }
    void strokeStyle(const std::string& css) { strokeStyle(parseColor(css)); }
    void strokeStyle(const Gradient& g) { strokeGrad_ = std::make_shared<Gradient>(g); }

    // 创建线性渐变（用户空间坐标，绘制时受当前变换影响）
    Gradient createLinearGradient(double x0, double y0, double x1, double y1) const {
        Gradient g;
        g.x0 = x0;
        g.y0 = y0;
        g.x1 = x1;
        g.y1 = y1;
        return g;
    }
    // 创建径向渐变（两圆焦点：内圆 (x0,y0,r0) -> 外圆 (x1,y1,r1)）
    Gradient createRadialGradient(double x0, double y0, double r0, double x1,
                                  double y1, double r1) const {
        Gradient g;
        g.radial = true;
        g.x0 = x0;
        g.y0 = y0;
        g.r0 = r0;
        g.x1 = x1;
        g.y1 = y1;
        g.r1 = r1;
        return g;
    }

    // 描边宽度（像素），非正数忽略（与 Canvas 语义一致）
    void lineWidth(double w) {
        if (w > 0.0) {
            lineWidth_ = w;
        }
    }

    // 全局透明度 0..1（越界自动截断）
    void globalAlpha(double a) {
        if (a < 0.0) a = 0.0;
        if (a > 1.0) a = 1.0;
        globalAlpha_ = a;
    }

    // 合成模式（Canvas globalCompositeOperation 语义，12 种；未知值保持当前不变）。
    // 因子见 applyComposite()：全部模式单遍 glBlendFunc 即可表达
    // （DST_ALPHA/DST_COLOR 系因子，无需两遍法）。
    void globalCompositeOperation(const std::string& name) {
        if (name == "source-over" || name == "source-in" || name == "source-out" ||
            name == "source-atop" || name == "destination-over" ||
            name == "destination-in" || name == "destination-out" ||
            name == "destination-atop" || name == "lighter" || name == "copy" ||
            name == "xor" || name == "multiply") {
            composite_ = name;
        }
    }

    // 线条光栅化算法（仅影响用户路径 stroke()；strokeText/strokeRect 始终矢量描边）
    //   "default"   三角带矢量描边（默认，= 现有行为；尊重 lineWidth）
    //   "dda"       逐像素直线（1px，无抗锯齿，阶梯锯齿）
    //   "bresenham" 逐像素直线（1px，整数误差累积，无抗锯齿）
    //   "wu"        Xiaolin Wu 抗锯齿直线（1px，相邻像素 alpha 渐变）
    // 设为 dda/bresenham/wu 时 lineWidth 被忽略（固定 1px 逻辑线宽）；
    // 未知值回退 "default"。
    void lineAlgorithm(const std::string& name) {
        if (name == "dda") {
            lineAlgorithm_ = detail::LineAlgo::Dda;
        } else if (name == "bresenham") {
            lineAlgorithm_ = detail::LineAlgo::Bresenham;
        } else if (name == "wu") {
            lineAlgorithm_ = detail::LineAlgo::Wu;
        } else {
            lineAlgorithm_ = detail::LineAlgo::Default;
        }
    }

    // 全局抗锯齿模式（影响整帧渲染，含文本/路径/图像）：
    //   "off"   直接绘制默认 framebuffer（默认；零额外开销，输出与旧版一致）
    //   "ssaa"  2x 超采样离屏渲染后缩小（画质最高，片元开销 ~4x）
    //   "msaa"  4x 多重采样离屏渲染后 resolve（画质/性能均衡，推荐启用）
    //   "fxaa"  全屏后处理（快；文本/细线边缘会轻微模糊）
    //   "mlaa"  简化形态学抗锯齿两遍后处理（快；质量低于 FXAA）
    // 未知值回退 "off"。模式在下一帧 clear()/绘制时生效。
    void antialias(const std::string& mode) {
        if (mode == "ssaa" || mode == "msaa" || mode == "fxaa" || mode == "mlaa") {
            antialias_ = mode;
        } else {
            antialias_ = "off";
        }
        aaDirty_ = true;
    }

    // 主动把离屏结果合成到默认 framebuffer（通常由 swapBuffers 自动完成；
    // 帧内显式调用可在不交换缓冲的前提下读回最终像素，如 Node 导出）
    void resolve() { present(); }

    // ---------------- 矩形 ----------------

    // 填充矩形 [x, x+w) x [y, y+h)
    void fillRect(double x, double y, double w, double h) {
        if (w <= 0.0 || h <= 0.0) {
            return;
        }
        float x0 = static_cast<float>(x), y0 = static_cast<float>(y);
        float x1 = static_cast<float>(x + w), y1 = static_cast<float>(y + h);
        const detail::Vec2 tris[6] = {
            {x0, y0}, {x1, y0}, {x1, y1},
            {x0, y0}, {x1, y1}, {x0, y1},
        };
        drawSolid(fillStyle_, tris, 6, nullptr, fillGrad_.get());
    }

    // 描边矩形（中心线沿矩形边界，宽度为 lineWidth）
    void strokeRect(double x, double y, double w, double h) {
        if (w <= 0.0 || h <= 0.0) {
            return;
        }
        const std::vector<detail::Vec2> pts = {
            {static_cast<float>(x), static_cast<float>(y)},
            {static_cast<float>(x + w), static_cast<float>(y)},
            {static_cast<float>(x + w), static_cast<float>(y + h)},
            {static_cast<float>(x), static_cast<float>(y + h)},
        };
        std::vector<detail::Vec2> strip =
            detail::buildStrokeStrip(pts, true, static_cast<float>(lineWidth_));
        if (!strip.empty()) {
            drawSolid(strokeStyle_, strip.data(), strip.size(), nullptr,
                      strokeGrad_.get());
        }
    }

    // 清除矩形区域为透明（scissor + 透明清屏）
    void clearRect(double x, double y, double w, double h) {
        if (w <= 0.0 || h <= 0.0) {
            return;
        }
        ensureFrame(); // 确保清除目标是离屏 FBO（clear() 同样先切换）
        float sx = logicalToFbX(x);
        float sy = logicalToFbY(y);
        float sw = logicalToFbX(x + w) - sx;
        float sh = logicalToFbY(y + h) - sy;
        int fh = window_.framebufferHeight();
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<GLint>(std::lround(sx)),
                  static_cast<GLint>(std::lround(fh - sy - sh)),
                  static_cast<GLsizei>(std::lround(sw)),
                  static_cast<GLsizei>(std::lround(sh)));
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }

    // ---------------- 路径 ----------------

    // 清空当前路径
    void beginPath() {
        path_.clear();
        subPathPoints_ = 0;
    }

    void moveTo(double x, double y) {
        path_.push_back({detail::PathCmd::MoveTo, static_cast<float>(x),
                         static_cast<float>(y)});
        subPathPoints_ = 1;
    }

    void lineTo(double x, double y) {
        if (subPathPoints_ == 0) {
            moveTo(x, y); // Canvas 语义：无当前点时等价 moveTo
            return;
        }
        path_.push_back({detail::PathCmd::LineTo, static_cast<float>(x),
                         static_cast<float>(y)});
        ++subPathPoints_;
    }

    void quadraticCurveTo(double cx, double cy, double x, double y) {
        if (subPathPoints_ == 0) {
            moveTo(cx, cy); // Canvas 语义：无当前点时等价 moveTo(控制点)
        }
        path_.push_back({detail::PathCmd::QuadraticTo, static_cast<float>(cx),
                         static_cast<float>(cy), static_cast<float>(x),
                         static_cast<float>(y)});
        ++subPathPoints_;
    }

    void bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x,
                       double y) {
        if (subPathPoints_ == 0) {
            moveTo(c1x, c1y);
        }
        path_.push_back({detail::PathCmd::CubicTo, static_cast<float>(c1x),
                         static_cast<float>(c1y), static_cast<float>(c2x),
                         static_cast<float>(c2y), static_cast<float>(x),
                         static_cast<float>(y)});
        ++subPathPoints_;
    }

    // 圆弧（Canvas 语义：角度弧度制，y 向下顺时针为正；ccw 逆时针）
    void arc(double cx, double cy, double r, double a0, double a1, bool ccw = false) {
        if (r < 0.0) {
            throw std::invalid_argument("wbwopenglapi: arc 半径不能为负");
        }
        if (r == 0.0) { // Canvas 语义：零半径退化为直线到终点
            if (subPathPoints_ == 0) {
                moveTo(cx, cy);
            } else {
                lineTo(cx, cy);
            }
            return;
        }
        double delta = a1 - a0;
        if (ccw) {
            if (delta > 0.0) {
                delta -= 2.0 * detail::kPi;
            }
        } else {
            if (delta < 0.0) {
                delta += 2.0 * detail::kPi;
            }
        }
        const double px = cx + r * std::cos(a0);
        const double py = cy + r * std::sin(a0);
        if (subPathPoints_ == 0) {
            moveTo(px, py);
        } else {
            lineTo(px, py);
        }
        int n = static_cast<int>(std::ceil(std::abs(delta) / (detail::kPi / 16.0)));
        if (n < 8) {
            n = 8;
        }
        for (int i = 1; i <= n; ++i) {
            const double a = a0 + delta * i / n;
            lineTo(cx + r * std::cos(a), cy + r * std::sin(a));
        }
    }

    // 追加矩形子路径（Canvas 语义：moveTo + 3 条 lineTo + closePath）
    void rect(double x, double y, double w, double h) {
        moveTo(x, y);
        lineTo(x + w, y);
        lineTo(x + w, y + h);
        lineTo(x, y + h);
        closePath();
    }

    // 椭圆弧（Canvas 语义：rotation 为椭圆长轴旋转角，弧度制；
    // 起点/终点按椭圆参数方程计算，段数与 arc 相同）
    void ellipse(double cx, double cy, double rx, double ry, double rotation,
                 double a0, double a1, bool ccw = false) {
        if (rx < 0.0 || ry < 0.0) {
            throw std::invalid_argument("wbwopenglapi: ellipse 半径不能为负");
        }
        if (rx == 0.0 || ry == 0.0) { // Canvas 语义：零半径退化为直线到终点
            if (subPathPoints_ == 0) {
                moveTo(cx, cy);
            } else {
                lineTo(cx, cy);
            }
            return;
        }
        double delta = a1 - a0;
        if (ccw) {
            if (delta > 0.0) {
                delta -= 2.0 * detail::kPi;
            }
        } else {
            if (delta < 0.0) {
                delta += 2.0 * detail::kPi;
            }
        }
        const double cosR = std::cos(rotation);
        const double sinR = std::sin(rotation);
        auto ellP = [&](double a) {
            const double ca = std::cos(a) * rx;
            const double sa = std::sin(a) * ry;
            return std::pair<double, double>{cx + ca * cosR - sa * sinR,
                                             cy + ca * sinR + sa * cosR};
        };
        const auto p0 = ellP(a0);
        if (subPathPoints_ == 0) {
            moveTo(p0.first, p0.second);
        } else {
            lineTo(p0.first, p0.second);
        }
        int n = static_cast<int>(std::ceil(std::abs(delta) / (detail::kPi / 16.0)));
        if (n < 8) {
            n = 8;
        }
        for (int i = 1; i <= n; ++i) {
            const auto p = ellP(a0 + delta * i / n);
            lineTo(p.first, p.second);
        }
    }

    // 圆角矩形（Canvas 标准 roundRect；radius 支持单值或四角数组，
    // 负值/0 语义：负值按 max(0, r) 钳制，0 为直角；r 过大钳制到 w/2, h/2）
    void roundRect(double x, double y, double w, double h, double r) {
        roundRect(x, y, w, h, r, r, r, r);
    }
    void roundRect(double x, double y, double w, double h, double rTL,
                   double rTR, double rBR, double rBL) {
        if (w < 0.0 || h < 0.0) {
            throw std::invalid_argument("wbwopenglapi: roundRect 宽高不能为负");
        }
        if (w == 0.0 || h == 0.0) {
            return; // Canvas 语义：零尺寸不产生路径
        }
        const double maxR = std::min(std::abs(w), std::abs(h)) / 2.0;
        rTL = std::clamp(std::max(0.0, rTL), 0.0, maxR);
        rTR = std::clamp(std::max(0.0, rTR), 0.0, maxR);
        rBR = std::clamp(std::max(0.0, rBR), 0.0, maxR);
        rBL = std::clamp(std::max(0.0, rBL), 0.0, maxR);
        moveTo(x + rTL, y);
        if (rTR > 0.0) {
            arc(x + w - rTR, y + rTR, rTR, -detail::kPi / 2.0, 0.0);
        } else {
            lineTo(x + w, y);
        }
        if (rBR > 0.0) {
            arc(x + w - rBR, y + h - rBR, rBR, 0.0, detail::kPi / 2.0);
        } else {
            lineTo(x + w, y + h);
        }
        if (rBL > 0.0) {
            arc(x + rBL, y + h - rBL, rBL, detail::kPi / 2.0, detail::kPi);
        } else {
            lineTo(x, y + h);
        }
        if (rTL > 0.0) {
            arc(x + rTL, y + rTL, rTL, detail::kPi, 3.0 * detail::kPi / 2.0);
        } else {
            lineTo(x, y);
        }
        closePath();
    }

    // 闭合当前子路径（stroke 时首尾相连，fill 时视为闭合）
    void closePath() {
        if (subPathPoints_ == 0) {
            return;
        }
        path_.push_back({detail::PathCmd::Close});
        subPathPoints_ = 0;
    }

    // 用 fillStyle 填充当前路径（stencil even-odd 两遍法）
    void fill() { fillOutline(path_, fillStyle_, fillGrad_.get()); }

    // 用 strokeStyle/lineWidth 描边当前路径（复用粗线三角带）
    void stroke() { strokeOutline(path_, strokeStyle_, true, strokeGrad_.get()); }

    // 用当前路径与现有裁剪区域求交，作为新的裁剪区域（Canvas 语义）。
    // 空路径不改变裁剪；嵌套层数上限 127（超出抛 std::runtime_error）。
    // 后续所有绘制（fill/stroke/rect/text/image/像素线条）都被限制在裁剪区域内；
    // 与 save()/restore() 集成：restore 恢复保存时的裁剪深度。
    void clip() {
        const std::vector<detail::SubPath> subs = detail::flattenPath(path_);
        bool hasShape = false;
        for (const detail::SubPath& sub : subs) {
            if (sub.points.size() >= 3) {
                hasShape = true;
                break;
            }
        }
        if (!hasShape) {
            return;
        }
        if (clipDepth_ >= 0x7F) {
            throw std::runtime_error("wbwopenglapi: clip 嵌套过深（上限 127）");
        }
        ensureFrame();
        glEnable(GL_STENCIL_TEST);
        // 1. 清第 7 位（全屏，只写第 7 位=0；低 7 位深度保留）
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0x80);
        drawFullQuad();
        // 2. 裁剪区域内做 even-odd（第 7 位翻转）
        glStencilFunc(GL_EQUAL, clipDepth_, 0x7F);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        glStencilMask(0x80);
        for (const detail::SubPath& sub : subs) {
            const size_t n = sub.points.size();
            if (n < 3) {
                continue;
            }
            const detail::Vec2& p0 = sub.points[0];
            for (size_t i = 1; i + 1 < n; ++i) {
                const detail::Vec2 tris[3] = {p0, sub.points[i],
                                              sub.points[i + 1]};
                drawSolid(Color(), tris, 3, nullptr, nullptr, nullptr, false);
            }
        }
        // 3. 折叠：第 7 位=1 处 INCR（0x80|d +1 -> 低 7 位 = d+1，第 7 位暂存 1）
        glStencilFunc(GL_EQUAL, clipDepth_ | 0x80, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        glStencilMask(0xFF);
        drawFullQuad();
        // 4. 清第 7 位
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0x80);
        drawFullQuad();
        glStencilMask(0xFF);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        ++clipDepth_;
        syncClipState();
    }

    // ---------------- 文本（矢量轮廓） ----------------

// 设置字体: "16px sans-serif"（取字号，字体名忽略）或字体文件路径
    //   （FreeType 后端用文件路径或默认字体；GDI 后端始终用系统默认字体）
    //   支持 "NNpx <字体文件路径>" 组合形式
    void font(const std::string& css) {
        std::string s = css;
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) {
            return;
        }
        s = s.substr(b, e - b + 1);
        auto isPath = [](const std::string& t) {
            return t.find('/') != std::string::npos ||
                   t.find('\\') != std::string::npos ||
                   t.find(".ttf") != std::string::npos ||
                   t.find(".otf") != std::string::npos ||
                   t.find(".ttc") != std::string::npos;
        };
        std::string file;
        int size = 16;
        const size_t p = s.find("px");
        if (p != std::string::npos) {
            size_t b2 = p;
            while (b2 > 0) {
                const char c = s[b2 - 1];
                if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                    --b2;
                } else {
                    break;
                }
            }
            if (b2 < p) {
                size = static_cast<int>(std::strtod(s.substr(b2, p - b2).c_str(), nullptr));
                if (size <= 0) {
                    size = 16;
                }
                std::string rest = s.substr(p + 2);
                size_t rb = rest.find_first_not_of(" \t\r\n");
                if (rb != std::string::npos && isPath(rest.substr(rb))) {
                    file = rest.substr(rb);
                }
            }
        }
        if (file.empty() && isPath(s)) {
            file = s;
        }
        if (!fontFace_ || fontCss_ != s) {
            fontFace_ = std::make_unique<detail::FontFace>(file, size);
            glyphCache_.clear();
            glyphIndexCache_.clear();
            fontCss_ = s;
        }
    }

    // ---------------- OpenType 特性 / 连体（FreeType+HarfBuzz 生效） ----------------

    // 设置特性（CSS font-feature-settings 风格，整体替换）:
    //   fontFeatures("\"cv02\", \"zero\"") / fontFeatures("cv02, zero, ss01 0")
    // 空列表 = 全部关闭（默认；此时走逐码点渲染，与无 HarfBuzz 行为一致）
    void fontFeatures(const std::string& css) {
        std::vector<std::pair<std::string, bool>> feats;
        size_t i = 0;
        while (i <= css.size()) {
            size_t b = i;
            while (b < css.size() &&
                   (css[b] == ' ' || css[b] == '\t' || css[b] == '"' || css[b] == '\'')) {
                ++b;
            }
            size_t segEnd = b;
            while (segEnd < css.size() && css[segEnd] != ',') {
                ++segEnd;
            }
            size_t t = segEnd;
            while (t > b && (css[t - 1] == ' ' || css[t - 1] == '\t' ||
                             css[t - 1] == '"' || css[t - 1] == '\'')) {
                --t;
            }
            std::string seg = css.substr(b, t - b);
            if (!seg.empty()) {
                const size_t sp = seg.find_first_of(" \t");
                const std::string tag = sp == std::string::npos ? seg : seg.substr(0, sp);
                std::string val;
                if (sp != std::string::npos) {
                    const size_t vb = seg.find_first_not_of(" \t", sp);
                    if (vb != std::string::npos) {
                        val = seg.substr(vb);
                    }
                }
                bool on = true;
                if (val == "0" || val == "off" || val == "false") {
                    on = false;
                }
                feats.push_back({tag, on});
            }
            if (segEnd >= css.size()) {
                break;
            }
            i = segEnd + 1;
        }
        features_ = std::move(feats);
    }

    // 程序化设置特性（整体替换）
    void fontFeatures(std::initializer_list<std::pair<std::string, bool>> feats) {
        features_.assign(feats.begin(), feats.end());
    }

    // 恢复默认（全部特性关闭）
    void resetFontFeatures() { features_.clear(); }

    void textAlign(TextAlign a) { textAlign_ = a; }
    void textBaseline(TextBaseline b) { textBaseline_ = b; }

    void fillText(const std::string& text, double x, double y, double maxWidth = 0) {
        drawText(text, x, y, maxWidth, true);
    }

    void strokeText(const std::string& text, double x, double y, double maxWidth = 0) {
        drawText(text, x, y, maxWidth, false);
    }

    // 文本宽度（像素）
    double measureText(const std::string& text) const {
        if (shapingActive() && fontFace_) {
            double total = 0.0;
            for (const auto& sg : fontFace_->shape(text, features_)) {
                total += sg.advanceX;
            }
            return total / 64.0;
        }
        double total = 0.0;
        size_t i = 0;
        while (i < text.size()) {
            total += glyph(detail::utf8Next(text, i)).advanceX;
        }
        return total / 64.0;
    }

    // ---------------- 变换（Canvas 语义：后调用的变换先应用） ----------------

    void translate(double dx, double dy) {
        const float t[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                            static_cast<float>(dx), static_cast<float>(dy), 1.0f};
        multiplyCurrent(t);
    }

    // 弧度，顺时针（y 向下坐标系下的标准旋转矩阵）
    void rotate(double rad) {
        const float c = static_cast<float>(std::cos(rad));
        const float s = static_cast<float>(std::sin(rad));
        const float r[9] = {c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f};
        multiplyCurrent(r);
    }

    // 恢复单位矩阵（仅变换，样式不变）
    void resetTransform() {
        for (int i = 0; i < 9; ++i) {
            current_[i] = 0.0f;
        }
        current_[0] = current_[4] = current_[8] = 1.0f;
    }

    // 压栈：变换矩阵 + 全部样式状态
    void save() {
        StackEntry e;
        std::memcpy(e.m, current_, sizeof(current_));
        e.fillStyle = fillStyle_;
        e.strokeStyle = strokeStyle_;
        e.fillGrad = fillGrad_;
        e.strokeGrad = strokeGrad_;
        e.lineWidth = lineWidth_;
        e.globalAlpha = globalAlpha_;
        e.composite = composite_;
        e.textAlign = textAlign_;
        e.textBaseline = textBaseline_;
        e.fontCss = fontCss_;
        e.clipDepth = clipDepth_;
        stack_.push_back(e);
    }

    // 出栈恢复；无对应 save() 时抛出 std::runtime_error
    void restore() {
        if (stack_.empty()) {
            throw std::runtime_error("wbwopenglapi: restore() 无对应 save()");
        }
        const StackEntry e = stack_.back();
        stack_.pop_back();
        std::memcpy(current_, e.m, sizeof(current_));
        fillStyle_ = e.fillStyle;
        strokeStyle_ = e.strokeStyle;
        fillGrad_ = e.fillGrad;
        strokeGrad_ = e.strokeGrad;
        lineWidth_ = e.lineWidth;
        globalAlpha_ = e.globalAlpha;
        composite_ = e.composite;
        textAlign_ = e.textAlign;
        textBaseline_ = e.textBaseline;
        if (fontCss_ != e.fontCss) {
            font(e.fontCss);
        }
        if (e.clipDepth < clipDepth_) {
            // 降级：把残留深度（旧裁剪区域）降为恢复后的深度，防止与新深度冲突。
            // 用 GL_LESS：ref=target 时 pass 条件 (ref&mask) < (stencil&mask)，
            // 即 stencil 大于 target 的像素通过，REPLACE 写回 target。
            glEnable(GL_STENCIL_TEST);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glStencilFunc(GL_LESS, e.clipDepth, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glStencilMask(0xFF);
            // 规避 Intel 驱动 stencil 陈旧读：折叠写入后紧接的 stencil 测试可能读到
            // 旧值，glFlush 强制提交前序命令（合规驱动上为无操作）。
            glFlush();
            drawFullQuad();
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        clipDepth_ = e.clipDepth;
        syncClipState();
    }

    // ---------------- 图像 ----------------

    // 绘制图像；w/h 为 0 时按原尺寸（Canvas 语义）
    void drawImage(const Image& img, double x, double y, double w = 0, double h = 0) {
        if (img.width <= 0 || img.height <= 0 ||
            img.rgba.size() < static_cast<size_t>(img.width) * img.height * 4) {
            return;
        }
        const double dw = w > 0 ? w : static_cast<double>(img.width);
        const double dh = h > 0 ? h : static_cast<double>(img.height);
        // 图像行 0 = 顶行，位于纹理数据首行 = GL 纹理坐标 v=0（纹素原点在左下）。
        // 故画布顶（图像顶）对应 v=0，画布底对应 v=1。
        struct TexV {
            float px, py, u, v;
        };
        const TexV src[6] = {
            {static_cast<float>(x), static_cast<float>(y), 0.0f, 0.0f},
            {static_cast<float>(x + dw), static_cast<float>(y), 1.0f, 0.0f},
            {static_cast<float>(x + dw), static_cast<float>(y + dh), 1.0f, 1.0f},
            {static_cast<float>(x), static_cast<float>(y), 0.0f, 0.0f},
            {static_cast<float>(x + dw), static_cast<float>(y + dh), 1.0f, 1.0f},
            {static_cast<float>(x), static_cast<float>(y + dh), 0.0f, 1.0f},
        };
        // 像素坐标 -> NDC（proj_ * current_）
        float m[9];
        matMul(proj_, current_, m);
        TexV ndc[6];
        for (int i = 0; i < 6; ++i) {
            ndc[i] = src[i];
            ndc[i].px = m[0] * src[i].px + m[3] * src[i].py + m[6];
            ndc[i].py = m[1] * src[i].px + m[4] * src[i].py + m[7];
        }
        ensureFrame(); // 抗锯齿模式：首次绘制切换离屏 FBO
        applyClipGuard();
        applyComposite();
        texProgram_->use();
        glUniform4f(texProgram_->uniform("u_color"), 1.0f, 1.0f, 1.0f,
                    static_cast<float>(globalAlpha_));
        glActiveTexture(GL_TEXTURE0);
        tex_->upload(img.width, img.height, img.rgba.data());
        glUniform1i(texProgram_->uniform("u_tex"), 0);
        texVao_->bind();
        texVbo_->upload(ndc, static_cast<GLsizeiptr>(sizeof(ndc)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

private:
    // 纹理通道顶点（pos + uv 交错）
    struct TexVertex {
        float px, py, u, v;
    };

    // save() 保存的完整状态（变换 + 样式）
    struct StackEntry {
        float m[9];
        Color fillStyle;
        Color strokeStyle;
        std::shared_ptr<Gradient> fillGrad;
        std::shared_ptr<Gradient> strokeGrad;
        double lineWidth = 1.0;
        double globalAlpha = 1.0;
        std::string composite = "source-over";
        TextAlign textAlign = TextAlign::Left;
        TextBaseline textBaseline = TextBaseline::Alphabetic;
        std::string fontCss;
        int clipDepth = 0;
    };

    // 列主序 mat3 乘法: out = a * b
    static void matMul(const float* a, const float* b, float* out) {
        for (int cc = 0; cc < 3; ++cc) {
            for (int rr = 0; rr < 3; ++rr) {
                out[cc * 3 + rr] = a[0 * 3 + rr] * b[cc * 3 + 0] +
                                   a[1 * 3 + rr] * b[cc * 3 + 1] +
                                   a[2 * 3 + rr] * b[cc * 3 + 2];
            }
        }
    }

    // 列主序 mat3 伴随矩阵法求逆（仿射可逆矩阵）；退化返回 false
    static bool mat3Inverse(const float* m, float* out) {
        const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
                          m[3] * (m[1] * m[8] - m[2] * m[7]) +
                          m[6] * (m[1] * m[5] - m[2] * m[4]);
        if (std::abs(det) < 1e-12f) {
            return false;
        }
        const float inv = 1.0f / det;
        out[0] = (m[4] * m[8] - m[7] * m[5]) * inv;
        out[1] = -(m[1] * m[8] - m[7] * m[2]) * inv;
        out[2] = (m[1] * m[5] - m[4] * m[2]) * inv;
        out[3] = -(m[3] * m[8] - m[6] * m[5]) * inv;
        out[4] = (m[0] * m[8] - m[6] * m[2]) * inv;
        out[5] = -(m[0] * m[5] - m[3] * m[2]) * inv;
        out[6] = (m[3] * m[7] - m[6] * m[4]) * inv;
        out[7] = -(m[0] * m[7] - m[6] * m[1]) * inv;
        out[8] = (m[0] * m[4] - m[3] * m[1]) * inv;
        return true;
    }

    // current_ = current_ * b（右乘：后调用的变换先应用）
    void multiplyCurrent(const float* b) {
        float out[9];
        matMul(current_, b, out);
        std::memcpy(current_, out, sizeof(out));
    }

    // solid 通道着色器（阶段 5 增加纹理通道）。
    // 顶点在 CPU 端已变换为 NDC（-1..1），shader 直写：
    // 该驱动（Intel UHD 630 Build 27.20.100.9168）对 GL_FLOAT attribute
    // 的大数值（如像素坐标 40/200）读取异常，且 mat3 uniform 变换不可靠，
    // 故统一在 CPU 完成变换，属性值恒为 NDC 小数值。
    static constexpr const char* kSolidVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";
    static constexpr const char* kSolidFS = R"GLSL(
#version 330 core
uniform vec4 u_color;
out vec4 frag;
void main() {
    frag = u_color;
}
)GLSL";

    // 纹理通道着色器（drawImage；uv 由 CPU 端按图像行序翻转）
    static constexpr const char* kTexVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)GLSL";
    static constexpr const char* kTexFS = R"GLSL(
#version 330 core
uniform sampler2D u_tex;
uniform vec4 u_color;
in vec2 v_uv;
out vec4 frag;
void main() {
    frag = texture(u_tex, v_uv) * u_color;
}
)GLSL";

    // 像素级线条通道着色器（位置 + 每像素覆盖度 alpha 交错顶点）
    static constexpr const char* kPixelVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in float a_alpha;
out float v_alpha;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_alpha = a_alpha;
}
)GLSL";
    static constexpr const char* kPixelFS = R"GLSL(
#version 330 core
uniform vec4 u_color;
in float v_alpha;
out vec4 frag;
void main() {
    frag = vec4(u_color.rgb, u_color.a * v_alpha);
}
)GLSL";

    // 渐变通道着色器：顶点仍为 CPU 变换后的 NDC（复用 solid 的 VAO/VBO）。
    // 用户空间坐标由 gl_FragCoord -> NDC -> u_invM 恢复（仿射变换下与顶点
    // 属性插值等价，且 fill 的 stencil Pass 2 全屏四边形也能正确取色）。
    static constexpr const char* kGradVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";
    static constexpr const char* kGradFS = R"GLSL(
#version 330 core
uniform vec4 u_color;          // rgb=1，a=globalAlpha
uniform mat3 u_invM;           // 列主序（NDC -> 用户坐标）
uniform vec2 u_viewport;       // 当前视口尺寸（gl_FragCoord 归一化）
uniform int u_gradType;        // 0=线性 1=径向
uniform vec2 u_p0;
uniform vec2 u_p1;
uniform float u_r0;
uniform float u_r1;
uniform int u_stopCount;
uniform vec4 u_stops[8];       // rgba
uniform float u_offsets[8];
out vec4 frag;

vec4 sampleStops(float t) {
    int n = u_stopCount;
    if (n <= 1) {
        return u_stops[0];
    }
    if (t <= u_offsets[0]) {
        return u_stops[0];
    }
    if (t >= u_offsets[n - 1]) {
        return u_stops[n - 1];
    }
    for (int i = 1; i < n; ++i) {
        if (t <= u_offsets[i]) {
            float lo = u_offsets[i - 1];
            float hi = u_offsets[i];
            float k = (hi > lo) ? (t - lo) / (hi - lo) : 0.0;
            return mix(u_stops[i - 1], u_stops[i], k);
        }
    }
    return u_stops[n - 1];
}

void main() {
    // gl_FragCoord.y 自下而上与 NDC +y 同向，直接归一化（无需翻转）
    vec2 ndc = vec2(gl_FragCoord.x / u_viewport.x * 2.0 - 1.0,
                    gl_FragCoord.y / u_viewport.y * 2.0 - 1.0);
    vec3 p3 = u_invM * vec3(ndc, 1.0);
    vec2 pos = vec2(p3.x / p3.z, p3.y / p3.z);
    float t = 0.0;
    if (u_gradType == 0) {
        // 线性：沿 p0->p1 的投影
        vec2 d = u_p1 - u_p0;
        float len2 = dot(d, d);
        t = len2 > 1e-12 ? clamp(dot(pos - u_p0, d) / len2, 0.0, 1.0) : 0.0;
    } else {
        // 径向：两圆焦点 |p - (p0 + t*d)| = r0 + t*dr 的二次方程
        vec2 d = u_p1 - u_p0;
        float dr = u_r1 - u_r0;
        vec2 f = pos - u_p0;
        float A = dot(d, d) - dr * dr;
        float B = 2.0 * (dot(f, d) + u_r0 * dr);
        float C = dot(f, f) - u_r0 * u_r0;
        t = 1.0;
        if (abs(A) > 1e-12) {
            float disc = B * B - 4.0 * A * C;
            if (disc >= 0.0) {
                float sq = sqrt(disc);
                float t1 = (B - sq) / (2.0 * A);
                float t2 = (B + sq) / (2.0 * A);
                float lo = min(t1, t2);
                float hi = max(t1, t2);
                t = lo;
                if (t < 0.0) {
                    t = hi;
                }
                if (t < 0.0) {
                    t = 0.0; // 两根皆负：位于内圆内部
                }
            } else {
                // 无交点：内圆内 -> 0，否则（外圆外）-> 1
                t = (dot(f, f) < u_r0 * u_r0) ? 0.0 : 1.0;
            }
        } else if (abs(B) > 1e-12) {
            t = C / B;
        }
        t = clamp(t, 0.0, 1.0);
    }
    vec4 col = sampleStops(t);
    frag = vec4(col.rgb, col.a) * u_color;
}
)GLSL";

    // 逻辑像素 -> framebuffer 像素（HiDPI 缩放；宽高为 0 时退化 1:1）
    float logicalToFbX(double x) const {
        int fw = window_.framebufferWidth();
        int ww = window_.width();
        return ww > 0 ? static_cast<float>(x * fw / ww) : static_cast<float>(x);
    }
    float logicalToFbY(double y) const {
        int fh = window_.framebufferHeight();
        int wh = window_.height();
        return wh > 0 ? static_cast<float>(y * fh / wh) : static_cast<float>(y);
    }

    // 以 framebuffer 尺寸刷新视口与投影矩阵（y 向下，原点左上）。
    // 抗锯齿模式下视口按当前绘制目标（离屏 FBO 或默认 framebuffer）的尺寸，
    // 但投影恒按逻辑画布尺寸（framebuffer 尺寸）——SSAA 离屏为 2x 时，
    // 内容仍按逻辑坐标布局，由视口放大采样（否则内容会缩小 2 倍）。
    // proj_ 按列主序存储 mat3：col0=(2/fw,0,0) col1=(0,-2/fh,0) col2=(-1,1,1)
    void updateViewport() {
        const int fw = window_.framebufferWidth();
        const int fh = window_.framebufferHeight();
        if (fw <= 0 || fh <= 0) {
            return;
        }
        const int vw = curTargetW_ > 0 ? curTargetW_ : fw;
        const int vh = curTargetH_ > 0 ? curTargetH_ : fh;
        glViewport(0, 0, vw, vh);
        proj_[0] = 2.0f / fw;
        proj_[1] = 0.0f;
        proj_[2] = 0.0f;
        proj_[3] = 0.0f;
        proj_[4] = -2.0f / fh;
        proj_[5] = 0.0f;
        proj_[6] = -1.0f;
        proj_[7] = 1.0f;
        proj_[8] = 1.0f;
    }

    // 惰性创建渐变通道（复用 solid 的 VAO/VBO，顶点仍是 NDC Vec2）
    void ensureGradPipeline() {
        if (!gradProgram_) {
            gradProgram_ = std::make_unique<detail::Program>(kGradVS, kGradFS);
        }
    }

    // 设置渐变 uniform（invM 为 NDC -> 用户坐标的逆矩阵，列主序）
    void setGradUniforms(const Gradient& g, const float* invM, float alpha) {
        glUniform4f(gradProgram_->uniform("u_color"), 1.0f, 1.0f, 1.0f, alpha);
        glUniformMatrix3fv(gradProgram_->uniform("u_invM"), 1, GL_FALSE, invM);
        const int vw = curTargetW_ > 0 ? curTargetW_ : window_.framebufferWidth();
        const int vh = curTargetH_ > 0 ? curTargetH_ : window_.framebufferHeight();
        glUniform2f(gradProgram_->uniform("u_viewport"),
                    static_cast<float>(vw), static_cast<float>(vh));
        glUniform1i(gradProgram_->uniform("u_gradType"), g.radial ? 1 : 0);
        glUniform2f(gradProgram_->uniform("u_p0"),
                    static_cast<float>(g.x0), static_cast<float>(g.y0));
        glUniform2f(gradProgram_->uniform("u_p1"),
                    static_cast<float>(g.x1), static_cast<float>(g.y1));
        glUniform1f(gradProgram_->uniform("u_r0"), static_cast<float>(g.r0));
        glUniform1f(gradProgram_->uniform("u_r1"), static_cast<float>(g.r1));
        const size_t n = g.stops.size() > 8 ? 8 : g.stops.size();
        glUniform1i(gradProgram_->uniform("u_stopCount"),
                    static_cast<GLint>(n));
        float stops[8 * 4] = {};
        float offs[8] = {};
        for (size_t i = 0; i < n; ++i) {
            stops[i * 4 + 0] = g.stops[i].second.r;
            stops[i * 4 + 1] = g.stops[i].second.g;
            stops[i * 4 + 2] = g.stops[i].second.b;
            stops[i * 4 + 3] = g.stops[i].second.a;
            offs[i] = static_cast<float>(g.stops[i].first);
        }
        glUniform4fv(gradProgram_->uniform("u_stops"), 8, stops);
        glUniform1fv(gradProgram_->uniform("u_offsets"), 8, offs);
    }

    // stencil 位分配：低 7 位 = clip 深度（0..127），第 7 位 = even-odd 临时位。
    // 裁剪区域像素的低 7 位 == clipDepth_；无裁剪时 clipDepth_ == 0。

    // 帧首/恢复时同步 stencil 开关：无裁剪禁用测试，有裁剪启用并限制到当前深度
    void syncClipState() {
        if (clipDepth_ == 0) {
            glDisable(GL_STENCIL_TEST);
        } else {
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);
            glStencilFunc(GL_EQUAL, clipDepth_, 0x7F);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }
    }

    // 绘制前把 stencil 限制到当前裁剪区域（无裁剪时保持 GL 现状）
    void applyClipGuard() {
        if (clipDepth_ > 0) {
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);
            glStencilFunc(GL_EQUAL, clipDepth_, 0x7F);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }
    }

    // 按当前合成模式设置混合因子（source-over = 直通 alpha，与历史行为一致）。
    // 直通 alpha 存储下 GL 混合方程：C = Cs*Fs + Cd*Fd，A = As*Fs + Ad*Fd。
    //   source-over        (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
    //   source-in          (DST_ALPHA, ZERO)
    //   source-out         (ONE_MINUS_DST_ALPHA, ZERO)
    //   source-atop        (DST_ALPHA, ONE_MINUS_SRC_ALPHA)
    //   destination-over   (ONE_MINUS_DST_ALPHA, ONE)
    //   destination-in     (ZERO, SRC_ALPHA)
    //   destination-out    (ZERO, ONE_MINUS_SRC_ALPHA)
    //   destination-atop   (ONE_MINUS_DST_ALPHA, SRC_ALPHA)
    //   lighter            (ONE, ONE)
    //   copy               (ONE, ZERO)
    //   xor                (ONE_MINUS_DST_ALPHA, ONE_MINUS_SRC_ALPHA)
    //   multiply           (DST_COLOR, ZERO)
    void applyComposite() {
        glEnable(GL_BLEND);
        const std::string& m = composite_;
        if (m == "source-in") {
            glBlendFunc(GL_DST_ALPHA, GL_ZERO);
        } else if (m == "source-out") {
            glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ZERO);
        } else if (m == "source-atop") {
            glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (m == "destination-over") {
            glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE);
        } else if (m == "destination-in") {
            glBlendFunc(GL_ZERO, GL_SRC_ALPHA);
        } else if (m == "destination-out") {
            glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        } else if (m == "destination-atop") {
            glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA);
        } else if (m == "lighter") {
            glBlendFunc(GL_ONE, GL_ONE);
        } else if (m == "copy") {
            glBlendFunc(GL_ONE, GL_ZERO);
        } else if (m == "xor") {
            glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (m == "multiply") {
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
        } else { // source-over
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    // 全屏 NDC 四边形（单位矩阵；调用方需先配置好 stencil 状态）
    void drawFullQuad(const Color& c = Color(0.0f, 0.0f, 0.0f, 1.0f),
                      const Gradient* grad = nullptr) {
        const detail::Vec2 quad[6] = {
            {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
            {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f},
        };
        if (grad) {
            float m2[9], invM[9];
            matMul(proj_, current_, m2);
            if (mat3Inverse(m2, invM)) {
                drawSolid(c, quad, 6, kIdentity, grad, invM, false);
            } else {
                drawSolid(c, quad, 6, kIdentity, nullptr, nullptr, false);
            }
        } else {
            drawSolid(c, quad, 6, kIdentity, nullptr, nullptr, false);
        }
    }

    // 以指定颜色绘制三角形列表（含 globalAlpha）。
    // 顶点在 CPU 变换为 NDC 后上传（见 kSolidVS 注释：驱动 attribute/矩阵限制）。
    // mat 为 nullptr 时使用当前 proj_（像素坐标 -> NDC），
    // 否则使用给定矩阵（列主序 mat3，如 fill() 的全屏四边形用单位矩阵）。
    // grad 非空时走渐变通道：invM 为 NDC -> 用户坐标逆矩阵（nullptr 时自动求
    // 解 proj_*current_ 之逆；mat 非空且需不同逆矩阵时必须显式传入）。
    // clipGuard 为 true（默认）时若存在裁剪区域（clipDepth_>0），先启用 stencil
    // 限制到当前裁剪区域；fillOutline 内部自行管理 stencil 时传 false。
    void drawSolid(const Color& c, const detail::Vec2* verts, size_t count,
                   const float* mat = nullptr, const Gradient* grad = nullptr,
                   const float* invM = nullptr, bool clipGuard = true) {
        ensureFrame(); // 抗锯齿模式：首次绘制切换离屏 FBO（本帧内只做一次）
        if (clipGuard) {
            applyClipGuard();
        }
        applyComposite();
        // mat 非空 = NDC 空间直写（如 stencil 全屏 quad），不受当前变换影响；
        // mat 为空 = 像素空间，顶点经当前变换（current_）后再投影（proj_）。
        float m[9];
        if (mat) {
            std::memcpy(m, mat, sizeof(m));
        } else {
            matMul(proj_, current_, m); // m = proj_ * current_
        }
        std::vector<detail::Vec2> ndc(count);
        for (size_t i = 0; i < count; ++i) {
            ndc[i].x = m[0] * verts[i].x + m[3] * verts[i].y + m[6];
            ndc[i].y = m[1] * verts[i].x + m[4] * verts[i].y + m[7];
        }
        if (grad) {
            ensureGradPipeline();
            float im[9];
            const float* useInv = invM;
            if (!useInv && mat3Inverse(m, im)) {
                useInv = im;
            }
            gradProgram_->use();
            setGradUniforms(*grad, useInv ? useInv : kIdentity,
                            static_cast<float>(globalAlpha_));
            vao_->bind();
            vbo_->upload(ndc.data(),
                         static_cast<GLsizeiptr>(ndc.size() * sizeof(detail::Vec2)));
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
            glBindVertexArray(0);
            return;
        }
        program_->use();
        glUniform4f(program_->uniform("u_color"), c.r, c.g, c.b,
                    c.a * static_cast<float>(globalAlpha_));
        vao_->bind();
        vbo_->upload(ndc.data(), static_cast<GLsizeiptr>(ndc.size() * sizeof(detail::Vec2)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
        glBindVertexArray(0);
    }

    // stencil even-odd 两遍填充任意路径命令序列（grad 非空时渐变着色）。
    // 与 clip 兼容：even-odd 临时位使用第 7 位，仅在裁剪区域（低 7 位==clipDepth_）
    // 内生效，绘制后清理第 7 位并保留低 7 位深度。
    void fillOutline(const std::vector<detail::PathSeg>& segs, const Color& c,
                     const Gradient* grad = nullptr) {
        const std::vector<detail::SubPath> subs = detail::flattenPath(segs);
        bool hasShape = false;
        for (const detail::SubPath& sub : subs) {
            if (sub.points.size() >= 3) {
                hasShape = true;
                break;
            }
        }
        if (!hasShape) {
            return;
        }
        // Pass 1: 轮廓三角扇写入第 7 位（GL_INVERT -> even-odd），仅限裁剪区域
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0x80);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_EQUAL, clipDepth_, 0x7F);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        for (const detail::SubPath& sub : subs) {
            const size_t n = sub.points.size();
            if (n < 3) {
                continue;
            }
            const detail::Vec2& p0 = sub.points[0];
            for (size_t i = 1; i + 1 < n; ++i) {
                const detail::Vec2 tris[3] = {p0, sub.points[i],
                                              sub.points[i + 1]};
                drawSolid(c, tris, 3, nullptr, nullptr, nullptr, false);
            }
        }
        // Pass 2: 裁剪区 ∩ 路径内部（第 7 位=1）上色
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilFunc(GL_EQUAL, clipDepth_ | 0x80, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        drawFullQuad(c, grad);
        // 清理第 7 位（保留低 7 位 clip 深度）
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilMask(0x80);
        drawFullQuad();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0xFF);
        syncClipState();
    }

    // ---------------- 像素级线条（lineAlgorithm != "default"） ----------------

    // 惰性创建像素通道（着色器 + 位置/alpha 交错的 VAO/VBO）
    void ensurePixelPipeline() {
        if (pixelProgram_) {
            return;
        }
        pixelProgram_ = std::make_unique<detail::Program>(kPixelVS, kPixelFS);
        pixelVao_ = std::make_unique<detail::VertexArray>();
        pixelVbo_ = std::make_unique<detail::VertexBuffer>();
        pixelVao_->bind();
        pixelVbo_->bind();
        const GLsizei stride = 3 * static_cast<GLsizei>(sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    // 按当前 lineAlgorithm_ 把路径折线光栅化为覆盖像素并绘制。
    // 折线顶点先经当前变换（current_）映射到画布坐标，再按逻辑像素网格选点；
    // 每覆盖像素生成 1x1 四边形（Wu 相邻像素带 alpha 渐变），
    // 顶点已处于画布像素空间，直接乘 proj_ 变换到 NDC。
    void strokePixels(const std::vector<detail::PathSeg>& segs, const Color& c) {
        const std::vector<detail::SubPath> subs = detail::flattenPath(segs);
        std::vector<detail::PixelRun> runs;
        const float* m = current_;
        for (const detail::SubPath& sub : subs) {
            const size_t n = sub.points.size();
            if (n < 2) {
                continue;
            }
            const size_t segN = sub.closed ? n : n - 1;
            for (size_t i = 0; i < segN; ++i) {
                const detail::Vec2& a = sub.points[i % n];
                const detail::Vec2& b = sub.points[(i + 1) % n];
                const double ax = m[0] * a.x + m[3] * a.y + m[6];
                const double ay = m[1] * a.x + m[4] * a.y + m[7];
                const double bx = m[0] * b.x + m[3] * b.y + m[6];
                const double by = m[1] * b.x + m[4] * b.y + m[7];
                switch (lineAlgorithm_) {
                    case detail::LineAlgo::Dda:
                        detail::rasterDDA(ax, ay, bx, by, runs);
                        break;
                    case detail::LineAlgo::Bresenham:
                        detail::rasterBresenham(
                            static_cast<int>(std::floor(ax + 0.5)),
                            static_cast<int>(std::floor(ay + 0.5)),
                            static_cast<int>(std::floor(bx + 0.5)),
                            static_cast<int>(std::floor(by + 0.5)), runs);
                        break;
                    case detail::LineAlgo::Wu:
                        detail::rasterWu(ax, ay, bx, by, runs);
                        break;
                    default:
                        return; // "default" 不会进入本函数
                }
            }
        }
        if (runs.empty()) {
            return;
        }
        // 去重（相邻段共享端点）：按 y,x 排序，同像素保留最大覆盖度
        std::sort(runs.begin(), runs.end(),
                  [](const detail::PixelRun& p, const detail::PixelRun& q) {
                      return p.y != q.y ? p.y < q.y : p.x < q.x;
                  });
        std::vector<detail::PixelRun> uniq;
        uniq.reserve(runs.size());
        for (const detail::PixelRun& r : runs) {
            if (!uniq.empty() && uniq.back().x == r.x && uniq.back().y == r.y) {
                if (r.alpha > uniq.back().alpha) {
                    uniq.back().alpha = r.alpha;
                }
            } else {
                uniq.push_back(r);
            }
        }
        drawPixels(c, uniq);
    }

    // 把覆盖像素列表绘制为 1x1 四边形（含每像素覆盖度 alpha），
    // 颜色 alpha = 样式 alpha * globalAlpha * 覆盖度
    void drawPixels(const Color& c, const std::vector<detail::PixelRun>& runs) {
        if (runs.empty()) {
            return;
        }
        ensurePixelPipeline();
        ensureFrame(); // 抗锯齿模式：离屏绘制
        applyClipGuard();
        applyComposite();
        const int fw = curTargetW_ > 0 ? curTargetW_ : window_.framebufferWidth();
        const int fh = curTargetH_ > 0 ? curTargetH_ : window_.framebufferHeight();
        struct PVertex {
            float x, y, a;
        };
        std::vector<PVertex> verts;
        verts.reserve(runs.size() * 6);
        for (const detail::PixelRun& r : runs) {
            if (r.x < 0 || r.y < 0 || r.x >= fw || r.y >= fh) {
                continue;
            }
            const float x0 = static_cast<float>(r.x);
            const float y0 = static_cast<float>(r.y);
            const float x1 = x0 + 1.0f;
            const float y1 = y0 + 1.0f;
            const float a = r.alpha;
            verts.push_back({x0, y0, a});
            verts.push_back({x1, y0, a});
            verts.push_back({x1, y1, a});
            verts.push_back({x0, y0, a});
            verts.push_back({x1, y1, a});
            verts.push_back({x0, y1, a});
        }
        if (verts.empty()) {
            return;
        }
        // 画布像素坐标 -> NDC（proj_）
        const float* m = proj_;
        for (PVertex& v : verts) {
            const float ox = v.x, oy = v.y;
            v.x = m[0] * ox + m[3] * oy + m[6];
            v.y = m[1] * ox + m[4] * oy + m[7];
        }
        pixelProgram_->use();
        glUniform4f(pixelProgram_->uniform("u_color"), c.r, c.g, c.b,
                    c.a * static_cast<float>(globalAlpha_));
        pixelVao_->bind();
        pixelVbo_->upload(verts.data(),
                          static_cast<GLsizeiptr>(verts.size() * sizeof(PVertex)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 3));
        glBindVertexArray(0);
    }

    // ---------------- 抗锯齿管线（antialias()） ----------------

    // 帧内首个绘制调用前：切换离屏 FBO（只执行一次/帧）。
    // off 模式同样经 1x 离屏 FBO，保证 present()/resolve() 后默认
    // framebuffer 即当前帧内容（读回/合成语义与 AA 模式一致）。
    void ensureFrame() {
        if (!aaDirty_) {
            return;
        }
        aaDirty_ = false;
        beginFrame();
    }

    // 绑定离屏 FBO 并按其尺寸设置视口/投影（含 FBO 惰性创建与 resize 重建）
    void beginFrame() {
        const int fw = window_.framebufferWidth();
        const int fh = window_.framebufferHeight();
        const int scale = antialias_ == "ssaa" ? 2 : 1;
        const int samples = antialias_ == "msaa" ? 4 : 0;
        if (!aaFbo_ || aaFbo_->width() != fw * scale ||
            aaFbo_->height() != fh * scale || aaFbo_->samples() != samples) {
            aaFbo_ = std::make_unique<detail::FrameBuffer>(fw * scale, fh * scale,
                                                           samples);
        }
        curTargetW_ = fw * scale;
        curTargetH_ = fh * scale;
        aaFbo_->bindDraw();
        glViewport(0, 0, fw * scale, fh * scale);
        updateViewport();
        clipDepth_ = 0; // 新帧：stencil 已清零，裁剪深度归零
        // 帧首清空 stencil（裁剪区域随帧失效）
        // 每帧起始清空 stencil（clip 深度从 0 开始；fill 不再清整块 stencil）
        glClear(GL_STENCIL_BUFFER_BIT);
    }

    // 帧末：按模式把离屏结果 resolve 到默认 framebuffer。
    // 由 Window::swapBuffers() 在交换缓冲前自动调用
    void present() {
        if (!aaFbo_) {
            aaDirty_ = true;
            return;
        }
        const int fw = window_.framebufferWidth();
        const int fh = window_.framebufferHeight();
        aaFbo_->bindRead();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        if (antialias_ == "off") {
            // 1x 离屏 → 默认 framebuffer（1:1 拷贝，保持硬边）
            glBlitFramebuffer(0, 0, fw, fh, 0, 0, fw, fh,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        } else if (antialias_ == "ssaa" || antialias_ == "msaa") {
            // SSAA：2x 缩小采样（GL_LINEAR）；MSAA：多重采样自动 resolve
            glBlitFramebuffer(0, 0, aaFbo_->width(), aaFbo_->height(), 0, 0, fw, fh,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
        } else {
            // FXAA / MLAA 后处理（全屏四边形，复用纹理通道 VAO）
            // 后处理四边形必须直写窗口（不参与合成混合；下一帧首个绘制会经
            // applyComposite() 重新开启并设置因子）
            glDisable(GL_BLEND);
            ensurePostPipeline();
            glViewport(0, 0, fw, fh);
            if (antialias_ == "fxaa") {
                fxaaProg_->use();
                glUniform2f(fxaaProg_->uniform("u_texel"), 1.0f / fw, 1.0f / fh);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, aaFbo_->colorTexture());
                glUniform1i(fxaaProg_->uniform("u_tex"), 0);
                drawPostQuad();
            } else { // mlaa
                if (!mlaaFbo_ || mlaaFbo_->width() != fw || mlaaFbo_->height() != fh) {
                    mlaaFbo_ =
                        std::make_unique<detail::FrameBuffer>(fw, fh, 0);
                }
                // pass1：亮度梯度边缘图 -> mlaaFbo_
                mlaaFbo_->bindBoth();
                mlaaEdgeProg_->use();
                glUniform2f(mlaaEdgeProg_->uniform("u_texel"), 1.0f / fw, 1.0f / fh);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, aaFbo_->colorTexture());
                glUniform1i(mlaaEdgeProg_->uniform("u_tex"), 0);
                drawPostQuad();
                // pass2：沿边缘方向混合 -> 默认 framebuffer
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                mlaaSmoothProg_->use();
                glUniform2f(mlaaSmoothProg_->uniform("u_texel"), 1.0f / fw, 1.0f / fh);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, aaFbo_->colorTexture());
                glUniform1i(mlaaSmoothProg_->uniform("u_color"), 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, mlaaFbo_->colorTexture());
                glUniform1i(mlaaSmoothProg_->uniform("u_edge"), 1);
                drawPostQuad();
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fw, fh);
        curTargetW_ = 0;
        curTargetH_ = 0;
        updateViewport();
        aaDirty_ = true;
    }

    // 惰性创建 FXAA/MLAA 后处理程序
    void ensurePostPipeline() {
        if (fxaaProg_) {
            return;
        }
        fxaaProg_ = std::make_unique<detail::Program>(kTexVS, detail::kFxaaFS);
        mlaaEdgeProg_ = std::make_unique<detail::Program>(kTexVS, detail::kMlaaEdgeFS);
        mlaaSmoothProg_ = std::make_unique<detail::Program>(kTexVS, detail::kMlaaSmoothFS);
    }

    // 全屏 NDC 四边形（uv 0..1；v=0 对应纹理底部 = 窗口 y=0 行，无需翻转）
    void drawPostQuad() {
        const TexVertex quad[6] = {
            {-1.0f, -1.0f, 0.0f, 0.0f}, {1.0f, -1.0f, 1.0f, 0.0f},
            {1.0f, 1.0f, 1.0f, 1.0f},   {-1.0f, -1.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 1.0f, 1.0f},   {-1.0f, 1.0f, 0.0f, 1.0f},
        };
        texVao_->bind();
        texVbo_->upload(quad, static_cast<GLsizeiptr>(sizeof(quad)));
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    // 描边任意路径命令序列（逐子路径）。
    // pixelOk=false 时强制走矢量三角带（strokeText 字体轮廓用，避免像素化）
void strokeOutline(const std::vector<detail::PathSeg>& segs, const Color& c,
                   bool pixelOk = true, const Gradient* grad = nullptr) {
        if (pixelOk && lineAlgorithm_ != detail::LineAlgo::Default) {
            strokePixels(segs, c);
            return;
        }
        float invM[9];
        bool haveInv = false;
        if (grad) {
            float m2[9];
            matMul(proj_, current_, m2);
            haveInv = mat3Inverse(m2, invM);
        }
        const std::vector<detail::SubPath> subs = detail::flattenPath(segs);
        for (const detail::SubPath& sub : subs) {
            if (sub.points.size() < 2) {
                continue;
            }
            std::vector<detail::Vec2> strip = detail::buildStrokeStrip(
                sub.points, sub.closed, static_cast<float>(lineWidth_));
            if (!strip.empty()) {
                drawSolid(c, strip.data(), strip.size(), nullptr, grad,
                          haveInv ? invM : nullptr);
            }
        }
    }

    // 加载并缓存字形（键为码点；font() 改变字号时清缓存）
    detail::Glyph glyph(uint32_t cp) const {
        auto it = glyphCache_.find(cp);
        if (it != glyphCache_.end()) {
            return it->second;
        }
        detail::Glyph g;
        if (fontFace_) {
            g = fontFace_->loadGlyph(cp);
        }
        glyphCache_.emplace(cp, g);
        return g;
    }

    // 按字形索引加载并缓存（HarfBuzz 整形路径；特性已编码进索引，无需清缓存）
    detail::Glyph glyphIndex(uint32_t idx) const {
        auto it = glyphIndexCache_.find(idx);
        if (it != glyphIndexCache_.end()) {
            return it->second;
        }
        detail::Glyph g;
        if (fontFace_) {
            g = fontFace_->loadGlyphIndex(idx);
        }
        glyphIndexCache_.emplace(idx, g);
        return g;
    }

    // 整形是否生效（FreeType+HarfBuzz 编译配置 且 显式设置了特性）
    bool shapingActive() const {
#if defined(WBWOPENGAL_API_FONT_FREETYPE) && defined(WBWOPENGAL_API_FONT_HARFBUZZ)
        return !features_.empty();
#else
        return false;
#endif
    }

    // 文本对齐偏移（像素）
    double alignOffset(double w) const {
        switch (textAlign_) {
        case TextAlign::Center:
            return w / 2.0;
        case TextAlign::Right:
            return w;
        default:
            return 0.0;
        }
    }

    // 文本基线到字形基线的偏移（像素；字形基线即 fillText 的 y 参数）
    double baselineOffset() const {
        const double asc = fontFace_ ? fontFace_->ascender() / 64.0 : 0.0;
        const double desc = fontFace_ ? fontFace_->descender() / 64.0 : 0.0; // 负
        switch (textBaseline_) {
        case TextBaseline::Top:
            return asc;
        case TextBaseline::Middle:
            return (asc + desc) / 2.0;
        case TextBaseline::Bottom:
            return desc;
        default: // Alphabetic
            return 0.0;
        }
    }

    // 文本绘制（fill=true 用 fillStyle，否则 strokeStyle/lineWidth）
    void drawText(const std::string& text, double x, double y, double maxWidth,
                  bool fill) {
        if (!fontFace_) {
            font("");
        }
        // 整形路径（FreeType+HarfBuzz 且显式设置了特性；否则回退逐码点）
        std::vector<detail::ShapedGlyph> shaped;
        double total = 0.0;
        if (shapingActive() && fontFace_) {
            shaped = fontFace_->shape(text, features_);
            for (const auto& sg : shaped) {
                total += sg.advanceX;
            }
            total /= 64.0;
        } else {
            total = measureText(text);
        }
        double scale = 1.0;
        if (maxWidth > 0.0 && total > maxWidth) {
            scale = maxWidth / total; // Canvas 语义: 超宽压缩
        }
        const double tx = x - alignOffset(total * scale);
        const double ty = y + baselineOffset();
        // 绘制单个字形（ox/oy 为像素偏移；字形空间 1/64 像素、y 向上）
        auto drawGlyphAt = [&](const detail::Glyph& g, double ox, double oy) {
            if (g.outline.empty()) {
                return;
            }
            std::vector<detail::PathSeg> segs;
            segs.reserve(g.outline.size());
            for (const detail::PathSeg& s : g.outline) {
                detail::PathSeg t = s;
                t.x1 = static_cast<float>(ox + s.x1 * scale / 64.0);
                t.y1 = static_cast<float>(ty - s.y1 * scale / 64.0 - oy);
                t.x2 = static_cast<float>(ox + s.x2 * scale / 64.0);
                t.y2 = static_cast<float>(ty - s.y2 * scale / 64.0 - oy);
                t.x3 = static_cast<float>(ox + s.x3 * scale / 64.0);
                t.y3 = static_cast<float>(ty - s.y3 * scale / 64.0 - oy);
                segs.push_back(t);
            }
            if (fill) {
                fillOutline(segs, fillStyle_, fillGrad_.get());
            } else {
                strokeOutline(segs, strokeStyle_, false, strokeGrad_.get()); // 字体轮廓始终矢量描边
            }
        };
        if (!shaped.empty()) {
            double cur = 0.0; // 光标 x 偏移（1/64 像素单位，随 scale 缩放）
            for (const auto& sg : shaped) {
                drawGlyphAt(glyphIndex(sg.index),
                            tx + (cur + sg.dx) * scale / 64.0,
                            sg.dy * scale / 64.0);
                cur += sg.advanceX;
            }
            return;
        }
        double cur = 0.0; // 光标 x 偏移（1/64 像素单位，随 scale 缩放）
        size_t i = 0;
        while (i < text.size()) {
            const detail::Glyph g = glyph(detail::utf8Next(text, i));
            drawGlyphAt(g, tx + cur * scale / 64.0, 0.0);
            cur += g.advanceX;
        }
    }

    Window& window_;
    std::unique_ptr<detail::Program> program_;
    std::unique_ptr<detail::VertexArray> vao_;
    std::unique_ptr<detail::VertexBuffer> vbo_;
    float proj_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // 单位矩阵（列主序），用于已处于 NDC 空间的顶点（fill 的全屏四边形）
    static constexpr float kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    // 当前变换（列主序 3x3 仿射，像素坐标空间；初始单位矩阵）
    float current_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<StackEntry> stack_;

    // 纹理通道（drawImage）
    std::unique_ptr<detail::Program> texProgram_;
    std::unique_ptr<detail::VertexArray> texVao_;
    std::unique_ptr<detail::VertexBuffer> texVbo_;
    std::unique_ptr<detail::Texture> tex_;

    // 渐变通道（复用 solid 的 vao_/vbo_）
    std::unique_ptr<detail::Program> gradProgram_;

    Color fillStyle_{0.0f, 0.0f, 0.0f, 1.0f};
    Color strokeStyle_{0.0f, 0.0f, 0.0f, 1.0f};
    std::shared_ptr<Gradient> fillGrad_;   // 非空时填充样式为渐变
    std::shared_ptr<Gradient> strokeGrad_; // 非空时描边样式为渐变
    double lineWidth_ = 1.0;
    double globalAlpha_ = 1.0;
    std::string composite_ = "source-over"; // globalCompositeOperation（见 applyComposite）
    int clipDepth_ = 0; // 当前裁剪深度（0=无裁剪；低 7 位深度 + 第 7 位临时 even-odd）

    std::vector<detail::PathSeg> path_;
    int subPathPoints_ = 0; // 当前子路径点数（arc 自动连线判断）

    std::unique_ptr<detail::FontFace> fontFace_;
    mutable std::unordered_map<uint32_t, detail::Glyph> glyphCache_;
    mutable std::unordered_map<uint32_t, detail::Glyph> glyphIndexCache_;
    TextAlign textAlign_ = TextAlign::Left;
    TextBaseline textBaseline_ = TextBaseline::Alphabetic;
    std::string fontCss_;
    std::vector<std::pair<std::string, bool>> features_; // 空 = 特性全关（默认）

    // 线条算法（lineAlgorithm 属性）与像素通道
    detail::LineAlgo lineAlgorithm_ = detail::LineAlgo::Default;
    std::unique_ptr<detail::Program> pixelProgram_;
    std::unique_ptr<detail::VertexArray> pixelVao_;
    std::unique_ptr<detail::VertexBuffer> pixelVbo_;

    // 抗锯齿（antialias 属性）：离屏 FBO + 后处理程序
    std::string antialias_ = "off";
    std::unique_ptr<detail::FrameBuffer> aaFbo_;    // 当前模式离屏绘制目标
    std::unique_ptr<detail::FrameBuffer> mlaaFbo_;  // MLAA pass1 边缘图
    std::unique_ptr<detail::Program> fxaaProg_;
    std::unique_ptr<detail::Program> mlaaEdgeProg_;
    std::unique_ptr<detail::Program> mlaaSmoothProg_;
    int curTargetW_ = 0;  // 当前绘制目标尺寸（0 = 窗口 framebuffer）
    int curTargetH_ = 0;
    bool aaDirty_ = true; // 帧内首个绘制调用需切换离屏 FBO
};

// Window::swapBuffers 类外定义（须在 Canvas 完整定义之后：
// 交换缓冲前自动触发已注册 Canvas 的抗锯齿 resolve）
inline void Window::swapBuffers() {
    for (Canvas* c : attached_) {
        c->present();
    }
    glfwSwapBuffers(window_);
}

} // namespace wbwopenglapi
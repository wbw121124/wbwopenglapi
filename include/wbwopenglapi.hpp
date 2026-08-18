#pragma once
//
// wbwopenglapi - header-only C++17 库
// 目标: 让 C++ 的 OpenGL 编程体验接近 JavaScript Canvas 2D API
//
// 依赖: GLFW 3.3+ / GLAD (gl:core=3.3) / (可选) FreeType
//   字体后端选择:
//     - 定义宏 WBWOPENGAL_API_FONT_FREETYPE: 使用 FreeType (Linux 必需, Windows 可选)
//     - 未定义且 _WIN32: 使用系统 GDI 矢量字体 (Windows 默认)
//     - 未定义且非 Windows: 编译错误
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
#define GLFW_INCLUDE_NONE
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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
        out.push_back(al);
        out.push_back(bl);
        out.push_back(br);
    }
    return out;
}

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
    Window(int w, int h, const std::string& title, bool resizable = true) {
        glfwLife_ = detail::glfwLife();

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
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
    void swapBuffers() { glfwSwapBuffers(window_); }
    void close() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

    // ---- 输入轮询 ----
    bool keyPressed(int key) const { return glfwGetKey(window_, key) == GLFW_PRESS; }
    bool mousePressed(int button) const {
        return glfwGetMouseButton(window_, button) == GLFW_PRESS;
    }
    void mousePosition(double& x, double& y) const { glfwGetCursorPos(window_, &x, &y); }

    GLFWwindow* nativeHandle() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    std::shared_ptr<detail::GlfwLife> glfwLife_;
};

// =====================================================================
// Canvas - 绘制上下文（即 "ctx"），一次创建、每帧绘制
// =====================================================================
class Canvas {
public:
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
    }

    // 清屏（Canvas 无背景概念，此为便捷扩展）
    void clear(const Color& c) {
        updateViewport();
        glClearColor(c.r, c.g, c.b, c.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    void clear(const std::string& css) { clear(parseColor(css)); }

    int width() const { return window_.framebufferWidth(); }
    int height() const { return window_.framebufferHeight(); }

    // ---------------- 样式属性 ----------------

    void fillStyle(const Color& c) { fillStyle_ = c; }
    void fillStyle(const std::string& css) { fillStyle_ = parseColor(css); }
    void strokeStyle(const Color& c) { strokeStyle_ = c; }
    void strokeStyle(const std::string& css) { strokeStyle_ = parseColor(css); }

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
        drawSolid(fillStyle_, tris, 6);
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
            drawSolid(strokeStyle_, strip.data(), strip.size());
        }
    }

    // 清除矩形区域为透明（scissor + 透明清屏）
    void clearRect(double x, double y, double w, double h) {
        if (w <= 0.0 || h <= 0.0) {
            return;
        }
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

private:
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
    // proj_ 按列主序存储 mat3：col0=(2/fw,0,0) col1=(0,-2/fh,0) col2=(-1,1,1)
    void updateViewport() {
        int fw = window_.framebufferWidth();
        int fh = window_.framebufferHeight();
        if (fw <= 0 || fh <= 0) {
            return;
        }
        glViewport(0, 0, fw, fh);
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

    // 以指定颜色绘制三角形列表（含 globalAlpha）。
    // 顶点在 CPU 变换为 NDC 后上传（见 kSolidVS 注释：驱动 attribute/矩阵限制）。
    void drawSolid(const Color& c, const detail::Vec2* verts, size_t count) {
        std::vector<detail::Vec2> ndc(count);
        for (size_t i = 0; i < count; ++i) {
            ndc[i].x = proj_[0] * verts[i].x + proj_[3] * verts[i].y + proj_[6];
            ndc[i].y = proj_[1] * verts[i].x + proj_[4] * verts[i].y + proj_[7];
        }
        program_->use();
        glUniform4f(program_->uniform("u_color"), c.r, c.g, c.b,
                    c.a * static_cast<float>(globalAlpha_));
        vao_->bind();
        vbo_->upload(ndc.data(), static_cast<GLsizeiptr>(ndc.size() * sizeof(detail::Vec2)));
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
        glBindVertexArray(0);
    }

    Window& window_;
    std::unique_ptr<detail::Program> program_;
    std::unique_ptr<detail::VertexArray> vao_;
    std::unique_ptr<detail::VertexBuffer> vbo_;
    float proj_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    Color fillStyle_{0.0f, 0.0f, 0.0f, 1.0f};
    Color strokeStyle_{0.0f, 0.0f, 0.0f, 1.0f};
    double lineWidth_ = 1.0;
    double globalAlpha_ = 1.0;
};

} // namespace wbwopenglapi
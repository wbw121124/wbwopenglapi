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

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace wbwopenglapi {

// =====================================================================
// Color - RGBA 颜色 (0..1)，可从 CSS 字符串解析（解析器在基础矩形阶段加入）
// =====================================================================
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}
};

namespace detail {

// ---------- GLFW 生命周期（引用计数，最后一个 Window 析构时 glfwTerminate） ----------
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
//   骨架阶段: 仅提供 clear()；矩形/路径/文本/变换在后续阶段加入
// =====================================================================
class Canvas {
public:
    explicit Canvas(Window& win) : window_(win) {
        int fw = window_.framebufferWidth();
        int fh = window_.framebufferHeight();
        if (fw > 0 && fh > 0) {
            glViewport(0, 0, fw, fh);
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // 清屏（Canvas 无背景概念，此为便捷扩展）
    void clear(const Color& c) {
        int fw = window_.framebufferWidth();
        int fh = window_.framebufferHeight();
        if (fw > 0 && fh > 0) {
            glViewport(0, 0, fw, fh);
        }
        glClearColor(c.r, c.g, c.b, c.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    int width() const { return window_.framebufferWidth(); }
    int height() const { return window_.framebufferHeight(); }

private:
    Window& window_;
};

} // namespace wbwopenglapi
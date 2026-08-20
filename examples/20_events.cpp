// 20_events - Window 事件回调系统（步 6/8：setKeyCallback 等 GLFW 回调注册，
// 与轮询 API 并存）。-t 模式经 GLFW hook 抓取 + 合成事件直调验证分发链路
// （resize 走真实 GLFW 事件），视觉模式演示按键/字符/鼠标/滚轮/光标。
#include <wbwopenglapi.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// 抓取 GLFW 层回调钩子（glfwSetXxxCallback(handle, nullptr) 返回上一个），
// 用合成参数直调以验证 用户指针 -> Window -> std::function 分发链路。

static bool testDispatch(wbwopenglapi::Window& win) {
    bool all = true;

    bool fired = false;
    win.setKeyCallback([&](int key, int scancode, int action, int mods) {
        fired = key == GLFW_KEY_A && scancode == 3 && action == GLFW_PRESS &&
                mods == GLFW_MOD_SHIFT;
    });
    GLFWkeyfun keyHook = glfwSetKeyCallback(win.nativeHandle(), nullptr);
    glfwSetKeyCallback(win.nativeHandle(), keyHook);
    keyHook(win.nativeHandle(), GLFW_KEY_A, 3, GLFW_PRESS, GLFW_MOD_SHIFT);
    all &= fired;
    std::printf("  %-24s %s\n", "setKeyCallback", fired ? "OK" : "FAIL");

    fired = false;
    win.setCharCallback([&](unsigned int cp) { fired = cp == 0x78; });
    GLFWcharfun charHook = glfwSetCharCallback(win.nativeHandle(), nullptr);
    glfwSetCharCallback(win.nativeHandle(), charHook);
    charHook(win.nativeHandle(), 0x78);
    all &= fired;
    std::printf("  %-24s %s\n", "setCharCallback", fired ? "OK" : "FAIL");

    fired = false;
    win.setMouseButtonCallback([&](int button, int action, int mods) {
        fired = button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE &&
                mods == 0;
    });
    GLFWmousebuttonfun mbHook =
        glfwSetMouseButtonCallback(win.nativeHandle(), nullptr);
    glfwSetMouseButtonCallback(win.nativeHandle(), mbHook);
    mbHook(win.nativeHandle(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    all &= fired;
    std::printf("  %-24s %s\n", "setMouseButtonCallback", fired ? "OK" : "FAIL");

    fired = false;
    win.setCursorPosCallback([&](double x, double y) {
        fired = x == 12.5 && y == 34.25;
    });
    GLFWcursorposfun posHook =
        glfwSetCursorPosCallback(win.nativeHandle(), nullptr);
    glfwSetCursorPosCallback(win.nativeHandle(), posHook);
    posHook(win.nativeHandle(), 12.5, 34.25);
    all &= fired;
    std::printf("  %-24s %s\n", "setCursorPosCallback", fired ? "OK" : "FAIL");

    fired = false;
    win.setScrollCallback([&](double x, double y) {
        fired = x == 0.0 && y == -2.0;
    });
    GLFWscrollfun scrollHook = glfwSetScrollCallback(win.nativeHandle(), nullptr);
    glfwSetScrollCallback(win.nativeHandle(), scrollHook);
    scrollHook(win.nativeHandle(), 0.0, -2.0);
    all &= fired;
    std::printf("  %-24s %s\n", "setScrollCallback", fired ? "OK" : "FAIL");

    fired = false;
    win.setCursorEnterCallback([&](int entered) { fired = entered == 1; });
    GLFWcursorenterfun enterHook =
        glfwSetCursorEnterCallback(win.nativeHandle(), nullptr);
    glfwSetCursorEnterCallback(win.nativeHandle(), enterHook);
    enterHook(win.nativeHandle(), 1);
    all &= fired;
    std::printf("  %-24s %s\n", "setCursorEnterCallback", fired ? "OK" : "FAIL");

    fired = false;
    win.setFramebufferSizeCallback([&](int w, int h) { fired = w == 640 && h == 480; });
    GLFWframebuffersizefun fbHook =
        glfwSetFramebufferSizeCallback(win.nativeHandle(), nullptr);
    glfwSetFramebufferSizeCallback(win.nativeHandle(), fbHook);
    fbHook(win.nativeHandle(), 640, 480);
    all &= fired;
    std::printf("  %-24s %s\n", "setFramebufferSizeCallback", fired ? "OK" : "FAIL");

    fired = false;
    win.setCloseCallback([&]() { fired = true; });
    GLFWwindowclosefun closeHook =
        glfwSetWindowCloseCallback(win.nativeHandle(), nullptr);
    glfwSetWindowCloseCallback(win.nativeHandle(), closeHook);
    closeHook(win.nativeHandle());
    all &= fired;
    std::printf("  %-24s %s\n", "setCloseCallback", fired ? "OK" : "FAIL");

    return all;
}

static bool testResize(wbwopenglapi::Window& win, wbwopenglapi::Canvas& ctx) {
    bool all = true;
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fw, &fh);

    // 真实 resize 事件：framebuffer 回调应触发
    bool resized = false;
    win.setFramebufferSizeCallback(
        [&](int w, int h) { resized = (w == 640 && h == 480); });

    // 初始尺寸绘制红色
    ctx.clear("#ff0000");
    ctx.resolve();
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(fw / 2, fh / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const bool red = px[0] > 200 && px[1] < 60 && px[2] < 60;
    all &= red;
    std::printf("  %-24s (%3d,%3d,%3d) %s\n", "800x600 绘制红", px[0], px[1],
                px[2], red ? "OK" : "FAIL");

    int cw = 0, ch = 0;
    glfwGetWindowSize(glfwGetCurrentContext(), &cw, &ch);
    glfwSetWindowSize(glfwGetCurrentContext(), 640, 480);
    win.pollEvents(); // 触发 framebuffer-size 回调
    all &= resized;
    std::printf("  %-24s %s\n", "resize 回调触发(640x480)",
                resized ? "OK" : "FAIL");
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fw, &fh);

    // 新尺寸绘制蓝色（FBO 按 framebuffer 尺寸自动重建）
    ctx.clear("#0000ff");
    ctx.resolve();
    glReadPixels(fw / 2, fh / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const bool blue = px[2] > 200 && px[0] < 60 && px[1] < 60;
    all &= blue;
    std::printf("  %-24s (%3d,%3d,%3d) %s\n", "640x480 绘制蓝", px[0], px[1],
                px[2], blue ? "OK" : "FAIL");

    // 恢复原尺寸
    glfwSetWindowSize(glfwGetCurrentContext(), cw, ch);
    win.pollEvents();
    return all;
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 20_events", true);
        wbwopenglapi::Canvas ctx(win);

        // 视觉演示状态
        double ballX = 400, ballY = 300, ballR = 30;
        std::vector<std::pair<double, double>> clicks;
        std::string typed;
        double cursorX = 0, cursorY = 0;
        std::string lastEvent = "无事件";

        win.setKeyCallback([&](int key, int, int action, int) {
            if (action != GLFW_PRESS) {
                return;
            }
            const double step = 8.0;
            if (key == GLFW_KEY_LEFT || key == GLFW_KEY_A) {
                ballX -= step;
            } else if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_D) {
                ballX += step;
            } else if (key == GLFW_KEY_UP || key == GLFW_KEY_W) {
                ballY -= step;
            } else if (key == GLFW_KEY_DOWN || key == GLFW_KEY_S) {
                ballY += step;
            } else if (key == GLFW_KEY_ESCAPE) {
                win.close();
            } else {
                lastEvent = "按键 " + std::to_string(key);
            }
        });
        win.setCharCallback([&](unsigned int cp) {
            if (cp >= 32 && cp < 127) {
                typed.push_back(static_cast<char>(cp));
                lastEvent = std::string("字符 '") + typed.back() + "'";
            }
        });
        win.setMouseButtonCallback([&](int button, int action, int) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
                clicks.emplace_back(cursorX, cursorY);
                lastEvent = "左键点击";
            }
        });
        win.setCursorPosCallback(
            [&](double x, double y) { cursorX = x;
                                      cursorY = y; });
        win.setScrollCallback([&](double, double y) {
            ballR += y > 0 ? 5.0 : -5.0;
            if (ballR < 5.0) {
                ballR = 5.0;
            }
            lastEvent = "滚轮";
        });
        win.setCloseCallback([&]() { lastEvent = "关闭请求"; });

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");
            if (testMode) {
                if (!verified && glfwGetTime() - t0 > 0.5) {
                    verified = true;
                    bool all = true;
                    all &= testDispatch(win);
                    all &= testResize(win, ctx);
                    std::printf(all ? "20_events: ALL OK\n"
                                    : "20_events: FAILURES\n");
                    win.close();
                    return all ? 0 : 2;
                }
            } else {
                // 点击产生的圆
                ctx.fillStyle("#e67e22");
                for (const auto& p : clicks) {
                    ctx.beginPath();
                    ctx.arc(p.first, p.second, 8, 0, 6.283185307179586);
                    ctx.fill();
                }
                // 小球（方向键/滚轮）
                ctx.fillStyle("#3498db");
                ctx.beginPath();
                ctx.arc(ballX, ballY, ballR, 0, 6.283185307179586);
                ctx.fill();
                // HUD
                ctx.font("16px sans-serif");
                ctx.fillStyle("#333333");
                ctx.fillText("方向键/WASD 移动小球, 滚轮缩放, 左键点击, 输入字符, ESC 退出",
                             20, 30);
                ctx.fillText("光标: (" + std::to_string(static_cast<int>(cursorX)) +
                                 ", " + std::to_string(static_cast<int>(cursorY)) +
                                 ")  事件: " + lastEvent,
                             20, 56);
                ctx.fillText("输入: " + typed, 20, 82);
            }
            win.swapBuffers();
            win.pollEvents();
            if (testMode && glfwGetTime() - t0 > 3.0) {
                win.close();
            }
        }
        std::printf("20_events 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
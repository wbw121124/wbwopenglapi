// 01_hello - 窗口 + 清屏演示（骨架阶段验证程序）
// 用法: 01_hello.exe [-t]   (-t: 测试模式，3 秒后自动关闭，退出码 0 表示成功)
#include <wbwopenglapi.hpp>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 01_hello", true);
        wbwopenglapi::Canvas ctx(win);

        std::printf("GL_VERSION : %s\n", (const char*)glGetString(GL_VERSION));
        std::printf("GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));
        std::printf("窗口 %dx%d / framebuffer %dx%d\n",
                    win.width(), win.height(),
                    win.framebufferWidth(), win.framebufferHeight());

        double t0 = glfwGetTime();
        while (!win.shouldClose()) {
            double t = glfwGetTime();
            float r = 0.5f + 0.5f * static_cast<float>(std::sin(t));
            float g = 0.5f + 0.5f * static_cast<float>(std::sin(t + 2.094f));
            float b = 0.5f + 0.5f * static_cast<float>(std::sin(t + 4.188f));
            ctx.clear(wbwopenglapi::Color{r, g, b, 1.0f});

            win.swapBuffers();
            win.pollEvents();
            if (win.keyPressed(GLFW_KEY_ESCAPE)) {
                win.close();
            }
            if (testMode && t - t0 > 3.0) {
                win.close();
            }
        }
        std::printf("01_hello 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
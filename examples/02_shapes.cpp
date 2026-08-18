// 02_shapes - 矩形与样式演示（路径部分在阶段 4 加入）
#include <wbwopenglapi.hpp>

#include <cstdio>
#include <cstring>

// 测试模式下的像素校验（Canvas 坐标 y 向下，GL 读坐标 y 向上）
static bool checkPixel(const char* name, int x, int y, int er, int eg, int eb, int tol) {
    int fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(x, fh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    bool ok = std::abs(static_cast<int>(px[0]) - er) <= tol &&
              std::abs(static_cast<int>(px[1]) - eg) <= tol &&
              std::abs(static_cast<int>(px[2]) - eb) <= tol;
    std::printf("  %-26s (%3d,%3d,%3d) %s\n", name, px[0], px[1], px[2],
                ok ? "OK" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 02_shapes", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // fillRect: 基础填充
            ctx.fillStyle("#ff8000");
            ctx.fillRect(40, 40, 160, 120);

            // fillStyle 支持各种 CSS 写法
            ctx.fillStyle("rgb(0, 128, 255)");
            ctx.fillRect(240, 40, 160, 120);
            ctx.fillStyle("#28a745");
            ctx.fillRect(440, 40, 160, 120);
            ctx.fillStyle("rgba(220, 53, 69, 0.5)");
            ctx.fillRect(560, 40, 200, 120); // 半透明叠加

            // strokeRect + lineWidth
            ctx.strokeStyle("#333333");
            ctx.lineWidth(2);
            ctx.strokeRect(40, 220, 160, 120);
            ctx.lineWidth(8);
            ctx.strokeStyle("blue");
            ctx.strokeRect(240, 220, 160, 120);
            ctx.lineWidth(2);
            ctx.strokeStyle("#6f42c1");
            ctx.strokeRect(440, 220, 160, 120);

            // clearRect 清出透明区域
            ctx.fillStyle("teal");
            ctx.fillRect(40, 400, 300, 160);
            ctx.clearRect(90, 440, 120, 80);

            // globalAlpha
            ctx.fillStyle("red");
            ctx.globalAlpha(0.3);
            ctx.fillRect(420, 400, 150, 150);
            ctx.globalAlpha(0.6);
            ctx.fillRect(480, 420, 150, 150);
            ctx.globalAlpha(1.0);

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                GLenum err = glGetError();
                std::printf("glGetError 检查: %s\n",
                            err == GL_NO_ERROR ? "无错误" : ("错误码 " + std::to_string(err)).c_str());
                bool all = true;
                all &= checkPixel("fillRect #ff8000", 120, 100, 255, 128, 0, 8);
                all &= checkPixel("fillRect rgb(0,128,255)", 320, 100, 0, 128, 255, 8);
                all &= checkPixel("fillRect #28a745", 520, 100, 40, 167, 69, 8);
                all &= checkPixel("rgba 半透明叠加", 660, 100, 230, 147, 155, 12);
                // 注意：GL top-left 光栅化规则下，奇数像素宽边框的整数坐标点
                // 像素中心恰在边缘会被排除，故线宽用偶数并取边框内部点
                all &= checkPixel("strokeRect 线宽8", 320, 222, 0, 0, 255, 12);
                all &= checkPixel("strokeRect 线宽2", 120, 220, 51, 51, 51, 12);
                all &= checkPixel("clearRect 透明区", 150, 480, 0, 0, 0, 8);
                all &= checkPixel("globalAlpha 0.3", 445, 475, 245, 168, 168, 12);
                all &= checkPixel("背景 #f0f0f0", 750, 560, 240, 240, 240, 8);
                verified = true;
                win.close();
                return all ? 0 : 2;
            }

            win.swapBuffers();
            win.pollEvents();
            if (win.keyPressed(GLFW_KEY_ESCAPE)) {
                win.close();
            }
            if (testMode && glfwGetTime() - t0 > 3.0) {
                win.close();
            }
        }
        std::printf("02_shapes 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
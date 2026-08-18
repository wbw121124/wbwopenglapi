// 06_transform - 变换矩阵栈演示（translate/rotate/save/restore/resetTransform）
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

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
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 06_transform", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 基准矩形
            ctx.fillStyle("#e74c3c");
            ctx.fillRect(50, 50, 100, 100);

            // save -> 蓝 + 平移 -> restore（验证样式与变换均恢复）
            ctx.save();
            ctx.fillStyle("#3498db");
            ctx.translate(100, 0);
            ctx.fillRect(50, 50, 100, 100);
            ctx.restore();
            ctx.fillRect(250, 50, 100, 100); // restore 后: 红色、无偏移

            // 45 度旋转正方形（中心必命中，验证旋转后仍落在原中心）
            ctx.save();
            ctx.translate(200, 200);
            ctx.rotate(3.14159265358979323846 / 4.0);
            ctx.fillRect(-50, -50, 100, 100);
            ctx.restore();

            // resetTransform 后按常规坐标绘制
            ctx.resetTransform();
            ctx.fillStyle("#2ecc71");
            ctx.fillRect(300, 300, 100, 100);

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                all &= checkPixel("基准矩形", 100, 100, 231, 76, 60, 8);
                all &= checkPixel("save 内平移矩形", 200, 100, 52, 152, 219, 8);
                all &= checkPixel("restore 后红色", 300, 100, 231, 76, 60, 8);
                all &= checkPixel("旋转 45 度中心", 200, 200, 231, 76, 60, 8);
                all &= checkPixel("reset 后绿色", 350, 350, 46, 204, 113, 8);
                all &= checkPixel("背景", 700, 560, 240, 240, 240, 8);
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
        std::printf("06_transform 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
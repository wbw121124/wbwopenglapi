// 04_path - 路径系统演示（moveTo/lineTo/贝塞尔/arc/closePath/fill/stroke）
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
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 04_path", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 三角形 fill
            ctx.fillStyle("#e74c3c");
            ctx.beginPath();
            ctx.moveTo(300, 80);
            ctx.lineTo(500, 80);
            ctx.lineTo(400, 220);
            ctx.closePath();
            ctx.fill();

            // 圆形 fill（arc + closePath）
            ctx.fillStyle("#3498db");
            ctx.beginPath();
            ctx.arc(300, 350, 70, 0, 6.283185307179586, false);
            ctx.closePath();
            ctx.fill();

            // even-odd 环形（内矩形挖空）
            ctx.fillStyle("#2ecc71");
            ctx.beginPath();
            ctx.rect(40, 80, 160, 120);
            ctx.rect(80, 120, 80, 80);
            ctx.fill();

            // 二次贝塞尔围成区域
            ctx.fillStyle("#9b59b6");
            ctx.beginPath();
            ctx.moveTo(650, 80);
            ctx.quadraticCurveTo(700, 40, 720, 140);
            ctx.lineTo(650, 160);
            ctx.closePath();
            ctx.fill();

            // 三次贝塞尔围成区域
            ctx.fillStyle("#1abc9c");
            ctx.beginPath();
            ctx.moveTo(40, 260);
            ctx.bezierCurveTo(100, 220, 140, 300, 200, 260);
            ctx.lineTo(200, 380);
            ctx.lineTo(40, 380);
            ctx.closePath();
            ctx.fill();

            // 折线 stroke（未闭合）
            ctx.strokeStyle("#34495e");
            ctx.lineWidth(6);
            ctx.beginPath();
            ctx.moveTo(400, 80);
            ctx.lineTo(500, 150);
            ctx.lineTo(600, 80);
            ctx.stroke();

            // 闭合菱形 stroke（closePath 首尾相连）
            ctx.strokeStyle("#f39c12");
            ctx.lineWidth(6);
            ctx.beginPath();
            ctx.moveTo(520, 400);
            ctx.lineTo(600, 460);
            ctx.lineTo(520, 520);
            ctx.lineTo(440, 460);
            ctx.closePath();
            ctx.stroke();

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                all &= checkPixel("三角形内部", 400, 140, 231, 76, 60, 12);
                all &= checkPixel("圆形圆心", 300, 350, 52, 152, 219, 12);
                all &= checkPixel("环形外带", 60, 100, 46, 204, 113, 12);
                all &= checkPixel("环形挖空(even-odd)", 100, 140, 240, 240, 240, 8);
                all &= checkPixel("二次贝塞尔区域", 690, 110, 155, 89, 182, 12);
                all &= checkPixel("三次贝塞尔区域", 120, 320, 26, 188, 156, 12);
                all &= checkPixel("折线 stroke", 450, 115, 52, 73, 94, 24);
                all &= checkPixel("闭合菱形 stroke", 480, 430, 243, 156, 18, 24);
                all &= checkPixel("背景", 750, 560, 240, 240, 240, 8);
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
        std::printf("04_path 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
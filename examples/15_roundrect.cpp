// 15_roundrect - ellipse / roundRect 路径演示（步 1/8 验证）
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
    std::printf("  %-30s (%3d,%3d,%3d) %s\n", name, px[0], px[1], px[2],
                ok ? "OK" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 15_roundrect", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 单半径圆角矩形（fill）
            ctx.fillStyle("#e74c3c");
            ctx.beginPath();
            ctx.roundRect(40, 40, 200, 120, 24);
            ctx.fill();

            // 四角不同半径（fill）
            ctx.fillStyle("#3498db");
            ctx.beginPath();
            ctx.roundRect(300, 40, 200, 120, 40, 8, 20, 8);
            ctx.fill();

            // 半径钳制：r 大于 min(w,h)/2 时钳到 50（半宽/半高）
            ctx.fillStyle("#2ecc71");
            ctx.beginPath();
            ctx.roundRect(560, 40, 100, 100, 999);
            ctx.fill();

            // 旋转椭圆（fill，rotation = 0.6rad）
            ctx.fillStyle("#9b59b6");
            ctx.beginPath();
            ctx.ellipse(160, 320, 110, 55, 0.6, 0, 6.283185307179586, false);
            ctx.closePath();
            ctx.fill();

            // 椭圆弧 stroke（1/4 圆弧段，rotation = 0）
            ctx.strokeStyle("#e67e22");
            ctx.lineWidth(6);
            ctx.beginPath();
            ctx.ellipse(480, 320, 130, 70, 0, 0, 1.5707963267948966, false);
            ctx.stroke();

            // roundRect stroke（未闭合路径验证首尾衔接）
            ctx.strokeStyle("#34495e");
            ctx.lineWidth(6);
            ctx.beginPath();
            ctx.roundRect(560, 240, 180, 120, 30);
            ctx.stroke();

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                all &= checkPixel("圆角矩形内部", 100, 80, 231, 76, 60, 12);
                all &= checkPixel("圆角矩形角落外", 45, 45, 240, 240, 240, 8);
                all &= checkPixel("四角半径内部", 380, 80, 52, 152, 219, 12);
                all &= checkPixel("半径钳制内部", 580, 100, 46, 204, 113, 12);
                all &= checkPixel("旋转椭圆内部", 150, 350, 155, 89, 182, 12);
                all &= checkPixel("旋转椭圆中心", 160, 320, 155, 89, 182, 12);
                all &= checkPixel("椭圆弧中点", 572, 370, 230, 126, 34, 24);
                all &= checkPixel("roundRect stroke 上边", 640, 240, 52, 73, 94, 24);
                all &= checkPixel("roundRect stroke 圆角", 571, 247, 52, 73, 94, 24);
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
        std::printf("15_roundrect 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
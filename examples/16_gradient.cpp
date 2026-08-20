// 16_gradient - 线性/径向渐变演示（步 2/8 验证）
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

static bool checkPixel(const char* name, int x, int y, int er, int eg, int eb,
                       int tol) {
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
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 16_gradient", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 线性渐变：红->蓝，水平
            wbwopenglapi::Gradient g1 = ctx.createLinearGradient(40, 0, 240, 0);
            g1.addColorStop(0, "#e74c3c");
            g1.addColorStop(0.5, "#f1c40f");
            g1.addColorStop(1, "#3498db");
            ctx.fillStyle(g1);
            ctx.fillRect(40, 40, 200, 120);

            // 线性渐变对角 + 变换联动（translate 后渐变随画布移动）
            wbwopenglapi::Gradient g2 = ctx.createLinearGradient(0, 0, 160, 160);
            g2.addColorStop(0, "#2ecc71");
            g2.addColorStop(1, "#9b59b6");
            ctx.save();
            ctx.translate(320, 100);
            ctx.fillStyle(g2);
            ctx.fillRect(0, 0, 160, 160);
            ctx.restore();

            // 径向渐变：白心->紫边
            wbwopenglapi::Gradient g3 = ctx.createRadialGradient(180, 320, 0, 180, 320, 90);
            g3.addColorStop(0, "#ffffff");
            g3.addColorStop(0.6, "#e67e22");
            g3.addColorStop(1, "#c0392b");
            ctx.fillStyle(g3);
            ctx.beginPath();
            ctx.arc(180, 320, 90, 0, 6.283185307179586);
            ctx.fill();

            // 径向渐变：内圆焦点偏移（两圆焦点式）
            wbwopenglapi::Gradient g4 =
                ctx.createRadialGradient(520, 250, 20, 560, 320, 110);
            g4.addColorStop(0, "#1abc9c");
            g4.addColorStop(1, "#2980b9");
            ctx.fillStyle(g4);
            ctx.beginPath();
            ctx.roundRect(450, 210, 220, 220, 30);
            ctx.fill();

            // 渐变 stroke
            wbwopenglapi::Gradient g5 = ctx.createLinearGradient(40, 490, 260, 490);
            g5.addColorStop(0, "#34495e");
            g5.addColorStop(1, "#e74c3c");
            ctx.strokeStyle(g5);
            ctx.lineWidth(16);
            ctx.beginPath();
            ctx.moveTo(40, 490);
            ctx.lineTo(260, 490);
            ctx.stroke();

            // 渐变文本（fillText 走矢量轮廓；■ 实心方块便于像素校验）
            wbwopenglapi::Gradient g6 = ctx.createLinearGradient(320, 0, 720, 0);
            g6.addColorStop(0, "#8e44ad");
            g6.addColorStop(1, "#f39c12");
            ctx.fillStyle(g6);
            ctx.font("40px sans-serif");
            ctx.fillText("Gradient", 320, 520);
            ctx.font("96px sans-serif");
            ctx.fillText("\xe2\x96\x88", 320, 300);

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                all &= checkPixel("线性 0% 红", 45, 80, 231, 76, 60, 10);
                all &= checkPixel("线性 50% 黄", 140, 80, 241, 196, 15, 14);
                all &= checkPixel("线性 100% 蓝", 235, 80, 52, 152, 219, 10);
                all &= checkPixel("线性外部钳制", 42, 45, 231, 76, 60, 10);
                all &= checkPixel("对角渐变左上", 340, 120, 60, 190, 122, 14);
                all &= checkPixel("对角渐变右下", 330, 110, 53, 197, 117, 14);
                all &= checkPixel("径向渐变心", 180, 320, 255, 255, 255, 6);
                all &= checkPixel("径向渐变中环", 180, 250, 213, 95, 38, 14);
                all &= checkPixel("径向渐变外缘", 265, 320, 197, 67, 42, 18);
                all &= checkPixel("径向焦点内绿", 520, 250, 26, 188, 156, 14);
                all &= checkPixel("径向焦点外蓝", 620, 380, 41, 128, 185, 16);
                all &= checkPixel("渐变 stroke 左", 45, 490, 52, 73, 94, 16);
                all &= checkPixel("渐变 stroke 右", 255, 490, 231, 76, 60, 16);
                all &= checkPixel("渐变文本左", 350, 250, 134, 75, 161, 20);
                all &= checkPixel("渐变文本右", 370, 250, 155, 79, 154, 20);
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
        std::printf("16_gradient 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
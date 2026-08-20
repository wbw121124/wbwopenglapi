// 17_clip - clip 裁剪演示（步 3/8 验证：基础/嵌套/even-odd/渐变/文本/恢复）
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

static bool checkPixel(const char* name, int x, int y,
                       int er, int eg, int eb, int tol) {
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
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 17_clip", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 1) 矩形 clip + 渐变填充：clip 区域外是背景
            wbwopenglapi::Gradient g1 = ctx.createLinearGradient(40, 0, 240, 0);
            g1.addColorStop(0, "#e74c3c");
            g1.addColorStop(1, "#3498db");
            ctx.save();
            ctx.beginPath();
            ctx.rect(40, 40, 200, 120);
            ctx.clip();
            ctx.fillStyle(g1);
            ctx.fillRect(0, 0, 800, 600);
            ctx.restore();

            // 2) 圆形 clip + 纯色 fill：圆形外不透出
            ctx.save();
            ctx.beginPath();
            ctx.arc(420, 100, 80, 0, 6.283185307179586);
            ctx.clip();
            ctx.fillStyle("#2ecc71");
            ctx.fillRect(0, 0, 800, 600);
            ctx.restore();

            // 3) 嵌套 clip：矩形 ∩ 圆 = 内部紫色小区域
            ctx.save();
            ctx.beginPath();
            ctx.rect(300, 250, 260, 200);
            ctx.clip();
            ctx.save();
            ctx.beginPath();
            ctx.arc(430, 350, 100, 0, 6.283185307179586);
            ctx.clip();
            ctx.fillStyle("#9b59b6");
            ctx.fillRect(0, 0, 800, 600);
            ctx.restore();
            ctx.restore();

            // 4) 两圆 clip：单子路径 even-odd = 异或（重叠区为洞，不在裁剪区）
            ctx.save();
            ctx.beginPath();
            ctx.arc(180, 330, 70, 0, 6.283185307179586);
            ctx.arc(240, 330, 70, 0, 6.283185307179586);
            ctx.clip();
            ctx.fillStyle("#e67e22");
            ctx.fillRect(0, 0, 800, 600);
            ctx.restore();

            // 5) clip + stroke：描边被裁剪
            ctx.save();
            ctx.beginPath();
            ctx.rect(480, 300, 200, 140);
            ctx.clip();
            ctx.strokeStyle("#34495e");
            ctx.lineWidth(8);
            ctx.beginPath();
            ctx.moveTo(400, 240);
            ctx.lineTo(800, 480);
            ctx.stroke();
            ctx.restore();

            // 6) clip 内渐变文本（fillText 矢量轮廓；■ 实心方块）
            ctx.save();
            ctx.beginPath();
            ctx.roundRect(40, 430, 260, 100, 20);
            ctx.clip();
            wbwopenglapi::Gradient g6 = ctx.createLinearGradient(40, 0, 300, 0);
            g6.addColorStop(0, "#8e44ad");
            g6.addColorStop(1, "#f39c12");
            ctx.fillStyle(g6);
            ctx.font("64px sans-serif");
            ctx.fillText("\xe2\x96\x88", 40, 520);
            ctx.restore();

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                ctx.resolve(); // 把离屏结果合成到默认 framebuffer 并绑定 FBO 为读目标
                bool all = true;
                all &= checkPixel("矩形 clip 内渐变", 140, 100, 142, 115, 140, 24);
                all &= checkPixel("矩形 clip 外背景", 40, 20, 240, 240, 240, 8);
                all &= checkPixel("圆形 clip 内绿", 420, 100, 46, 204, 113, 10);
                all &= checkPixel("圆形 clip 外背景", 520, 100, 240, 240, 240, 8);
                all &= checkPixel("嵌套交叠区紫", 430, 350, 155, 89, 182, 12);
                all &= checkPixel("嵌套矩形内圆外", 320, 270, 240, 240, 240, 8);
                all &= checkPixel("嵌套圆内矩内", 350, 310, 155, 89, 182, 12);
                all &= checkPixel("两圆重叠区(even-odd洞)", 210, 330, 240, 240, 240, 8);
                all &= checkPixel("单圆区", 150, 330, 230, 126, 34, 12);
                all &= checkPixel("clip stroke 线上", 560, 336, 52, 73, 94, 16);
                all &= checkPixel("clip stroke 外", 560, 320, 240, 240, 240, 8);
                all &= checkPixel("clip 文本内", 70, 490, 154, 78, 155, 24);
                all &= checkPixel("clip 文本外", 340, 500, 240, 240, 240, 8);
                all &= checkPixel("restore 后全屏", 700, 560, 240, 240, 240, 8);
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
        std::printf("17_clip 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
// 08_demo - 综合演示：全部 API + 动画（矩形/路径/文本/变换/图像）
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        const wbwopenglapi::Image img = wbwopenglapi::loadBMP("examples/test.bmp");

        wbwopenglapi::Window win(800, 600, "wbwopenglapi 08_demo", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        while (!win.shouldClose()) {
            const double t = glfwGetTime() - t0;

            ctx.clear("#20242b");
            ctx.globalAlpha(1.0);

            // ---- 路径：旋转的星形 ----
            ctx.save();
            ctx.translate(160, 160);
            ctx.rotate(t * 0.8);
            ctx.beginPath();
            const int spikes = 5;
            for (int i = 0; i < spikes * 2; ++i) {
                const double r = (i % 2 == 0) ? 90.0 : 38.0;
                const double a = i * 3.14159265358979323846 / spikes;
                const double px = r * std::cos(a);
                const double py = r * std::sin(a);
                if (i == 0) {
                    ctx.moveTo(px, py);
                } else {
                    ctx.lineTo(px, py);
                }
            }
            ctx.closePath();
            ctx.fillStyle("#f1c40f");
            ctx.fill();
            ctx.strokeStyle("#e67e22");
            ctx.lineWidth(4);
            ctx.stroke();
            ctx.restore();

            // ---- 文本 + 变换：旋转的标题 ----
            ctx.save();
            ctx.translate(400, 160);
            ctx.rotate(std::sin(t * 1.5) * 0.25);
            ctx.fillStyle("#ecf0f1");
            ctx.font("36px sans-serif");
            ctx.textAlign(wbwopenglapi::TextAlign::Center);
            ctx.textBaseline(wbwopenglapi::TextBaseline::Middle);
            ctx.fillText("wbwopenglapi Demo", 0, 0);
            ctx.restore();

            // ---- 图像 + 变换：旋转的方块 ----
            ctx.save();
            ctx.translate(640, 140);
            ctx.rotate(t * 1.2);
            ctx.globalAlpha(0.85);
            ctx.drawImage(img, -30, -30, 60, 60);
            ctx.restore();
            ctx.globalAlpha(1.0);

            // ---- 矩形与路径组合 ----
            ctx.fillStyle("#2c3e50");
            ctx.fillRect(40, 300, 120, 260);
            ctx.strokeStyle("#3498db");
            ctx.lineWidth(6);
            ctx.strokeRect(40, 300, 120, 260);

            // 贝塞尔曲线
            ctx.strokeStyle("#e74c3c");
            ctx.lineWidth(4);
            ctx.beginPath();
            ctx.moveTo(200, 520);
            ctx.bezierCurveTo(300, 320, 420, 320, 520, 520);
            ctx.stroke();

            // 圆环（even-odd 挖空）
            ctx.fillStyle("#16a085");
            ctx.beginPath();
            ctx.arc(620, 430, 90, 0, 6.2832);
            ctx.arc(620, 430, 55, 0, 6.2832);
            ctx.fill();

            // 底部状态栏文本
            ctx.fillStyle("#95a5a6");
            ctx.font("18px sans-serif");
            ctx.textAlign(wbwopenglapi::TextAlign::Left);
            ctx.textBaseline(wbwopenglapi::TextBaseline::Alphabetic);
            char fps[64];
            std::snprintf(fps, sizeof(fps), "t=%.2fs  measure='Demo'=%.1fpx", t,
                          ctx.measureText("Demo"));
            ctx.fillText(fps, 20, 580);

            win.swapBuffers();
            win.pollEvents();
            if (win.keyPressed(GLFW_KEY_ESCAPE)) {
                win.close();
            }
            if (testMode && glfwGetTime() - t0 > 3.0) {
                win.close();
            }
        }
        std::printf("08_demo 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
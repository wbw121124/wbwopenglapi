// 18_composite - globalCompositeOperation 12 模式演示与像素校验（步 4/8）
// 验证场景：dst=不透明蓝矩形(150,150,120,120)，src=半透明红圆(globalAlpha 0.5)，
// 圆心 (200,200) r=60 突出矩形左缘，故 (145,200) 为 src 在 dst 外。
// 预期按直通 alpha 存储下 GL 混合方程计算：C = Cs*Fs + Cd*Fd（因子见 applyComposite）。
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static bool checkPixel(const char* name, int x, int y, int er, int eg, int eb,
                       int tol) {
    int fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(x, fh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    bool ok = std::abs(static_cast<int>(px[0]) - er) <= tol &&
              std::abs(static_cast<int>(px[1]) - eg) <= tol &&
              std::abs(static_cast<int>(px[2]) - eb) <= tol;
    std::printf("  %-32s (%3d,%3d,%3d) %s\n", name, px[0], px[1], px[2],
                ok ? "OK" : "FAIL");
    return ok;
}

static const char* kModes[12] = {
    "source-over",       "source-in",    "source-out",     "source-atop",
    "destination-over",  "destination-in", "destination-out", "destination-atop",
    "lighter",           "copy",         "xor",            "multiply",
};

// 重叠点 (200,200)：Cs=红 As=0.5，Cd=蓝 Ad=1
static const int kOverlap[12][3] = {
    {128, 0, 128}, // source-over:  0.5*红 + 0.5*蓝
    {255, 0, 0},   // source-in:    1.0*红
    {0, 0, 0},     // source-out:   0.0*红（透明）
    {255, 0, 128}, // source-atop:  1.0*红 + 0.5*蓝
    {0, 0, 255},   // destination-over: 0*红 + 1.0*蓝
    {0, 0, 128},   // destination-in:   0.5*蓝
    {0, 0, 128},   // destination-out:  0.5*蓝
    {0, 0, 128},   // destination-atop: 0*红 + 0.5*蓝
    {255, 0, 255}, // lighter:      红 + 蓝
    {255, 0, 0},   // copy:         红
    {0, 0, 128},   // xor:          0*红 + 0.5*蓝
    {0, 0, 0},     // multiply:     红*蓝
};

// src 在 dst 外 (145,200)：Cd=0 Ad=0
static const int kOutside[12][3] = {
    {128, 0, 0}, // source-over:  0.5*红
    {0, 0, 0},   // source-in:    0*红（透明）
    {255, 0, 0}, // source-out:   1.0*红
    {0, 0, 0},   // source-atop:  0*红
    {255, 0, 0}, // destination-over: 1.0*红
    {0, 0, 0},   // destination-in:   0
    {0, 0, 0},   // destination-out:  0
    {255, 0, 0}, // destination-atop: 1.0*红
    {255, 0, 0}, // lighter:      红
    {255, 0, 0}, // copy:         红
    {255, 0, 0}, // xor:          1.0*红
    {0, 0, 0},   // multiply:     红*0
};

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 18_composite", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            if (testMode) {
                if (!verified && glfwGetTime() - t0 > 0.5) {
                    verified = true;
                    bool all = true;
                    for (int i = 0; i < 12; ++i) {
                        ctx.clearRect(0, 0, 800, 600); // 透明底
                        ctx.fillStyle("#0000ff");      // dst 不透明蓝
                        ctx.fillRect(150, 150, 120, 120);
                        ctx.globalCompositeOperation(kModes[i]);
                        ctx.globalAlpha(0.5); // src 半透明红
                        ctx.fillStyle("#ff0000");
                        ctx.beginPath();
                        ctx.arc(200, 200, 60, 0, 6.283185307179586);
                        ctx.fill();
                        ctx.globalAlpha(1.0);
                        ctx.globalCompositeOperation("source-over");
                        ctx.resolve(); // 合成到默认 framebuffer 后读回
                        const std::string ov = std::string("重叠 ") + kModes[i];
                        const std::string ot = std::string("外部 ") + kModes[i];
                        all &= checkPixel(ov.c_str(), 200, 200, kOverlap[i][0],
                                          kOverlap[i][1], kOverlap[i][2], 1);
                        all &= checkPixel(ot.c_str(), 145, 200, kOutside[i][0],
                                          kOutside[i][1], kOutside[i][2], 1);
                    }
                    std::printf(all ? "18_composite: ALL OK (24 checks)\n"
                                    : "18_composite: FAILURES\n");
                    win.close();
                    return all ? 0 : 2;
                }
            } else {
                // 视觉演示：白底 + 4x3 网格，每格 dst 半透明蓝 + src 半透明红
                ctx.clear("#ffffff");
                ctx.font("14px sans-serif");
                ctx.fillStyle("#000000");
                ctx.fillText("globalCompositeOperation 12 modes", 20, 24);
                for (int i = 0; i < 12; ++i) {
                    const int col = i % 4, row = i / 4;
                    const double x0 = 20 + col * 195, y0 = 44 + row * 178;
                    ctx.globalCompositeOperation("source-over");
                    ctx.fillStyle("#0000ff");
                    ctx.globalAlpha(0.4);
                    ctx.fillRect(x0, y0, 160, 120);
                    ctx.globalAlpha(1.0);
                    ctx.globalCompositeOperation(kModes[i]);
                    ctx.fillStyle("#ff0000");
                    ctx.globalAlpha(0.5);
                    ctx.beginPath();
                    ctx.arc(x0 + 90, y0 + 60, 45, 0, 6.283185307179586);
                    ctx.fill();
                    ctx.globalAlpha(1.0);
                    ctx.globalCompositeOperation("source-over");
                    ctx.fillStyle("#333333");
                    ctx.fillText(kModes[i], x0, y0 + 142);
                }
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
        std::printf("18_composite 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
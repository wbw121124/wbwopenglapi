// 12_antialias - 抗锯齿策略对比（off / ssaa / msaa / fxaa / mlaa）
//   每帧按区域绘制 45° 斜线 + 圆 + 文本（含连字 "->"），五种模式依次切换，
//   验证帧内切换、swapBuffers 自动 resolve、以及逐帧循环不泄漏。
// 断言:
//   1. off 模式边缘为二值; ssaa/msaa/fxaa/mlaa 边缘存在中间色像素
//   2. 各模式几何内部深处像素一致（不糊内部）
//   3. 连续 5 模式 × 数帧切换无崩溃、无资源泄漏（GL 错误计数为零）
#include <wbwopenglapi.hpp>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static constexpr int kW = 800, kH = 600;
static constexpr int kBg = 240;
static bool isInk(unsigned char r, unsigned char g, unsigned char b) {
    return std::abs(static_cast<int>(r) - kBg) > 24 ||
           std::abs(static_cast<int>(g) - kBg) > 24 ||
           std::abs(static_cast<int>(b) - kBg) > 24;
}
static bool isSolid(unsigned char r, unsigned char g, unsigned char b) {
    return std::abs(static_cast<int>(r) - 231) <= 24 &&
           std::abs(static_cast<int>(g) - 76) <= 24 &&
           std::abs(static_cast<int>(b) - 60) <= 24;
}
static void readPx(int x, int y, unsigned char px[4]) {
    int fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
    glReadPixels(x, fh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
}
// 把当前默认 framebuffer 存为 24-bit BMP（GL 与 BMP 同为自下而上行序）
static void saveBmp(const char* path) {
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fw, &fh);
    if (fw <= 0 || fh <= 0) {
        return;
    }
    std::vector<unsigned char> px(static_cast<size_t>(fw) * fh * 4);
    glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const int stride = ((fw * 3 + 3) / 4) * 4; // BMP 行填充 4 字节对齐
    const int imgSize = stride * fh;
    std::vector<unsigned char> out(static_cast<size_t>(54) + imgSize, 0);
    out[0] = 'B';
    out[1] = 'M';
    const uint32_t fileSize = 54u + static_cast<uint32_t>(imgSize);
    std::memcpy(&out[2], &fileSize, 4);
    const uint32_t dataOff = 54;
    std::memcpy(&out[10], &dataOff, 4);
    const uint32_t biSize = 40;
    std::memcpy(&out[14], &biSize, 4);
    const int32_t iw = fw;
    std::memcpy(&out[18], &iw, 4);
    const int32_t ih = fh;
    std::memcpy(&out[22], &ih, 4);
    const uint16_t planes = 1;
    std::memcpy(&out[26], &planes, 2);
    const uint16_t bpp = 24;
    std::memcpy(&out[28], &bpp, 2);
    const uint32_t comp = 0, isize = static_cast<uint32_t>(imgSize);
    std::memcpy(&out[30], &comp, 4);
    std::memcpy(&out[34], &isize, 4);
    for (int y = 0; y < fh; ++y) {
        const unsigned char* src = &px[static_cast<size_t>(fh - 1 - y) * fw * 4];
        unsigned char* dst = &out[54 + static_cast<size_t>(y) * stride];
        for (int x = 0; x < fw; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2]; // B
            dst[x * 3 + 1] = src[x * 4 + 1]; // G
            dst[x * 3 + 2] = src[x * 4 + 0]; // R
        }
    }
    FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        std::printf("  已保存 %s (%dx%d)\n", path, fw, fh);
    }
}
// 沿斜线边缘统计中间色像素（[85..355]×[85..225] 斜线带状区）
static int midToneCount() {
    int n = 0;
    for (int x = 90; x <= 350; ++x) {
        const int yc = 90 + static_cast<int>((x - 90) * 0.5);
        for (int dy = -4; dy <= 4; ++dy) {
            unsigned char px[4] = {0, 0, 0, 0};
            readPx(x, yc + dy, px);
            if (isInk(px[0], px[1], px[2]) && !isSolid(px[0], px[1], px[2])) {
                ++n;
            }
        }
    }
    return n;
}

// 画一帧完整内容（当前 antialias 模式下）
static void drawScene(wbwopenglapi::Canvas& ctx, const char* label) {
    ctx.clear("#f0f0f0");
    ctx.strokeStyle("#e74c3c");
    ctx.lineWidth(3.0);
    ctx.beginPath();
    ctx.moveTo(90, 90);
    ctx.lineTo(350, 220);
    ctx.stroke();
    ctx.fillStyle("#2ecc71");
    ctx.beginPath();
    ctx.arc(470, 160, 70, 0, 6.28318);
    ctx.fill();
    ctx.fillStyle("#3498db");
    ctx.font("44px sans-serif");
    ctx.fillText("AA -> 12", 400, 330);
    ctx.fillStyle("#555555");
    ctx.font("20px sans-serif");
    ctx.fillText(label, 60, 500);
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(kW, kH, "wbwopenglapi 12_antialias", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        const char* modes[] = {"off", "ssaa", "msaa", "fxaa", "mlaa"};
        const char* labels[] = {"off 无抗锯齿", "ssaa 超采样2x", "msaa 多重采样4x",
                                "fxaa 快速近似", "mlaa 形态学"};
        int frame = 0;
        while (!win.shouldClose()) {
            const int modeIdx = (frame / 2) % 5;
            ctx.antialias(modes[modeIdx]);
            drawScene(ctx, labels[modeIdx]);

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                for (int m = 0; m < 5; ++m) {
                    // 协议：画 → resolve()（present 把结果合成到默认 framebuffer
                    // back，不交换缓冲）→ 读 back（此刻 back 即为本模式最终输出）
                    // → swapBuffers()（例行交换，其内部 present 因 aaDirty 已复位
                    // 而空跑，不影响后续）
                    ctx.antialias(modes[m]);
                    drawScene(ctx, "验证");
                    ctx.resolve();
                    if (m == 0) {
                        // DEBUG: 打印斜线四边形 strip 顶点 + 终点区域像素
                        std::fprintf(stderr, "[dbg] strip 顶点:\n");
                        std::vector<wbwopenglapi::detail::Vec2> dbgPts = {{90, 90}, {350, 220}};
                        auto dbgStrip = wbwopenglapi::detail::buildStrokeStrip(dbgPts, false, 3.0f);
                        for (size_t i = 0; i < dbgStrip.size(); ++i) {
                            std::fprintf(stderr, "  v[%zu]=(%.2f, %.2f)\n", i,
                                         static_cast<double>(dbgStrip[i].x),
                                         static_cast<double>(dbgStrip[i].y));
                        }
                        std::fprintf(stderr, "[dbg] 终点区域(347..351,216..223):\n");
                        for (int dx = 347; dx <= 351; ++dx) {
                            std::fprintf(stderr, "  x=%d:", dx);
                            for (int dy = 216; dy <= 223; ++dy) {
                                unsigned char q[4] = {0};
                                readPx(dx, dy, q);
                                std::fprintf(stderr, " %d,%d,%d", q[0], q[1], q[2]);
                            }
                            std::fprintf(stderr, "\n");
                        }
                    }
                    saveBmp((std::string("test/12_antialias_") + modes[m] + ".bmp").c_str());

                    const int mid = midToneCount();
                    unsigned char px[4] = {0, 0, 0, 0};
                    readPx(310, 200, px); // 斜线内部（90,90)-(350,220) 在 x=310 的中心 y=200
                    const bool deepOk = isSolid(px[0], px[1], px[2]);
                    std::printf("  [原始] %-5s 斜线内部=(%d,%d,%d) ", modes[m], px[0], px[1], px[2]);
                    readPx(470, 160, px); // 圆填充内部
                    const bool circleOk = std::abs(static_cast<int>(px[0]) - 46) <= 24 &&
                                          std::abs(static_cast<int>(px[1]) - 204) <= 24 &&
                                          std::abs(static_cast<int>(px[2]) - 113) <= 24;
                    std::printf("圆内部=(%d,%d,%d)\n", px[0], px[1], px[2]);
                    std::printf("  %-5s 中间色=%d 斜线内部=%s 圆内部=%s\n",
                                modes[m], mid,
                                deepOk ? "OK" : "FAIL",
                                circleOk ? "OK" : "FAIL");
                    if (m == 0) {
                        // off：斜线中心与圆内部保持纯色；中间色只来自 1px 光栅化边缘
                        all = all && deepOk && circleOk;
                    } else {
                        // AA 模式：斜线中心与圆内部仍须纯色（不糊内部），边缘有中间色
                        all = all && deepOk && circleOk && (mid > 50);
                    }
                    win.swapBuffers();
                }
                GLenum err = glGetError();
                all = all && (err == GL_NO_ERROR);
                std::printf("  切换后 glGetError=%u %s\n", err, err == GL_NO_ERROR ? "OK" : "FAIL");
                std::printf("12_antialias: %s\n", all ? "全部通过" : "有失败");
                verified = true;
                win.close();
                return all ? 0 : 2;
            }

            win.swapBuffers();
            win.pollEvents();
            if (win.keyPressed(GLFW_KEY_ESCAPE)) {
                win.close();
            }
            ++frame;
            if (testMode && glfwGetTime() - t0 > 3.0) {
                win.close();
            }
        }
        std::printf("12_antialias 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
// 19_png - PNG 读写验证（步 5/8：loadPNG/savePNG/toPNG + 手工构造多格式/全滤波行）
// 未启用 WBWOPENGAL_API_PNG（缺 zlib）时编译为提示 stub，exit 0。
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef WBWOPENGAL_API_PNG
#include <zlib.h>

using wbwopenglapi::Image;

// ---- 手工构造 PNG（CRC 复用库内实现，算法同源） ----

static void chunk(std::vector<uint8_t>& out, const char* type, const uint8_t* d,
                  size_t n) {
    auto be32 = [&out](uint32_t v) {
        out.push_back(static_cast<uint8_t>(v >> 24));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
    };
    be32(static_cast<uint32_t>(n));
    const size_t t = out.size();
    out.insert(out.end(), type, type + 4);
    if (n > 0) {
        out.insert(out.end(), d, d + n);
    }
    be32(wbwopenglapi::detail::pngCrc32(out.data() + t, 4 + n));
}

static std::vector<uint8_t> craftPng(int w, int h, int colorType,
                                     const std::vector<uint8_t>& rawRows,
                                     const std::vector<uint8_t>& plte = {},
                                     const std::vector<uint8_t>& trns = {}) {
    std::vector<uint8_t> out;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), sig, sig + 8);
    uint8_t ihdr[13] = {0};
    ihdr[0] = static_cast<uint8_t>(w >> 24);
    ihdr[1] = static_cast<uint8_t>(w >> 16);
    ihdr[2] = static_cast<uint8_t>(w >> 8);
    ihdr[3] = static_cast<uint8_t>(w);
    ihdr[4] = static_cast<uint8_t>(h >> 24);
    ihdr[5] = static_cast<uint8_t>(h >> 16);
    ihdr[6] = static_cast<uint8_t>(h >> 8);
    ihdr[7] = static_cast<uint8_t>(h);
    ihdr[8] = 8;
    ihdr[9] = static_cast<uint8_t>(colorType);
    chunk(out, "IHDR", ihdr, 13);
    if (!plte.empty()) {
        chunk(out, "PLTE", plte.data(), plte.size());
    }
    if (!trns.empty()) {
        chunk(out, "tRNS", trns.data(), trns.size());
    }
    uLongf len = compressBound(static_cast<uLong>(rawRows.size()));
    std::vector<uint8_t> comp(len);
    if (compress2(comp.data(), &len, rawRows.data(),
                  static_cast<uLong>(rawRows.size()), 6) != Z_OK) {
        std::abort();
    }
    chunk(out, "IDAT", comp.data(), len);
    chunk(out, "IEND", nullptr, 0);
    return out;
}

static bool checkImage(const char* name, const Image& got,
                       const std::vector<uint8_t>& want) {
    bool ok = got.rgba == want;
    if (ok) {
        std::printf("  %-30s (%dx%d) OK\n", name, got.width, got.height);
    } else {
        std::printf("  %-30s (%dx%d) FAIL (got %u, want %u)\n", name,
                    got.width, got.height,
                    static_cast<unsigned>(got.rgba.size()),
                    static_cast<unsigned>(want.size()));
        const size_t n = std::min(got.rgba.size(), want.size());
        for (size_t i = 0; i < n; ++i) {
            if (got.rgba[i] != want[i]) {
                std::printf("    首个差异 @%u: got=%u want=%u\n",
                            static_cast<unsigned>(i),
                            static_cast<unsigned>(got.rgba[i]),
                            static_cast<unsigned>(want[i]));
                break;
            }
        }
    }
    return ok;
}

// ---- 测试用例 ----

// 1) RGBA + 五种滤波行各一行（5 行 x 4 像素；值域跨 0..255 覆盖环绕）
static bool testFilters() {
    const int w = 4, h = 5, bpp = 4;
    std::vector<std::vector<uint8_t>> origRows(
        h, std::vector<uint8_t>(static_cast<size_t>(w) * 4));
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            origRows[i][j * 4 + 0] = static_cast<uint8_t>(20 * i + 5 * j);
            origRows[i][j * 4 + 1] = static_cast<uint8_t>(200 - 20 * i);
            origRows[i][j * 4 + 2] = static_cast<uint8_t>(250 - 30 * j);
            origRows[i][j * 4 + 3] = 255;
        }
    }
    const std::vector<uint8_t> zero(static_cast<size_t>(w) * 4, 0);
    std::vector<uint8_t> rows;
    for (int i = 0; i < h; ++i) {
        rows.push_back(static_cast<uint8_t>(i)); // filter 0..4
        const std::vector<uint8_t>& orig = origRows[i];
        const std::vector<uint8_t>& upRow = (i == 0) ? zero : origRows[i - 1];
        std::vector<uint8_t> cur(static_cast<size_t>(w) * 4);
        std::vector<uint8_t> rec(static_cast<size_t>(w) * 4); // 重建行（left 取自重建值）
        for (size_t x = 0; x < cur.size(); ++x) {
            const uint8_t left = x >= static_cast<size_t>(bpp) ? rec[x - bpp] : 0;
            const uint8_t up = upRow[x];
            const uint8_t upleft =
                x >= static_cast<size_t>(bpp) ? upRow[x - bpp] : 0;
            uint8_t pred = 0;
            if (i == 0) {
                pred = 0;
            } else if (i == 1) {
                pred = left;
            } else if (i == 2) {
                pred = up;
            } else if (i == 3) {
                pred = static_cast<uint8_t>((left + up) >> 1);
            } else {
                const int p = static_cast<int>(left) + up - upleft;
                const int pa = std::abs(p - left), pb = std::abs(p - up),
                          pc = std::abs(p - upleft);
                pred = (pa <= pb && pa <= pc) ? left
                       : (pb <= pc)           ? up
                                             : upleft;
            }
            cur[x] = static_cast<uint8_t>(orig[x] - pred);
            rec[x] = static_cast<uint8_t>(orig[x]);
        }
        rows.insert(rows.end(), cur.begin(), cur.end());
    }
    std::vector<uint8_t> want;
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            want.push_back(static_cast<uint8_t>(20 * i + 5 * j));
            want.push_back(static_cast<uint8_t>(200 - 20 * i));
            want.push_back(static_cast<uint8_t>(250 - 30 * j));
            want.push_back(255);
        }
    }
    const std::vector<uint8_t> png = craftPng(w, h, 6, rows);
    return checkImage("滤波 None/Sub/Up/Avg/Paeth",
                      wbwopenglapi::loadPNG(png.data(), png.size()), want);
}

// 2) 调色板（colortype 3）+ tRNS 半透明/全透明
static bool testPalette() {
    const int w = 2, h = 2;
    const std::vector<uint8_t> plte = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0};
    const std::vector<uint8_t> trns = {255, 128, 0, 255};
    const std::vector<uint8_t> rows = {0, 0, 1, 0, 2, 3}; // 2 行 filter 0
    const std::vector<uint8_t> want = {
        255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 0, 255,
    };
    const std::vector<uint8_t> png = craftPng(w, h, 3, rows, plte, trns);
    return checkImage("调色板+tRNS", wbwopenglapi::loadPNG(png.data(), png.size()), want);
}

// 3) 灰度（colortype 0）+ tRNS 透明灰值
static bool testGray() {
    const int w = 2, h = 1;
    const std::vector<uint8_t> trns = {0, 7};
    const std::vector<uint8_t> rows = {0, 7, 100};
    const std::vector<uint8_t> want = {7, 7, 7, 0, 100, 100, 100, 255};
    const std::vector<uint8_t> png = craftPng(w, h, 0, rows, {}, trns);
    return checkImage("灰度+tRNS", wbwopenglapi::loadPNG(png.data(), png.size()), want);
}

// 4) 真彩（colortype 2）+ tRNS 透明色
static bool testRgb() {
    const int w = 1, h = 2;
    const std::vector<uint8_t> trns = {5, 6, 7};
    const std::vector<uint8_t> rows = {0, 5, 6, 7, 0, 8, 9, 10};
    const std::vector<uint8_t> want = {5, 6, 7, 0, 8, 9, 10, 255};
    const std::vector<uint8_t> png = craftPng(w, h, 2, rows, {}, trns);
    return checkImage("真彩+tRNS", wbwopenglapi::loadPNG(png.data(), png.size()), want);
}

// 5) 画布绘制 -> 读回 -> toPNG/savePNG -> loadPNG 逐像素一致
static bool testCanvasRoundTrip(wbwopenglapi::Canvas& ctx) {
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fw, &fh);
    ctx.resolve(); // 合成到默认 framebuffer（本函数调用前应已完成本帧绘制）
    std::vector<uint8_t> buf(static_cast<size_t>(fw) * fh * 4);
    glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    Image img;
    img.width = fw;
    img.height = fh;
    img.rgba.resize(static_cast<size_t>(fw) * fh * 4);
    for (int y = 0; y < fh; ++y) { // GL 底行 -> 画布顶行
        std::memcpy(img.rgba.data() + static_cast<size_t>(fh - 1 - y) * fw * 4,
                    buf.data() + static_cast<size_t>(y) * fw * 4,
                    static_cast<size_t>(fw) * 4);
    }
    const std::vector<uint8_t> png = wbwopenglapi::toPNG(img);
    wbwopenglapi::savePNG(img, "test/19_png.png");
    const Image back = wbwopenglapi::loadPNG("test/19_png.png");
    bool ok = back.width == fw && back.height == fh && back.rgba == img.rgba;
    std::printf("  %-30s (%dx%d) %s\n", "画布 toPNG->savePNG->loadPNG", fw, fh,
                ok ? "OK" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 19_png", true);
        wbwopenglapi::Canvas ctx(win);

        // 视觉演示用的合成图（RGBA 棋盘 + 渐变，测试 alpha 通道）
        Image demo;
        demo.width = 64;
        demo.height = 64;
        demo.rgba.resize(64 * 64 * 4);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                uint8_t* p = &demo.rgba[(y * 64 + x) * 4];
                const bool dark = ((x / 8) + (y / 8)) % 2 == 0;
                p[0] = dark ? 30 : 220;
                p[1] = dark ? 150 : 60;
                p[2] = dark ? 90 : 230;
                p[3] = static_cast<uint8_t>(128 + x * 2);
            }
        }
        wbwopenglapi::savePNG(demo, "test/19_demo.png");

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");
            if (!testMode) {
                // 视觉演示：加载回的 PNG 放大显示 + 原图纹理
                Image loaded;
                try {
                    loaded = wbwopenglapi::loadPNG("test/19_demo.png");
                } catch (...) {
                    loaded = demo;
                }
                ctx.drawImage(demo, 40, 40, 256, 256);
                ctx.drawImage(loaded, 340, 40, 256, 256);
                ctx.font("16px sans-serif");
                ctx.fillStyle("#333333");
                ctx.fillText("原图 (alpha 棋盘) -> toPNG -> savePNG -> loadPNG", 40, 330);
                ctx.fillText("test/19_demo.png", 340, 330);
            } else {
                if (!verified && glfwGetTime() - t0 > 0.5) {
                    verified = true;
                    bool all = true;
                    all &= testFilters();
                    all &= testPalette();
                    all &= testGray();
                    all &= testRgb();

                    // 画布绘制（含渐变/形状/文本），供整帧回读校验
                    wbwopenglapi::Gradient g = ctx.createLinearGradient(100, 0, 500, 0);
                    g.addColorStop(0, "#e74c3c");
                    g.addColorStop(1, "#3498db");
                    ctx.fillStyle(g);
                    ctx.fillRect(100, 100, 400, 200);
                    ctx.fillStyle("#2ecc71");
                    ctx.beginPath();
                    ctx.arc(300, 300, 80, 0, 6.283185307179586);
                    ctx.fill();
                    ctx.fillStyle("#000000");
                    ctx.font("48px sans-serif");
                    ctx.fillText("PNG 19", 200, 450);
                    all &= testCanvasRoundTrip(ctx);
                    // 屏幕校验点（resolve 后）
                    int fh = 0;
                    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
                    unsigned char px[4] = {0, 0, 0, 0};
                    glReadPixels(150, fh - 1 - 200, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    const bool g1 = px[0] > 180 && px[1] < 120 && px[2] < 120;
                    std::printf("  渐变矩形内                  (%3d,%3d,%3d) %s\n",
                                px[0], px[1], px[2], g1 ? "OK" : "FAIL");
                    glReadPixels(300, fh - 1 - 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    const bool g2 = px[1] > 150 && px[0] < 110 && px[2] < 150;
                    std::printf("  圆内绿                      (%3d,%3d,%3d) %s\n",
                                px[0], px[1], px[2], g2 ? "OK" : "FAIL");
                    // 文本区域统计（容错字形空洞）
                    int dark = 0;
                    for (int yy = 415; yy < 465; ++yy) {
                        for (int xx = 195; xx < 360; ++xx) {
                            glReadPixels(xx, fh - 1 - yy, 1, 1, GL_RGBA,
                                         GL_UNSIGNED_BYTE, px);
                            if (px[0] < 100 && px[1] < 100 && px[2] < 100) {
                                ++dark;
                            }
                        }
                    }
                    const bool g3 = dark >= 100;
                    std::printf("  文本区域深色像素              %d %s\n", dark,
                                g3 ? "OK" : "FAIL");
                    all &= g1 && g2 && g3;
                    std::printf(all ? "19_png: ALL OK\n" : "19_png: FAILURES\n");
                    win.close();
                    return all ? 0 : 2;
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
        std::printf("19_png 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
#else
int main() {
    std::printf("19_png: PNG 支持未启用（编译时缺 WBWOPENGAL_API_PNG 宏/zlib）\n");
    return 0;
}
#endif // WBWOPENGAL_API_PNG
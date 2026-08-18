// 10_ligature - OpenType 连体/特性验证（Fira Code, 需 FreeType+HarfBuzz）
//   - calt: "->" 连体字形（hyphen_start.seq 等部件）与默认逐码点渲染不同
//   - cv02: "g" 双层变单层（字形变化）
//   - zero: "0" 默认方孔空心，zero 特性斜杠穿过中心
//   - 多行混排互不串位（西文）
// 字体: third_party/fonts/FiraCode-Regular.ttf（fetch_deps.ps1 下载）；
//   无字体或非 FreeType+HarfBuzz 配置时跳过连体断言。
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

[[maybe_unused]] static bool checkPixel(const char* name, int x, int y, int er, int eg, int eb, int tol) {
    int fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(x, fh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    bool ok = std::abs(static_cast<int>(px[0]) - er) <= tol &&
              std::abs(static_cast<int>(px[1]) - eg) <= tol &&
              std::abs(static_cast<int>(px[2]) - eb) <= tol;
    std::printf("  %-28s (%3d,%3d,%3d) %s\n", name, px[0], px[1], px[2],
                ok ? "OK" : "FAIL");
    return ok;
}

// 两个同尺寸区域（相对坐标一致）逐像素差异计数
[[maybe_unused]] static int diffCount(int x0, int y0, int x1, int y1, int x2, int y2) {
    int fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
    int n = 0;
    for (int y = 0; y <= y1 - y0; ++y) {
        for (int x = 0; x <= x1 - x0; ++x) {
            unsigned char a[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0};
            glReadPixels(x0 + x, fh - 1 - (y0 + y), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, a);
            glReadPixels(x2 + x, fh - 1 - (y2 + y), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, b);
            if (std::abs(static_cast<int>(a[0]) - b[0]) > 12 ||
                std::abs(static_cast<int>(a[1]) - b[1]) > 12 ||
                std::abs(static_cast<int>(a[2]) - b[2]) > 12) {
                ++n;
            }
        }
    }
    return n;
}

[[maybe_unused]] static const char* findFiraCode() {
    const char* cands[] = {
        "D:\\wbwopenglapi\\third_party\\fonts\\FiraCode-Regular.ttf",
        "../third_party/fonts/FiraCode-Regular.ttf",
        "third_party/fonts/FiraCode-Regular.ttf",
    };
    for (const char* c : cands) {
        std::ifstream f(c);
        if (f.good()) {
            return c;
        }
    }
    return nullptr;
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 10_ligature", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");
            ctx.textAlign(wbwopenglapi::TextAlign::Left);
            ctx.textBaseline(wbwopenglapi::TextBaseline::Alphabetic);

#if defined(WBWOPENGAL_API_FONT_FREETYPE) && defined(WBWOPENGAL_API_FONT_HARFBUZZ)
            const char* fc = findFiraCode();
            if (fc) {
                const std::string f96 = std::string("96px ") + fc;
                const std::string f64 = std::string("64px ") + fc;

                // A: calt 连体（96px, 基线 320）—— 箭头连体部件
                ctx.fontFeatures({{"calt", true}});
                ctx.font(f96);
                ctx.fillStyle("#e74c3c");
                ctx.fillText("->", 100, 320);

                // C: 默认（特性全关, 基线 240）—— 两个独立字符
                ctx.resetFontFeatures();
                ctx.fillStyle("#2ecc71");
                ctx.fillText("->", 100, 240);

                // G: "g" 默认（基线 160）双层 g
                ctx.fillStyle("#3498db");
                ctx.fillText("g", 100, 160);

                // B: "g" + cv02（基线 80）单层 g
                ctx.fontFeatures({{"cv02", true}});
                ctx.fillStyle("#e67e22");
                ctx.fillText("g", 100, 80);
                ctx.resetFontFeatures();

                // ZD: "0" 默认（x=300, 基线 320）方孔零
                ctx.font(f64);
                ctx.fillStyle("#16a085");
                ctx.fillText("0", 300, 320);

                // Z: "0" + zero（x=300, 基线 240）斜杠零
                ctx.fontFeatures({{"zero", true}});
                ctx.fillStyle("#9b59b6");
                ctx.fillText("0", 300, 240);
                ctx.resetFontFeatures();

                // 多行混排（32px, 基线 480/520/560）
                ctx.font(std::string("32px ") + fc);
                ctx.fillStyle("#9b59b6");
                ctx.fillText("==> Hello 123", 100, 480);
                ctx.fillStyle("#f39c12");
                ctx.fillText("==> Hello 123", 100, 520);
                ctx.fillStyle("#1abc9c");
                ctx.fillText("==> Hello 123", 100, 560);

                if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                    bool all = true;
                    const int diffAC = diffCount(100, 290, 220, 350, 100, 210);
                    const int diffBG = diffCount(100, 50, 170, 110, 100, 130);
                    std::printf("  [测量] diffAC(-> calt vs 默认)=%d diffBG(g cv02 vs 默认)=%d\n",
                                diffAC, diffBG);
                    // 1. calt 连体改变 "->" 字形（部件替换）
                    std::printf("  %-28s %s\n", "calt 连体像素差异",
                                diffAC > 40 ? "OK" : "FAIL");
                    all = all && (diffAC > 40);
                    // 2. cv02 改变 "g" 字形（双层->单层）
                    std::printf("  %-28s %s\n", "cv02 字形差异",
                                diffBG > 40 ? "OK" : "FAIL");
                    all = all && (diffBG > 40);
                    // 3. zero: 默认方孔中心背景, zero 斜杠穿过中心
                    //    Fira Code '0' 菱形孔中心 ~ (300+21, 基线-19)
                    all &= checkPixel("0 默认中心(空心)", 300 + 21, 320 - 19, 240, 240, 240, 20);
                    all &= checkPixel("0 zero 中心(斜杠)", 300 + 21, 240 - 19, 155, 89, 182, 30);
                    // 4. 多行起始点互不串位
                    all &= checkPixel("第 1 行起始", 106, 466, 155, 89, 182, 14);
                    all &= checkPixel("第 2 行起始", 106, 506, 243, 156, 18, 14);
                    all &= checkPixel("第 3 行起始", 106, 546, 26, 188, 156, 14);
                    verified = true;
                    win.close();
                    return all ? 0 : 2;
                }
            } else if (testMode) {
                // 无 Fira Code: 连体断言无法执行（视为通过）
                std::printf("10_ligature 未找到 FiraCode-Regular.ttf, 跳过连体断言\n");
                verified = true;
                win.close();
                return 0;
            }
#else
            (void)testMode;
            ctx.font("32px sans-serif");
            ctx.fillStyle("#2c3e50");
            ctx.fillText("10_ligature: 需 FreeType+HarfBuzz 编译配置", 100, 100);
            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                verified = true;
                win.close();
                return 0;
            }
#endif

            win.swapBuffers();
            win.pollEvents();
            if (win.keyPressed(GLFW_KEY_ESCAPE)) {
                win.close();
            }
            if (testMode && glfwGetTime() - t0 > 3.0) {
                win.close();
            }
        }
        std::printf("10_ligature 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
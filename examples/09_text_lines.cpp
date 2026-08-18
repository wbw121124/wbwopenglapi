// 09_text_lines - 多字符/多行混排文本验证（重叠缺陷回归）
//   "Hello 世界 123" 等多字符文本必须按 advance 逐字排开：
//   - "██" 的第二个方块必须位于首个方块右侧
//   - "世界" 的第二个字必须位于首字右侧（UTF-8 多字节）
//   - 多行混排各行互不串位
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
    std::printf("  %-28s (%3d,%3d,%3d) %s\n", name, px[0], px[1], px[2],
                ok ? "OK" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 09_text_lines", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 关键断言 1: "██" 第二个方块（修复前两个方块都画在 [100..157]，
            //   (185,100) 为背景色; 修复后第二个方块覆盖 [157..216]）
            ctx.fillStyle("#e74c3c");
            ctx.font("96px sans-serif");
            ctx.fillText("\xe2\x96\x88\xe2\x96\x88", 100, 100);

            // 关键断言 2: "世界" 第二个字"界"（64px 全宽字符，
            //   修复前两个字都在 [300..364]，(390,300) 为背景）
            ctx.fillStyle("#2ecc71");
            ctx.font("64px sans-serif");
            ctx.fillText("\xe4\xb8\x96\xe7\x95\x8c", 300, 300);

            // 多行混排（英文/中文/数字），行间距 50px
            ctx.font("32px sans-serif");
            ctx.textAlign(wbwopenglapi::TextAlign::Left);
            ctx.textBaseline(wbwopenglapi::TextBaseline::Alphabetic);
            ctx.fillStyle("#3498db");
            ctx.fillText("Hello \xe4\xb8\x96\xe7\x95\x8c 123", 100, 400);
            ctx.fillStyle("#e67e22");
            ctx.fillText("Hello \xe4\xb8\x96\xe7\x95\x8c 123", 100, 450);
            ctx.fillStyle("#9b59b6");
            ctx.fillText("Hello \xe4\xb8\x96\xe7\x95\x8c 123", 100, 500);

            // maxWidth 压缩路径（scale<1，光标需同比例缩放）
            ctx.fillStyle("#16a085");
            ctx.font("48px sans-serif");
            ctx.fillText("\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88", 600, 500, 60);

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                // 第二个方块（修复前背景色）
                all &= checkPixel("双方块第 2 块", 185, 100, 231, 76, 60, 16);
                // "世界" 第二字"界"的田字中心（基线 300 上方 40px，修复前背景色）
                all &= checkPixel("中文第 2 字", 392, 262, 46, 204, 113, 24);
                // 三行起始点颜色（基线 400/450/500 上方 12px，行互不串位）
                all &= checkPixel("第 1 行起始", 105, 388, 52, 152, 219, 12);
                all &= checkPixel("第 2 行起始", 105, 438, 230, 126, 34, 12);
                all &= checkPixel("第 3 行起始", 105, 488, 155, 89, 182, 12);
                // maxWidth 压缩后第 3 个方块仍在其第 2 块右侧（~600+2*15..45）
                all &= checkPixel("压缩第 2 块", 617, 498, 22, 160, 133, 16);
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
        std::printf("09_text_lines 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
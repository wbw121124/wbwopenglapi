// 13_dwrite_text - DirectWrite 字体后端验证（Windows 8.1+，链接系统库 -ldwrite）
//   仅在 build.ps1 -Backend dwrite 或 CMake -DWBWOPENGAL_API_FONT_DWRITE=ON 下
//   编译进 DWrite 分支（fillText/strokeText/measureText/对齐/基线）:
//   - 默认字体: 系统序列 Segoe UI -> Arial -> 微软雅黑（字形回退链，见 wbwopenglapi.hpp）
//   - 与 GDI 后端差异: DWrite 使用系统 ClearType 元数据级矢量轮廓，抗锯齿由本库
//     统一处理（软件 AA/SSAA/MSAA），字形度量语义一致（1/64 像素、y 向上、基线 y=0）
//   无 DWrite 宏时本示例等同 05_text（GDI/FreeType 后端均可用）
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
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 13_dwrite_text", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 大号方块字符（U+2588 FULL BLOCK）：字形为实心矩形，测试稳定
            ctx.fillStyle("#e74c3c");
            ctx.font("96px sans-serif");
            ctx.fillText("\xe2\x96\x88", 100, 300);

            // 实心圆（U+25CF BLACK CIRCLE）
            ctx.fillStyle("#3498db");
            ctx.fillText("\xe2\x97\x8f", 320, 300);

            // strokeText 方块字符
            ctx.strokeStyle("#f1c40f");
            ctx.lineWidth(8);
            ctx.font("96px sans-serif");
            ctx.strokeText("\xe2\x96\x88", 500, 300);

            // 中文填充（DWrite 下经回退链命中微软雅黑；无回退时本断言必失败）
            ctx.fillStyle("#2ecc71");
            ctx.font("64px sans-serif");
            ctx.fillText("\xe4\xb8\xad", 100, 500);

            // 对齐/基线演示（非测试断言）
            ctx.fillStyle("#7f8c8d");
            ctx.font("24px sans-serif");
            ctx.textAlign(wbwopenglapi::TextAlign::Center);
            ctx.textBaseline(wbwopenglapi::TextBaseline::Middle);
            ctx.fillText("Hello \xe4\xb8\x96\xe7\x95\x8c 123", 400, 60);
            ctx.textAlign(wbwopenglapi::TextAlign::Left);
            ctx.textBaseline(wbwopenglapi::TextBaseline::Alphabetic);
            ctx.strokeStyle("#95a5a6");
            ctx.lineWidth(1);
            ctx.strokeText("measureText: " +
                               std::to_string(ctx.measureText("Hello world")) +
                               " px",
                           40, 560);

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                // █ 实心方块 96px：字形约 [100..190] x [228..319]（基线 300）
                all &= checkPixel("fillText 方块", 130, 275, 231, 76, 60, 12);
                // ● 实心圆 96px：中心约 (368, 273) 附近，半径大容差
                all &= checkPixel("fillText 实心圆", 368, 270, 52, 152, 219, 12);
                // strokeText 方块：线宽 8 沿字形左边缘 500 -> 覆盖 496..504
                all &= checkPixel("strokeText 方块", 500, 280, 241, 196, 15, 24);
                // 中文"中"：竖笔画中心 (132, 468)
                all &= checkPixel("fillText 中文", 132, 468, 46, 204, 113, 16);
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
        std::printf("13_dwrite_text 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}

// 07_image - 图像演示（loadBMP + drawImage：原尺寸/缩放）
#include <wbwopenglapi.hpp>

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
    std::printf("  %-26s (%3d,%3d,%3d) %s\n", name, px[0], px[1], px[2],
                ok ? "OK" : "FAIL");
    return ok;
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        const wbwopenglapi::Image img = wbwopenglapi::loadBMP("examples/test.bmp");
        std::printf("图像尺寸: %dx%d (通道 %zu 字节)\n", img.width, img.height,
                    img.rgba.size());

        wbwopenglapi::Window win(800, 600, "wbwopenglapi 07_image", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // 原尺寸（4x4 像素，放大观察用矩形示意）
            ctx.drawImage(img, 100, 100);

            // 缩放 10 倍: 40x40
            ctx.drawImage(img, 200, 100, 40, 40);

            // 变换后绘制（平移 + 旋转）
            ctx.save();
            ctx.translate(400, 300);
            ctx.rotate(0.5);
            ctx.drawImage(img, -20, -20, 40, 40);
            ctx.restore();

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                // 原尺寸 4x4 @ (100,100)：四象限
                all &= checkPixel("原尺寸 红", 101, 101, 255, 0, 0, 8);
                all &= checkPixel("原尺寸 绿", 103, 101, 0, 255, 0, 8);
                all &= checkPixel("原尺寸 蓝", 101, 103, 0, 0, 255, 8);
                all &= checkPixel("原尺寸 白", 103, 103, 255, 255, 255, 8);
                // 缩放 40x40 @ (200,100)：中心取像素
                all &= checkPixel("缩放 红", 210, 110, 255, 0, 0, 16);
                all &= checkPixel("缩放 绿", 230, 110, 0, 255, 0, 16);
                all &= checkPixel("缩放 蓝", 210, 130, 0, 0, 255, 16);
                all &= checkPixel("缩放 白", 230, 130, 255, 255, 255, 16);
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
        std::printf("07_image 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
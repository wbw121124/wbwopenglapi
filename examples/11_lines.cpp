// 11_lines - 像素级直线算法对比（DDA / Bresenham / Xiaolin Wu）
//   - "default"    矢量三角带描边（默认，尊重 lineWidth）
//   - "dda"        逐像素直线（二值，阶梯锯齿）
//   - "bresenham"  逐像素直线（二值，整数误差累积）
//   - "wu"         Xiaolin Wu 抗锯齿直线（相邻像素 alpha 渐变）
// 断言:
//   1. wu 线边缘存在中间色像素（alpha 渐变）; dda/bresenham 无中间色
//   2. 各算法线起点像素命中
//   3. strokeText 在任意 lineAlgorithm 下输出与 default 逐像素一致（字体轮廓恒矢量）
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr int kBg = 240;
static const char* kColor = "#e74c3c";

// 把当前默认 framebuffer 存为 24-bit BMP
static void saveBmp(const char* path) {
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), &fw, &fh);
    if (fw <= 0 || fh <= 0) {
        return;
    }
    std::vector<unsigned char> px(static_cast<size_t>(fw) * fh * 4);
    glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const int stride = ((fw * 3 + 3) / 4) * 4;
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
            dst[x * 3 + 0] = src[x * 4 + 2];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 0];
        }
    }
    FILE* f = std::fopen(path, "wb");
    if (f) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }
}

static bool isInk(unsigned char r, unsigned char g, unsigned char b)
{
    return std::abs(static_cast<int>(r) - kBg) > 24 ||
        std::abs(static_cast<int>(g) - kBg) > 24 ||
        std::abs(static_cast<int>(b) - kBg) > 24;
}

static bool isPure(unsigned char r, unsigned char g, unsigned char b)
{
    return std::abs(static_cast<int>(r) - 231) <= 24 &&
        std::abs(static_cast<int>(g) - 76) <= 24 &&
        std::abs(static_cast<int>(b) - 60) <= 24;
}

static void readPx(int x, int y, unsigned char px[4])
{
    int fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
    glReadPixels(x, fh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

// 沿理想线段扫描：统计"中间色"（既非背景也非纯前景）像素数
static int midToneCount(int x0, int y0, int x1, int y1)
{
    int n = 0;
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    for (int x = x0 + 10; x <= x1 - 10; ++x)
    {
        const double t = (x - x0) / dx;
        const int yc = static_cast<int>(y0 + dy * t);
        for (int dy2 = -4; dy2 <= 4; ++dy2)
        {
            unsigned char px[4] = { 0, 0, 0, 0 };
            readPx(x, yc + dy2, px);
            if (isInk(px[0], px[1], px[2]) && !isPure(px[0], px[1], px[2]))
            {
                ++n;
            }
        }
    }
    return n;
}

int main(int argc, char** argv)
{
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try
    {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 11_lines", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose())
        {
            ctx.clear("#f0f0f0");
            ctx.strokeStyle(kColor);

            // A: default 矢量描边（线宽 4）
            ctx.lineAlgorithm("default");
            ctx.lineWidth(4.0);
            ctx.beginPath();
            ctx.moveTo(60, 80);
            ctx.lineTo(660, 230);
            ctx.stroke();

            // B: DDA（1px 二值）
            ctx.lineAlgorithm("dda");
            ctx.beginPath();
            ctx.moveTo(60, 250);
            ctx.lineTo(660, 400);
            ctx.stroke();

            // C: Bresenham（1px 二值）
            ctx.lineAlgorithm("bresenham");
            ctx.beginPath();
            ctx.moveTo(60, 420);
            ctx.lineTo(660, 570);
            ctx.stroke();

            // D: Xiaolin Wu（1px alpha 渐变）
            ctx.lineAlgorithm("wu");
            ctx.beginPath();
            ctx.moveTo(60, 480);
            ctx.lineTo(660, 530);
            ctx.stroke();
            ctx.lineAlgorithm("default");

            // strokeText 一致性锚点（default vs wu 必须逐像素一致）
            ctx.lineWidth(2.0);
            ctx.strokeStyle("#8e44ad");
            ctx.font("48px sans-serif");
            ctx.textBaseline(wbwopenglapi::TextBaseline::Alphabetic);
            ctx.lineAlgorithm("wu");
            ctx.strokeText("O", 690, 140);
            ctx.lineAlgorithm("default");
            ctx.strokeText("O", 690, 140);
            ctx.lineAlgorithm("default");

            if (testMode && !verified && glfwGetTime() - t0 > 0.5)
            {
                saveBmp("test/11_lines.bmp");
                bool all = true;
                const int midB = midToneCount(60, 250, 660, 400); // dda
                const int midC = midToneCount(60, 420, 660, 570); // bresenham
                const int midD = midToneCount(60, 480, 660, 530); // wu
                std::printf("  [测量] 中间色像素 dda=%d bresenham=%d wu=%d\n",
                    midB, midC, midD);

                // 1. dda/bresenham 二值（无中间色）; wu 有渐变
                all = all && (midB == 0 && midC == 0 && midD > 30);

                // 2. 起点像素命中
                {
                    unsigned char px[4] = { 0, 0, 0, 0 };
                    readPx(60, 80, px);   // default（线宽 4，中心 80）
                    std::printf("  [测量] 起点 default=(%d,%d,%d) ", px[0], px[1], px[2]);
                    all = all && isPure(px[0], px[1], px[2]);
                    readPx(60, 250, px);  // dda
                    std::printf("dda=(%d,%d,%d) ", px[0], px[1], px[2]);
                    all = all && isPure(px[0], px[1], px[2]);
                    readPx(60, 420, px);  // bresenham
                    std::printf("bresenham=(%d,%d,%d) ", px[0], px[1], px[2]);
                    all = all && isPure(px[0], px[1], px[2]);
                    readPx(60, 480, px);  // wu（端点按 gap 修正可能半覆盖）
                    std::printf("wu=(%d,%d,%d)\n", px[0], px[1], px[2]);
                    all = all && isInk(px[0], px[1], px[2]);
                    std::printf("  起点像素 x=60 %s\n", all ? "OK" : "FAIL");
                }

                // 3. strokeText 一致性：同一 "O" 以 default 与 wu 各画一遍，
                //    字体轮廓恒矢量，重叠区域不应出现半透明（中间色）像素
                {
                    int mid = 0;
                    for (int y = 95; y <= 145; ++y)
                    {
                        for (int x = 690; x <= 750; ++x)
                        {
                            unsigned char px[4] = { 0, 0, 0, 0 };
                            readPx(x, y, px);
                            const bool ink = std::abs(static_cast<int>(px[0]) - kBg) > 24 ||
                                std::abs(static_cast<int>(px[1]) - kBg) > 24 ||
                                std::abs(static_cast<int>(px[2]) - kBg) > 24;
                            const bool pure = std::abs(static_cast<int>(px[0]) - 142) <= 24 &&
                                std::abs(static_cast<int>(px[1]) - 68) <= 24 &&
                                std::abs(static_cast<int>(px[2]) - 173) <= 24;
                            if (ink && !pure)
                            {
                                ++mid;
                            }
                        }
                    }
                    std::printf("  [测量] strokeText 重叠区中间色像素=%d\n", mid);
                    all = all && (mid == 0);
                    std::printf("  strokeText 矢量一致性 %s\n", mid == 0 ? "OK" : "FAIL");
                }

                std::printf("11_lines: %s\n", all ? "全部通过" : "有失败");
                verified = true;
                win.close();
                return all ? 0 : 2;
            }

            win.swapBuffers();
            win.pollEvents();
            if (win.keyPressed(GLFW_KEY_ESCAPE))
            {
                win.close();
            }
            if (testMode && glfwGetTime() - t0 > 3.0)
            {
                win.close();
            }
        }
        std::printf("11_lines 正常退出\n");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}
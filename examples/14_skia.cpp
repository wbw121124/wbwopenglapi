// 14_skia - Skia 软件栅格化后端验证（可选，需 vcpkg skia + CMake -DWBWOPENGAL_API_SKIA=ON）
//   无窗口 headless：SkiaCanvas 栅格化矩形/圆/路径/文本 -> top-down RGBA ->
//   像素校验 + 导出 test/14_skia.bmp。
//   注意: BMP 文件行序自下而上（首行数据为图像底部），导出时须将内存 top-down 数据翻转，
//         本示例的 saveBmp 为正确范式（12_antialias 的旧实现存在颠倒缺陷）
//   构建: cmake -B build -DWBWOPENGAL_API_SKIA=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
//         cmake --build build --target 14_skia --config Release
#ifdef WBWOPENGAL_API_SKIA

#include <wbwopenglapi_skia.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// 内存像素（top-down, 行 0 = 图像顶部）-> 24-bit BMP（自下而上行序）
static bool saveBmp(const char* path, const std::vector<uint8_t>& px, int w, int h) {
    const int stride = ((w * 3 + 3) / 4) * 4; // BMP 行填充 4 字节对齐
    const int imgSize = stride * h;
    std::vector<uint8_t> out(static_cast<size_t>(54) + imgSize, 0);
    out[0] = 'B';
    out[1] = 'M';
    const uint32_t fileSize = 54u + static_cast<uint32_t>(imgSize);
    std::memcpy(&out[2], &fileSize, 4);
    const uint32_t dataOff = 54;
    std::memcpy(&out[10], &dataOff, 4);
    const uint32_t biSize = 40;
    std::memcpy(&out[14], &biSize, 4);
    const int32_t iw = w;
    std::memcpy(&out[18], &iw, 4);
    const int32_t ih = h;
    std::memcpy(&out[22], &ih, 4);
    const uint16_t planes = 1;
    std::memcpy(&out[26], &planes, 2);
    const uint16_t bpp = 24;
    std::memcpy(&out[28], &bpp, 2);
    const uint32_t comp = 0, isize = static_cast<uint32_t>(imgSize);
    std::memcpy(&out[30], &comp, 4);
    std::memcpy(&out[34], &isize, 4);
    // BMP 文件第 0 行 = 图像底部（自下而上），内存第 0 行 = 图像顶部
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = &px[static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) * 4];
        uint8_t* dst = &out[54 + static_cast<size_t>(y) * stride];
        for (int x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2]; // B
            dst[x * 3 + 1] = src[x * 4 + 1]; // G
            dst[x * 3 + 2] = src[x * 4 + 0]; // R
        }
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        return false;
    }
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

static bool checkPx(const char* name, const std::vector<uint8_t>& px, int w, int h,
                    int x, int y, int er, int eg, int eb, int tol) {
    const uint8_t* p = &px[static_cast<size_t>(y) * static_cast<size_t>(w) * 4 +
                           static_cast<size_t>(x) * 4];
    const bool ok = std::abs(static_cast<int>(p[0]) - er) <= tol &&
                    std::abs(static_cast<int>(p[1]) - eg) <= tol &&
                    std::abs(static_cast<int>(p[2]) - eb) <= tol;
    std::printf("  %-24s (%3d,%3d,%3d) %s\n", name, p[0], p[1], p[2], ok ? "OK" : "FAIL");
    return ok;
}

int main() {
    bool all = true;
    try {
        wbwopenglapi::SkiaCanvas c(800, 600);
        c.clear(0.941f, 0.941f, 0.941f); // #f0f0f0
        c.fillStyle(1.0f, 0.5f, 0.0f);   // 橙: fillRect
        c.fillRect(50, 50, 200, 150);
        c.fillStyle(0.18f, 0.60f, 0.71f); // 蓝: fillCircle
        c.fillCircle(400, 300, 80);
        c.beginPath();
        c.moveTo(600, 100);
        c.lineTo(700, 200);
        c.lineTo(620, 260);
        c.closePath();
        c.fillStyle(0.18f, 0.8f, 0.44f); // 绿: 路径三角形
        c.fill();
        c.font(48.0);
        c.fillStyle(0.0f, 0.0f, 0.0f); // 文本
        c.fillText("Hello Skia 123", 100, 500);
        std::printf("  %-24s %s\n", "字体链路", c.fontStatus().c_str());

        const std::vector<uint8_t> px = c.toRGBA();
        if (px.size() != static_cast<size_t>(800) * 600 * 4) {
            std::printf("14_skia: toRGBA 尺寸异常\n");
            return 2;
        }
        // 像素校验（Canvas 坐标, 内存 top-down 行 0 = 顶部, 直接索引）
        all &= checkPx("fillRect 内部", px, 800, 600, 100, 90, 255, 128, 0, 20);
        all &= checkPx("fillCircle 中心", px, 800, 600, 400, 300, 46, 153, 181, 24);
        all &= checkPx("路径三角形", px, 800, 600, 640, 180, 46, 204, 113, 24);
        // 文本像素存在（黑色笔画, 容差放宽）
        const uint8_t* tp = &px[static_cast<size_t>(480) * 800 * 4 + 150 * 4];
        const bool textOk = tp[0] < 200 && tp[1] < 200 && tp[2] < 200;
        std::printf("  %-24s %s\n", "fillText 笔画", textOk ? "OK" : "FAIL");
        all &= textOk;
        all &= checkPx("背景", px, 800, 600, 750, 560, 240, 240, 240, 10);

        if (saveBmp("test/14_skia.bmp", px, 800, 600)) {
            std::printf("  已保存 test/14_skia.bmp (800x600)\n");
        } else {
            std::printf("  test/14_skia.bmp 写入失败\n");
            all = false;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
    std::printf("14_skia: %s\n", all ? "全部通过" : "存在失败项");
    return all ? 0 : 2;
}

#else
int main() { return 0; }
#endif

// bmp.cc - 帧读回与 BMP 编码实现
#include "bmp.h"

#include <glad/gl.h> // GL 函数（由 wbwopenglapi.hpp 引入的 glad 提供）

#include <algorithm>
#include <cstring>

namespace wbw_napi {

std::vector<unsigned char> readRgba(int fw, int fh, int x, int y, int w, int h) {
    std::vector<unsigned char> out(static_cast<size_t>(w) * h * 4, 0);
    if (w <= 0 || h <= 0 || fw <= 0 || fh <= 0) {
        return out;
    }
    // 与画布的交集（裁剪越界）
    const int cx0 = std::max(x, 0);
    const int cy0 = std::max(y, 0);
    const int cx1 = std::min(x + w, fw);
    const int cy1 = std::min(y + h, fh);
    if (cx1 <= cx0 || cy1 <= cy0) {
        return out;
    }
    const int cw = cx1 - cx0;
    const int ch = cy1 - cy0;
    // 一行暂存
    std::vector<unsigned char> row(static_cast<size_t>(cw) * 4);
    for (int yy = cy0; yy < cy1; ++yy) {
        // canvas y 向下 -> GL y 向上（GL 行 0 = 画布底行 fh-1）
        glReadPixels(cx0, fh - 1 - yy, cw, 1, GL_RGBA, GL_UNSIGNED_BYTE, row.data());
        const size_t dstOff = (static_cast<size_t>(yy - y) * w + (cx0 - x)) * 4;
        std::memcpy(&out[dstOff], row.data(), static_cast<size_t>(cw) * 4);
    }
    return out;
}

std::vector<unsigned char> encodeBmp(int fw, int fh) {
    std::vector<unsigned char> out;
    if (fw <= 0 || fh <= 0) {
        return out;
    }
    const int stride = ((fw * 3 + 3) / 4) * 4; // BMP 行填充 4 字节对齐
    const int imgSize = stride * fh;
    out.assign(static_cast<size_t>(54) + imgSize, 0);
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

    // 整帧 RGBA（GL 底行在前）
    std::vector<unsigned char> px(static_cast<size_t>(fw) * fh * 4);
    glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // GL 与 BMP 同为自下而上（BMP 高度为正时文件首行数据 = 图像底部行）:
    // px 行 0 = 图像底部 = BMP 文件行 0，逐行直拷，不得翻转
    for (int y = 0; y < fh; ++y) {
        const unsigned char* src = &px[static_cast<size_t>(y) * fw * 4];
        unsigned char* dst = &out[54 + static_cast<size_t>(y) * stride];
        for (int x = 0; x < fw; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2]; // B
            dst[x * 3 + 1] = src[x * 4 + 1]; // G
            dst[x * 3 + 2] = src[x * 4 + 0]; // R
        }
    }
    return out;
}

} // namespace wbw_napi
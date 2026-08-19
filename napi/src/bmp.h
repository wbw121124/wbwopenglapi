// bmp.h - 帧读回与 BMP 编码
//   约定：canvas 坐标系（y 向下、左上原点），行序与画布一致（顶行在前）
#ifndef WBW_NAPI_BMP_H
#define WBW_NAPI_BMP_H

#include <cstdint>
#include <vector>

namespace wbw_napi {

// 从当前 GL 默认 framebuffer（须已 resolve）读回矩形区域为 RGBA。
// x/y/w/h 为 canvas 坐标；越界部分填 0（GL 越界读会报错）。
// 返回长度 w*h*4，行序 = canvas 顶行在前。
std::vector<unsigned char> readRgba(int fw, int fh, int x, int y, int w, int h);

// 整帧编码为 24-bit BMP（54 字节头 + BGR 行，行填充 4 字节对齐）。
// 复刻示例 12 的 saveBmp 已验证逻辑：GL 与 BMP 同为自下而上行序，
// canvas 顶行 = BMP 顶行，图像正向。
std::vector<unsigned char> encodeBmp(int fw, int fh);

} // namespace wbw_napi

#endif // WBW_NAPI_BMP_H
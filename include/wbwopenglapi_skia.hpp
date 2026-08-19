#pragma once
//
// wbwopenglapi_skia.hpp - Skia 软件栅格化封装（可选图形后端，集成不替换）
// 目标: 提供与 wbwopenglapi 一致的 Canvas 坐标语义（左上原点、y 向下、单位像素），
//      经 SkSurface::MakeRaster 纯 CPU 栅格化输出 top-down RGBA8，
//      供无窗口/headless 渲染、纹理合成前处理或文件导出使用。
// 引入方式: vcpkg skia 端口（find_package(skia CONFIG)），编译时定义宏 WBWOPENGAL_API_SKIA。
//   构建: cmake -DWBWOPENGAL_API_SKIA=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
//   注意: skia 端口需 MSVC 工具链（MinGW 8.1 不可构建），CI 用 windows-msvc 验证；
//         本文件不依赖 wbwopenglapi.hpp，可独立编译。
// 边界: 文本为 Skia 自带字体栈（Segoe UI 等），不走 GDI/FreeType/DirectWrite 后端；
//       绘制接口为子集（矩形/圆/路径/文本/变换），与主库同语义部分一一对应。
//
#ifdef WBWOPENGAL_API_SKIA

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"

namespace wbwopenglapi {

// Skia 软件栅格化画布（Canvas 2D 风格子集）
class SkiaCanvas {
public:
    explicit SkiaCanvas(int w, int h) : w_(w), h_(h) {
        surf_ = SkSurface::MakeRasterN32Premul(w, h);
        if (!surf_) {
            throw std::runtime_error("wbwopenglapi_skia: SkSurface 创建失败");
        }
        typeface_ = SkTypeface::MakeFromName("Segoe UI", SkFontStyle::Normal());
        if (!typeface_) {
            typeface_ = SkTypeface::MakeDefault();
        }
    }

    int width() const { return w_; }
    int height() const { return h_; }

    // 清屏（含 alpha 重写，语义同 wbwopenglapi::Canvas::clear）
    void clear(float r, float g, float b, float a = 1.0f) {
        canvas()->clear(SkColor4f{r, g, b, a}.toSkColor());
    }
    // 样式: fill/stroke 共用同一画笔，调用方按操作类型先后设置（fillStyle 后 fill*，
    // strokeStyle 后 stroke*）；这与主库"自动按操作切换样式"略有差异
    void fillStyle(float r, float g, float b, float a = 1.0f) {
        paint_.setStyle(SkPaint::Style::kFill_Style);
        paint_.setColor(SkColor4f{r, g, b, a}.toSkColor());
    }
    void strokeStyle(float r, float g, float b, float a = 1.0f) {
        paint_.setStyle(SkPaint::Style::kStroke_Style);
        paint_.setColor(SkColor4f{r, g, b, a}.toSkColor());
    }
    void lineWidth(double w) { paint_.setStrokeWidth(static_cast<SkScalar>(w)); }

    void fillRect(double x, double y, double w, double h) {
        canvas()->drawRect(SkRect::MakeXYWH(static_cast<SkScalar>(x), static_cast<SkScalar>(y),
                                            static_cast<SkScalar>(w), static_cast<SkScalar>(h)),
                           paint_);
    }
    void strokeRect(double x, double y, double w, double h) {
        canvas()->drawRect(SkRect::MakeXYWH(static_cast<SkScalar>(x), static_cast<SkScalar>(y),
                                            static_cast<SkScalar>(w), static_cast<SkScalar>(h)),
                           paint_);
    }
    void fillCircle(double cx, double cy, double r) {
        canvas()->drawCircle(static_cast<SkScalar>(cx), static_cast<SkScalar>(cy),
                             static_cast<SkScalar>(r), paint_);
    }
    void strokeCircle(double cx, double cy, double r) {
        canvas()->drawCircle(static_cast<SkScalar>(cx), static_cast<SkScalar>(cy),
                             static_cast<SkScalar>(r), paint_);
    }
    void line(double x1, double y1, double x2, double y2) {
        canvas()->drawLine(static_cast<SkScalar>(x1), static_cast<SkScalar>(y1),
                           static_cast<SkScalar>(x2), static_cast<SkScalar>(y2), paint_);
    }

    // ---- 路径（beginPath 起新路径，语义同主库；arc 缺省顺时针）----
    void beginPath() { path_.reset(); }
    void moveTo(double x, double y) {
        path_.moveTo(static_cast<SkScalar>(x), static_cast<SkScalar>(y));
    }
    void lineTo(double x, double y) {
        path_.lineTo(static_cast<SkScalar>(x), static_cast<SkScalar>(y));
    }
    void quadraticCurveTo(double cx, double cy, double x, double y) {
        path_.quadTo(static_cast<SkScalar>(cx), static_cast<SkScalar>(cy),
                     static_cast<SkScalar>(x), static_cast<SkScalar>(y));
    }
    void bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x, double y) {
        path_.cubicTo(static_cast<SkScalar>(c1x), static_cast<SkScalar>(c1y),
                      static_cast<SkScalar>(c2x), static_cast<SkScalar>(c2y),
                      static_cast<SkScalar>(x), static_cast<SkScalar>(y));
    }
    void arc(double cx, double cy, double r, double a0, double a1, bool ccw = false) {
        double sweep = a1 - a0;
        if (ccw) {
            sweep = -(6.283185307179586 - sweep);
        }
        // 角度转度（Skia sweep 正值顺时针）；范围归一化避免极值
        SkScalar sweepDeg = static_cast<SkScalar>(sweep * 180.0 / 3.141592653589793);
        path_.arcTo(SkRect::MakeXYWH(static_cast<SkScalar>(cx - r), static_cast<SkScalar>(cy - r),
                                     static_cast<SkScalar>(r * 2.0), static_cast<SkScalar>(r * 2.0)),
                    static_cast<SkScalar>(a0 * 180.0 / 3.141592653589793), sweepDeg, true);
    }
    void closePath() { path_.close(); }
    void fill() { canvas()->drawPath(path_, paint_); }
    void stroke() { canvas()->drawPath(path_, paint_); }

    // ---- 变换（后调用先应用，同主库）----
    void translate(double dx, double dy) {
        canvas()->translate(static_cast<SkScalar>(dx), static_cast<SkScalar>(dy));
    }
    void rotate(double rad) { canvas()->rotate(static_cast<SkScalar>(rad * 180.0 / 3.141592653589793)); }
    void save() { canvas()->save(); }
    void restore() { canvas()->restore(); }

    // ---- 文本（y 为基线，同主库默认 Alphabetic 基线）----
    void font(double sizePx) { fontPx_ = sizePx; }
    void fillText(const std::string& text, double x, double y) {
        SkFont f(typeface_, static_cast<SkScalar>(fontPx_));
        canvas()->drawString(text, static_cast<SkScalar>(x), static_cast<SkScalar>(y), f, paint_);
    }

    // 输出 top-down RGBA8（行 0 = 图像顶部；与 glReadPixels 的 bottom-up 相反）
    std::vector<uint8_t> toRGBA() const {
        std::vector<uint8_t> px(static_cast<size_t>(w_) * static_cast<size_t>(h_) * 4, 0);
        sk_sp<SkImage> img = surf_->makeImageSnapshot();
        if (!img) {
            return px;
        }
        SkImageInfo info = SkImageInfo::Make(w_, h_, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
        if (!img->readPixels(info, px.data(), static_cast<size_t>(w_) * 4, 0, 0)) {
            px.assign(px.size(), 0);
        }
        return px;
    }

private:
    SkCanvas* canvas() { return surf_->getCanvas(); }

    int w_ = 0;
    int h_ = 0;
    sk_sp<SkSurface> surf_;
    SkPaint paint_;
    SkPath path_;
    double fontPx_ = 16.0;
    sk_sp<SkTypeface> typeface_;
};

} // namespace wbwopenglapi

#endif // WBWOPENGAL_API_SKIA

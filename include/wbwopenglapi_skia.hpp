#pragma once
//
// wbwopenglapi_skia.hpp - Skia 软件栅格化封装（可选图形后端，集成不替换）
// 目标: 提供与 wbwopenglapi 一致的 Canvas 坐标语义（左上原点、y 向下、单位像素），
//      经 SkSurface::MakeRaster 纯 CPU 栅格化输出 top-down RGBA8，
//      供无窗口/headless 渲染、纹理合成前处理或文件导出使用。
// 引入方式（二选一），编译时定义宏 WBWOPENGAL_API_SKIA:
//   A. vcpkg skia 端口: cmake -DWBWOPENGAL_API_SKIA=ON
//      -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
//      (find_package(skia CONFIG))
//   B. GN+LLVM 自建产物直连: cmake -DWBWOPENGAL_API_SKIA=ON
//      -DWBWOPENGAL_API_SKIA_DIR=<gn out 目录>（含 include/ 与 *.lib/*.a，
//      icudtl.dat 需拷到运行目录）
//   注意: skia 需 LLVM/MSVC 工具链（MinGW 8.1 不可构建），CI 用 GN+LLVM 验证；
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
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
// chrome/m152 已删除 SkFontMgr::RefDefault()；Windows 经平台工厂 SkFontMgr_New_DirectWrite
// 获取系统字体（GN 产物已编入 skia.lib；DWrite 由 SkDWrite 动态加载，链接期零额外库）。
// SkTypeface_win.h 内部以 SK_BUILD_FOR_WIN 守卫（SkFeatures.h 在 _WIN32 下定义）。
#if defined(SK_BUILD_FOR_WIN)
#include "include/ports/SkTypeface_win.h"
#endif

namespace wbwopenglapi {

// Skia 软件栅格化画布（Canvas 2D 风格子集）
class SkiaCanvas {
public:
    // chrome/m* 分支工厂已迁至 SkSurfaces / SkTypeface 本体仅剩 MakeEmpty 等：
    explicit SkiaCanvas(int w, int h) : w_(w), h_(h) {
        surf_ = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h));
        if (!surf_) {
            throw std::runtime_error("wbwopenglapi_skia: SkSurface 创建失败");
        }
        sk_sp<SkFontMgr> fm;
#if defined(SK_BUILD_FOR_WIN)
        // chrome/m152 无 SkFontMgr::RefDefault()，改用 DirectWrite 平台工厂（符号在 skia.lib）
        fm = SkFontMgr_New_DirectWrite();
#else
        // 非 Windows：RefEmpty 保编译（空 fontmgr 无字形，文本渲染受限；
        // 后续可接 SkFontMgr_New_Custom_Directory 等平台工厂）
        fm = SkFontMgr::RefEmpty();
#endif
        fontMgr_ = fm;
        if (fm) {
            typeface_ = fm->matchFamilyStyle("Segoe UI", SkFontStyle::Normal());
            if (!typeface_) {
                typeface_ = fm->matchFamilyStyle(nullptr, SkFontStyle::Normal());
            }
            if (!typeface_) {
                typeface_ = fm->makeFromFile("C:\\Windows\\Fonts\\segoeui.ttf", 0);
            }
        }
        if (!typeface_) {
            typeface_ = SkTypeface::MakeEmpty();
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
    // chrome/m* 分支 SkPath 仅保留只读接口，路径构建统一走 SkPathBuilder
    void beginPath() {
        pathBuilder_.reset();
        lastPath_ = SkPath();
    }
    void moveTo(double x, double y) {
        pathBuilder_.moveTo(static_cast<SkScalar>(x), static_cast<SkScalar>(y));
    }
    void lineTo(double x, double y) {
        pathBuilder_.lineTo(static_cast<SkScalar>(x), static_cast<SkScalar>(y));
    }
    void quadraticCurveTo(double cx, double cy, double x, double y) {
        pathBuilder_.quadTo(static_cast<SkScalar>(cx), static_cast<SkScalar>(cy),
                            static_cast<SkScalar>(x), static_cast<SkScalar>(y));
    }
    void bezierCurveTo(double c1x, double c1y, double c2x, double c2y, double x, double y) {
        pathBuilder_.cubicTo(static_cast<SkScalar>(c1x), static_cast<SkScalar>(c1y),
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
        pathBuilder_.arcTo(SkRect::MakeXYWH(static_cast<SkScalar>(cx - r), static_cast<SkScalar>(cy - r),
                                            static_cast<SkScalar>(r * 2.0), static_cast<SkScalar>(r * 2.0)),
                           static_cast<SkScalar>(a0 * 180.0 / 3.141592653589793), sweepDeg, true);
    }
    void closePath() { pathBuilder_.close(); }
    // fill/stroke 复用同一路径：首次 detach 出 SkPath 后缓存，后续操作沿用（语义同旧版）
    void fill() {
        lastPath_ = pathBuilder_.detach();
        canvas()->drawPath(lastPath_, paint_);
    }
    void stroke() {
        if (lastPath_.isEmpty()) {
            lastPath_ = pathBuilder_.detach();
        }
        canvas()->drawPath(lastPath_, paint_);
    }

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
        canvas()->drawString(text.c_str(), static_cast<SkScalar>(x), static_cast<SkScalar>(y), f, paint_);
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

    // 诊断：字体链路状态（m152 DirectWrite 工厂调试用；示例 14_skia 校验前打印）
    std::string fontStatus() const {
        std::string s;
        if (fontMgr_) {
            s = "fontmgr=ok families=" + std::to_string(fontMgr_->countFamilies());
        } else {
            s = "fontmgr=null";
        }
        if (typeface_) {
            SkString name;
            typeface_->getFamilyName(&name);
            s += " typeface=" + std::string(name.c_str());
        } else {
            s += " typeface=null";
        }
        return s;
    }

private:
    SkCanvas* canvas() { return surf_->getCanvas(); }

    int w_ = 0;
    int h_ = 0;
    sk_sp<SkSurface> surf_;
    SkPaint paint_;
    SkPathBuilder pathBuilder_;
    SkPath lastPath_;
    double fontPx_ = 16.0;
    sk_sp<SkFontMgr> fontMgr_;
    sk_sp<SkTypeface> typeface_;
};

} // namespace wbwopenglapi

#endif // WBWOPENGAL_API_SKIA

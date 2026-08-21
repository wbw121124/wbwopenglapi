// renderer_wrap.h - Canvas 完整 2D API 的 Node 绑定（Napi::ObjectWrap）
#ifndef WBW_NAPI_RENDERER_WRAP_H
#define WBW_NAPI_RENDERER_WRAP_H

#include <napi.h>

#include <memory>

#include "renderer.h"

namespace wbw_napi {

// env 键控的类构造函数引用集：worker_threads 下每个 env 独立持有
// （napi_ref 绑定创建它的 env，跨 env 使用不安全）。单槽位实例数据
// 须用一个结构体承载全部类的引用；env 销毁时 SetInstanceData 默认
// finalize 自动 delete。
struct EnvRefs {
    Napi::FunctionReference renderer;
    Napi::FunctionReference image;
    Napi::FunctionReference gradient;
};

inline EnvRefs& envRefs(Napi::Env env) {
    EnvRefs* refs = env.GetInstanceData<EnvRefs>();
    if (!refs) {
        refs = new EnvRefs();
        env.SetInstanceData(refs);
    }
    return *refs;
}

// loadBMP(path) -> ImageHandle（RGBA 位图，无 GL 资源，析构安全）
class ImageWrap : public Napi::ObjectWrap<ImageWrap> {
public:
    static void Init(Napi::Env env, Napi::Object exports);
    ImageWrap(const Napi::CallbackInfo& info);
    wbwopenglapi::Image img;

    Napi::Value GetWidth(const Napi::CallbackInfo& i);
    Napi::Value GetHeight(const Napi::CallbackInfo& i);
    Napi::Value GetRgba(const Napi::CallbackInfo& i); // RGBA Buffer（拷贝）
#ifdef WBWOPENGAL_API_PNG
    Napi::Value ToPng(const Napi::CallbackInfo& i);
#endif

    // env 键控构造函数引用（见 EnvRefs）
    static Napi::FunctionReference& Ctor(Napi::Env env);
};

// createLinearGradient / createRadialGradient 的返回值（GradientHandle）。
// 纯数据对象（无 GL 资源）；fillStyle/strokeStyle 接受其实例。
class GradientWrap : public Napi::ObjectWrap<GradientWrap> {
public:
    static void Init(Napi::Env env, Napi::Object exports);
    GradientWrap(const Napi::CallbackInfo& info);
    wbwopenglapi::Gradient grad;

    Napi::Value AddColorStop(const Napi::CallbackInfo& i);

    static Napi::FunctionReference& Ctor(Napi::Env env);
};

// createCanvas(w, h) -> CanvasHandle（隐藏窗口 + 绘制上下文）
class RendererWrap : public Napi::ObjectWrap<RendererWrap> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);

    RendererWrap(const Napi::CallbackInfo& info);
    void Finalize(Napi::Env env) override; // GC 兜底：入 pending 队列延迟释放

    // env 键控构造函数引用（见 EnvRefs）
    static Napi::FunctionReference& Ctor(Napi::Env env);

private:
    friend class GradientWrap; // 复用参数解析工具（Num/ColorArg/RequireArgs）
    std::shared_ptr<Renderer> r_; // 为空 = 已 close

    // ---- 工具 ----
    // 取底层引用；已 close 时抛 JS Error（E_CLOSED 语义由 JS 层包装）
    Renderer& get();
    // 帧协议：读回前确保离屏结果已合成到默认 framebuffer（幂等）
    static void ensureResolved(Renderer& r);
    // 参数解析（类型错抛 TypeError；数字含 NaN/Infinity 抛 RangeError）
    static double Num(const Napi::CallbackInfo& i, size_t idx, double def);
    static std::string Str(const Napi::CallbackInfo& i, size_t idx,
                           const std::string& def);
    static bool Bool(const Napi::CallbackInfo& i, size_t idx, bool def);
    static wbwopenglapi::Color ColorArg(const Napi::CallbackInfo& i, size_t idx,
                                        const wbwopenglapi::Color& def);
    static void RequireArgs(const Napi::CallbackInfo& i, size_t n,
                            const char* name);
    static Napi::Object ToObject(const Napi::CallbackInfo& i, size_t idx);
    // 参数是否为 GradientHandle 实例（InstanceOf 判定，避免误 Unwrap）
    static bool IsGradient(const Napi::CallbackInfo& i, size_t idx);

    // ---- 方法 ----
    // 样式
    Napi::Value Clear(const Napi::CallbackInfo& i);
    Napi::Value FillStyle(const Napi::CallbackInfo& i);
    Napi::Value StrokeStyle(const Napi::CallbackInfo& i);
    Napi::Value LineWidth(const Napi::CallbackInfo& i);
    Napi::Value GlobalAlpha(const Napi::CallbackInfo& i);
    Napi::Value LineAlgorithm(const Napi::CallbackInfo& i);
    Napi::Value Antialias(const Napi::CallbackInfo& i);
    Napi::Value Font(const Napi::CallbackInfo& i);
    Napi::Value TextAlign(const Napi::CallbackInfo& i);
    Napi::Value TextBaseline(const Napi::CallbackInfo& i);
    Napi::Value FontFeatures(const Napi::CallbackInfo& i);
    Napi::Value ResetFontFeatures(const Napi::CallbackInfo& i);
    Napi::Value GlobalCompositeOperation(const Napi::CallbackInfo& i);
    Napi::Value CreateLinearGradient(const Napi::CallbackInfo& i);
    Napi::Value CreateRadialGradient(const Napi::CallbackInfo& i);
    // 矩形
    Napi::Value FillRect(const Napi::CallbackInfo& i);
    Napi::Value StrokeRect(const Napi::CallbackInfo& i);
    Napi::Value ClearRect(const Napi::CallbackInfo& i);
    // 路径
    Napi::Value BeginPath(const Napi::CallbackInfo& i);
    Napi::Value MoveTo(const Napi::CallbackInfo& i);
    Napi::Value LineTo(const Napi::CallbackInfo& i);
    Napi::Value QuadraticCurveTo(const Napi::CallbackInfo& i);
    Napi::Value BezierCurveTo(const Napi::CallbackInfo& i);
    Napi::Value Arc(const Napi::CallbackInfo& i);
    Napi::Value Ellipse(const Napi::CallbackInfo& i);
    Napi::Value RoundRect(const Napi::CallbackInfo& i);
    Napi::Value Rect(const Napi::CallbackInfo& i);
    Napi::Value ClosePath(const Napi::CallbackInfo& i);
    Napi::Value Clip(const Napi::CallbackInfo& i);
    Napi::Value Fill(const Napi::CallbackInfo& i);
    Napi::Value Stroke(const Napi::CallbackInfo& i);
    // 文本
    Napi::Value FillText(const Napi::CallbackInfo& i);
    Napi::Value StrokeText(const Napi::CallbackInfo& i);
    Napi::Value MeasureText(const Napi::CallbackInfo& i);
    // 变换
    Napi::Value Translate(const Napi::CallbackInfo& i);
    Napi::Value Rotate(const Napi::CallbackInfo& i);
    Napi::Value Save(const Napi::CallbackInfo& i);
    Napi::Value Restore(const Napi::CallbackInfo& i);
    Napi::Value ResetTransform(const Napi::CallbackInfo& i);
    // 图像
    Napi::Value DrawImage(const Napi::CallbackInfo& i);
    // 帧 / 输出
    Napi::Value Resolve(const Napi::CallbackInfo& i);
    Napi::Value ReadPixels(const Napi::CallbackInfo& i);
    Napi::Value ToBmp(const Napi::CallbackInfo& i);
#ifdef WBWOPENGAL_API_PNG
    Napi::Value ToPng(const Napi::CallbackInfo& i); // 整帧编码 PNG Buffer
#endif
    Napi::Value SwapBuffers(const Napi::CallbackInfo& i);
    Napi::Value Close(const Napi::CallbackInfo& i);

    // 静态模块级
    static Napi::Value LoadBmp(const Napi::CallbackInfo& i);
#ifdef WBWOPENGAL_API_PNG
    static Napi::Value LoadPng(const Napi::CallbackInfo& i);  // 路径或 Buffer
    static Napi::Value SavePng(const Napi::CallbackInfo& i);  // (ImageHandle, path)
#endif
    static Napi::Value CreateCanvas(const Napi::CallbackInfo& i);
    // 属性 getter
    Napi::Value GetWidth(const Napi::CallbackInfo& i);
    Napi::Value GetHeight(const Napi::CallbackInfo& i);
};

} // namespace wbw_napi

#endif // WBW_NAPI_RENDERER_WRAP_H
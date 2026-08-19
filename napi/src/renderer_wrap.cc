// renderer_wrap.cc - Canvas 完整 2D API 的 Node 绑定实现
#include "renderer_wrap.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "bmp.h"

namespace wbw_napi {

// env 键控构造函数引用：worker_threads 下每个 env 独立持有自己的
// FunctionReference（napi_ref 绑定创建它的 env）。SetInstanceData 的
// 默认 finalize 在 env 销毁时自动 delete。
Napi::FunctionReference& ImageWrap::Ctor(Napi::Env env) {
    return envRefs(env).image;
}

Napi::FunctionReference& RendererWrap::Ctor(Napi::Env env) {
    return envRefs(env).renderer;
}

// ---------------------------------------------------------------------
// ImageWrap
// ---------------------------------------------------------------------
void ImageWrap::Init(Napi::Env env, Napi::Object exports) {
    // GCC 8.1 对 DefineClass 的 braced-init-list 模板推导有歧义：显式 vector；
    // InstanceAccessor 单参版须显式模板实参（非类型模板参数不可推导）
    const std::vector<Napi::ClassPropertyDescriptor<ImageWrap>> props = {
        InstanceAccessor<&ImageWrap::GetWidth>("width"),
        InstanceAccessor<&ImageWrap::GetHeight>("height"),
    };
    Napi::Function func = DefineClass(env, "ImageHandle", props);
    Ctor(env) = Napi::Persistent(func);
    exports.Set("ImageHandle", func);
}

ImageWrap::ImageWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ImageWrap>(info) {
    // 仅由 loadBMP 内部构造
}

Napi::Value ImageWrap::GetWidth(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), img.width);
}

Napi::Value ImageWrap::GetHeight(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), img.height);
}

// ---------------------------------------------------------------------
// 参数工具
// ---------------------------------------------------------------------
Renderer& RendererWrap::get() {
    if (!r_ || r_->closed) {
        throw std::runtime_error("wbwopenglapi: canvas 已关闭");
    }
    return *r_;
}

void RendererWrap::ensureResolved(Renderer& r) {
    // 帧协议：绘制进离屏 FBO（off 模式 1x），present() 合成到默认
    // framebuffer 的 back buffer 后，glReadPixels 读回的才是当前帧
    r.ctx.resolve();
}

double RendererWrap::Num(const Napi::CallbackInfo& i, size_t idx, double def) {
    if (idx >= i.Length()) {
        return def;
    }
    const Napi::Value v = i[idx];
    if (!v.IsNumber()) {
        throw Napi::TypeError::New(i.Env(), "wbwopenglapi: 参数必须是数字");
    }
    const double d = v.As<Napi::Number>().DoubleValue();
    if (std::isnan(d) || std::isinf(d)) {
        throw Napi::RangeError::New(i.Env(), "wbwopenglapi: 数字参数不能为 NaN/Infinity");
    }
    return d;
}

std::string RendererWrap::Str(const Napi::CallbackInfo& i, size_t idx,
                              const std::string& def) {
    if (idx >= i.Length()) {
        return def;
    }
    const Napi::Value v = i[idx];
    if (v.IsString()) {
        return v.As<Napi::String>().Utf8Value();
    }
if (v.IsNumber()) { // 兼容数字转字符串（如字号）
        return v.ToString().Utf8Value();
    }
    throw Napi::TypeError::New(i.Env(), "wbwopenglapi: 参数必须是字符串");
}

bool RendererWrap::Bool(const Napi::CallbackInfo& i, size_t idx, bool def) {
    if (idx >= i.Length()) {
        return def;
    }
    const Napi::Value v = i[idx];
    if (!v.IsBoolean()) {
        throw Napi::TypeError::New(i.Env(), "wbwopenglapi: 参数必须是布尔值");
    }
    return v.As<Napi::Boolean>().Value();
}

Napi::Object RendererWrap::ToObject(const Napi::CallbackInfo& i, size_t idx) {
    if (idx >= i.Length() || !i[idx].IsObject()) {
        throw Napi::TypeError::New(i.Env(), "wbwopenglapi: 参数必须是对象");
    }
    return i[idx].As<Napi::Object>();
}

void RendererWrap::RequireArgs(const Napi::CallbackInfo& i, size_t n,
                               const char* name) {
    if (i.Length() < n) {
        throw Napi::TypeError::New(i.Env(),
                                   std::string("wbwopenglapi: ") + name +
                                       " 需要至少 " + std::to_string(n) + " 个参数");
    }
}

// CSS 字符串或 {r,g,b,a} 对象 / [r,g,b,a] 数组（整体判定 0..255 或 0..1 量纲）
wbwopenglapi::Color RendererWrap::ColorArg(const Napi::CallbackInfo& i,
                                           size_t idx,
                                           const wbwopenglapi::Color& def) {
    if (idx >= i.Length()) {
        return def;
    }
    const Napi::Value v = i[idx];
    if (v.IsString()) {
        return wbwopenglapi::parseColor(v.As<Napi::String>().Utf8Value());
    }
    if (v.IsArray()) {
        const Napi::Object o = v.As<Napi::Object>();
        const uint32_t n = o.As<Napi::Array>().Length();
        if (n < 3) {
            throw Napi::TypeError::New(i.Env(),
                                       "wbwopenglapi: 颜色数组须为 [r,g,b,a]");
        }
        auto num = [&](uint32_t k) {
            const Napi::Value x = o.Get(k);
            if (!x.IsNumber()) {
                throw Napi::TypeError::New(i.Env(),
                                           "wbwopenglapi: 颜色数组元素须为数字");
            }
            return x.As<Napi::Number>().DoubleValue();
        };
        // r/g/b 整体判定量纲：任一分量 >1 视为 0..255 制（Canvas 规范），
        // 全 ≤1 视为 0..1；a 固定 0..1 制（>1 兼容 0..255 写法）
        const double q0 = num(0), q1 = num(1), q2 = num(2);
        const double q3 = n >= 4 ? num(3) : 1.0;
        const double mx = std::max(std::max(q0, q1), q2);
        const double f = mx > 1.0 ? 1.0 / 255.0 : 1.0;
        wbwopenglapi::Color c;
        c.r = static_cast<float>(q0 * f);
        c.g = static_cast<float>(q1 * f);
        c.b = static_cast<float>(q2 * f);
        c.a = static_cast<float>(q3 > 1.0 ? q3 / 255.0 : q3);
        return c;
    }
    if (v.IsObject()) {
        const Napi::Object o = v.As<Napi::Object>();
        wbwopenglapi::Color c = def;
        const Napi::Value r = o.Get("r");
        const Napi::Value g = o.Get("g");
        const Napi::Value b = o.Get("b");
        const Napi::Value a = o.Get("a");
        if (!r.IsNumber() || !g.IsNumber() || !b.IsNumber()) {
            throw Napi::TypeError::New(i.Env(),
                                       "wbwopenglapi: 颜色对象须含 r/g/b 数字");
        }
        // r/g/b 整体判定量纲（同上）；a 固定 0..1 制（>1 兼容 0..255 写法）
        const double q0 = r.As<Napi::Number>().DoubleValue();
        const double q1 = g.As<Napi::Number>().DoubleValue();
        const double q2 = b.As<Napi::Number>().DoubleValue();
        const double q3 = a.IsNumber() ? a.As<Napi::Number>().DoubleValue() : 1.0;
        const double mx = std::max(std::max(q0, q1), q2);
        const double f = mx > 1.0 ? 1.0 / 255.0 : 1.0;
        c.r = static_cast<float>(q0 * f);
        c.g = static_cast<float>(q1 * f);
        c.b = static_cast<float>(q2 * f);
        if (a.IsNumber()) {
            c.a = static_cast<float>(q3 > 1.0 ? q3 / 255.0 : q3);
        } else {
            c.a = 1.0f;
        }
        return c;
    }
    throw Napi::TypeError::New(i.Env(), "wbwopenglapi: 颜色须为 CSS 字符串或 {r,g,b,a} 对象");
}

// ---------------------------------------------------------------------
// RendererWrap 方法
// ---------------------------------------------------------------------
Napi::Object RendererWrap::Init(Napi::Env env, Napi::Object exports) {
    // GCC 8.1 对 DefineClass 的 braced-init-list 模板推导有歧义：显式 vector
const std::vector<Napi::ClassPropertyDescriptor<RendererWrap>> props = {
            InstanceAccessor<&RendererWrap::GetWidth>("width"),
            InstanceAccessor<&RendererWrap::GetHeight>("height"),
            // 样式
            InstanceMethod("clear", &RendererWrap::Clear),
            InstanceMethod("fillStyle", &RendererWrap::FillStyle),
            InstanceMethod("strokeStyle", &RendererWrap::StrokeStyle),
            InstanceMethod("lineWidth", &RendererWrap::LineWidth),
            InstanceMethod("globalAlpha", &RendererWrap::GlobalAlpha),
            InstanceMethod("lineAlgorithm", &RendererWrap::LineAlgorithm),
            InstanceMethod("antialias", &RendererWrap::Antialias),
            InstanceMethod("font", &RendererWrap::Font),
            InstanceMethod("textAlign", &RendererWrap::TextAlign),
            InstanceMethod("textBaseline", &RendererWrap::TextBaseline),
            InstanceMethod("fontFeatures", &RendererWrap::FontFeatures),
            InstanceMethod("resetFontFeatures", &RendererWrap::ResetFontFeatures),
            // 矩形
            InstanceMethod("fillRect", &RendererWrap::FillRect),
            InstanceMethod("strokeRect", &RendererWrap::StrokeRect),
            InstanceMethod("clearRect", &RendererWrap::ClearRect),
            // 路径
            InstanceMethod("beginPath", &RendererWrap::BeginPath),
            InstanceMethod("moveTo", &RendererWrap::MoveTo),
            InstanceMethod("lineTo", &RendererWrap::LineTo),
            InstanceMethod("quadraticCurveTo", &RendererWrap::QuadraticCurveTo),
            InstanceMethod("bezierCurveTo", &RendererWrap::BezierCurveTo),
            InstanceMethod("arc", &RendererWrap::Arc),
            InstanceMethod("rect", &RendererWrap::Rect),
            InstanceMethod("closePath", &RendererWrap::ClosePath),
            InstanceMethod("fill", &RendererWrap::Fill),
            InstanceMethod("stroke", &RendererWrap::Stroke),
            // 文本
            InstanceMethod("fillText", &RendererWrap::FillText),
            InstanceMethod("strokeText", &RendererWrap::StrokeText),
            InstanceMethod("measureText", &RendererWrap::MeasureText),
            // 变换
            InstanceMethod("translate", &RendererWrap::Translate),
            InstanceMethod("rotate", &RendererWrap::Rotate),
            InstanceMethod("save", &RendererWrap::Save),
            InstanceMethod("restore", &RendererWrap::Restore),
            InstanceMethod("resetTransform", &RendererWrap::ResetTransform),
            // 图像
            InstanceMethod("drawImage", &RendererWrap::DrawImage),
            // 帧 / 输出
            InstanceMethod("resolve", &RendererWrap::Resolve),
            InstanceMethod("readPixels", &RendererWrap::ReadPixels),
            InstanceMethod("toBMP", &RendererWrap::ToBmp),
            InstanceMethod("swapBuffers", &RendererWrap::SwapBuffers),
            InstanceMethod("close", &RendererWrap::Close),
    };
    Napi::Function func = DefineClass(env, "CanvasHandle", props);
    RendererWrap::Ctor(env) = Napi::Persistent(func);
    exports.Set("CanvasHandle", func);
    exports.Set("createCanvas", Napi::Function::New(env, CreateCanvas));
    exports.Set("loadBMP", Napi::Function::New(env, LoadBmp));
    return exports;
}

RendererWrap::RendererWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<RendererWrap>(info) {
    if (info.Length() >= 2) {
        const double w = Num(info, 0, 0);
        const double h = Num(info, 1, 0);
        if (w <= 0 || h <= 0) {
            throw Napi::RangeError::New(info.Env(),
                                        "wbwopenglapi: 画布宽高必须为正数");
        }
        // GLFW/GL 初始化失败（无头会话、驱动过旧等）在此抛出
        r_ = std::make_shared<Renderer>(static_cast<int>(w), static_cast<int>(h));
    }
}

void RendererWrap::Finalize(Napi::Env /*env*/) {
    // GC 兜底：仅入队，由下次任意入口 flush（主线程 + 恢复 GL 上下文后释放）
    if (r_ && !r_->closed) {
        pendingCleanup().push_back(std::move(r_));
    }
}

Napi::Value RendererWrap::CreateCanvas(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    try {
        flushPendingCleanup(); // 兜底释放队列（新实例保证 GL 已可用）
        RequireArgs(info, 2, "createCanvas");
        const double w = Num(info, 0, 0);
        const double h = Num(info, 1, 0);
        if (w <= 0 || h <= 0) {
            throw Napi::RangeError::New(env, "wbwopenglapi: 画布宽高必须为正数");
        }
        // 经类构造函数创建（RendererWrap 构造解析参数并初始化 GL，失败即抛）
        return RendererWrap::Ctor(env).New(
            {Napi::Number::New(env, w), Napi::Number::New(env, h)});
    } catch (const Napi::Error& e) {
        // NAPI 异常（TypeError/RangeError）直接透传
        e.ThrowAsJavaScriptException();
        return env.Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("wbwopenglapi: ") + e.what())
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value RendererWrap::LoadBmp(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    try {
        RequireArgs(info, 1, "loadBMP");
        const std::string path = Str(info, 0, "");
        const wbwopenglapi::Image img = wbwopenglapi::loadBMP(path); // 失败抛异常
        Napi::Object obj =
            ImageWrap::Ctor(env).New({Napi::String::New(env, path)});
        ImageWrap::Unwrap(obj)->img = img;
        return obj;
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return env.Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("wbwopenglapi: ") + e.what())
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value RendererWrap::GetWidth(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        return Napi::Number::New(info.Env(), r.ctx.width());
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::GetHeight(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        return Napi::Number::New(info.Env(), r.ctx.height());
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

// ---------------- 样式 ----------------

Napi::Value RendererWrap::Clear(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        const wbwopenglapi::Color c = ColorArg(info, 0, wbwopenglapi::Color{1, 1, 1, 1});
        r.ctx.clear(c);
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::FillStyle(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.fillStyle(ColorArg(info, 0, wbwopenglapi::Color{0, 0, 0, 1}));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::StrokeStyle(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.strokeStyle(ColorArg(info, 0, wbwopenglapi::Color{0, 0, 0, 1}));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::LineWidth(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.lineWidth(Num(info, 0, 1.0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::GlobalAlpha(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.globalAlpha(Num(info, 0, 1.0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::LineAlgorithm(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.lineAlgorithm(Str(info, 0, "default"));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Antialias(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.antialias(Str(info, 0, "off"));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Font(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.font(Str(info, 0, "16px sans-serif"));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::TextAlign(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        const std::string s = Str(info, 0, "left");
        using wbwopenglapi::TextAlign;
        if (s == "center") {
            r.ctx.textAlign(TextAlign::Center);
        } else if (s == "right") {
            r.ctx.textAlign(TextAlign::Right);
        } else {
            r.ctx.textAlign(TextAlign::Left); // 未知值回退默认（宽容，与库一致）
        }
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::TextBaseline(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        const std::string s = Str(info, 0, "alphabetic");
        using wbwopenglapi::TextBaseline;
        if (s == "top") {
            r.ctx.textBaseline(TextBaseline::Top);
        } else if (s == "middle") {
            r.ctx.textBaseline(TextBaseline::Middle);
        } else if (s == "bottom") {
            r.ctx.textBaseline(TextBaseline::Bottom);
        } else {
            r.ctx.textBaseline(TextBaseline::Alphabetic); // 未知值回退默认
        }
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::FontFeatures(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.fontFeatures(Str(info, 0, ""));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::ResetFontFeatures(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.resetFontFeatures();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

// ---------------- 矩形 ----------------

Napi::Value RendererWrap::FillRect(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.fillRect(Num(info, 0, 0), Num(info, 1, 0), Num(info, 2, 0),
                       Num(info, 3, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::StrokeRect(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.strokeRect(Num(info, 0, 0), Num(info, 1, 0), Num(info, 2, 0),
                         Num(info, 3, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::ClearRect(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        r.ctx.clearRect(Num(info, 0, 0), Num(info, 1, 0), Num(info, 2, 0),
                        Num(info, 3, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

// ---------------- 路径 ----------------

Napi::Value RendererWrap::BeginPath(const Napi::CallbackInfo& info) {
    try {
        get().ctx.beginPath();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::MoveTo(const Napi::CallbackInfo& info) {
    try {
        get().ctx.moveTo(Num(info, 0, 0), Num(info, 1, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::LineTo(const Napi::CallbackInfo& info) {
    try {
        get().ctx.lineTo(Num(info, 0, 0), Num(info, 1, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::QuadraticCurveTo(const Napi::CallbackInfo& info) {
    try {
        get().ctx.quadraticCurveTo(Num(info, 0, 0), Num(info, 1, 0),
                                   Num(info, 2, 0), Num(info, 3, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::BezierCurveTo(const Napi::CallbackInfo& info) {
    try {
        get().ctx.bezierCurveTo(Num(info, 0, 0), Num(info, 1, 0), Num(info, 2, 0),
                                Num(info, 3, 0), Num(info, 4, 0), Num(info, 5, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Arc(const Napi::CallbackInfo& info) {
    try {
        get().ctx.arc(Num(info, 0, 0), Num(info, 1, 0), Num(info, 2, 0),
                      Num(info, 3, 0), Num(info, 4, 0), Bool(info, 5, false));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Rect(const Napi::CallbackInfo& info) {
    try {
        get().ctx.rect(Num(info, 0, 0), Num(info, 1, 0), Num(info, 2, 0),
                       Num(info, 3, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::ClosePath(const Napi::CallbackInfo& info) {
    try {
        get().ctx.closePath();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Fill(const Napi::CallbackInfo& info) {
    try {
        get().ctx.fill();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Stroke(const Napi::CallbackInfo& info) {
    try {
        get().ctx.stroke();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

// ---------------- 文本 ----------------

Napi::Value RendererWrap::FillText(const Napi::CallbackInfo& info) {
    try {
        get().ctx.fillText(Str(info, 0, ""), Num(info, 1, 0), Num(info, 2, 0),
                           Num(info, 3, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::StrokeText(const Napi::CallbackInfo& info) {
    try {
        get().ctx.strokeText(Str(info, 0, ""), Num(info, 1, 0), Num(info, 2, 0),
                             Num(info, 3, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::MeasureText(const Napi::CallbackInfo& info) {
    try {
        const double w = get().ctx.measureText(Str(info, 0, ""));
        return Napi::Number::New(info.Env(), w);
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

// ---------------- 变换 ----------------

Napi::Value RendererWrap::Translate(const Napi::CallbackInfo& info) {
    try {
        get().ctx.translate(Num(info, 0, 0), Num(info, 1, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Rotate(const Napi::CallbackInfo& info) {
    try {
        get().ctx.rotate(Num(info, 0, 0));
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Save(const Napi::CallbackInfo& info) {
    try {
        get().ctx.save();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Restore(const Napi::CallbackInfo& info) {
    try {
        get().ctx.restore();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::ResetTransform(const Napi::CallbackInfo& info) {
    try {
        get().ctx.resetTransform();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

// ---------------- 图像 ----------------

Napi::Value RendererWrap::DrawImage(const Napi::CallbackInfo& info) {
    try {
        Renderer& r = get();
        ImageWrap* imgWrap = nullptr;
        if (info.Length() < 1 || !info[0].IsObject()) {
            throw Napi::TypeError::New(
                info.Env(), "wbwopenglapi: drawImage 首个参数须为 loadBMP 返回的图像");
        }
        try {
            imgWrap = Napi::ObjectWrap<ImageWrap>::Unwrap(info[0].As<Napi::Object>());
        } catch (const Napi::Error&) {
            imgWrap = nullptr; // 非 ImageHandle 实例
        }
        if (imgWrap == nullptr) {
            throw Napi::TypeError::New(
                info.Env(), "wbwopenglapi: drawImage 首个参数须为 loadBMP 返回的图像");
        }
        // 参数 1..4：x y w h（缺省 0）
        const double x = Num(info, 1, 0);
        const double y = Num(info, 2, 0);
        const double w = Num(info, 3, 0);
        const double h = Num(info, 4, 0);
        r.ctx.drawImage(imgWrap->img, x, y, w, h);
        return info.Env().Undefined();
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

// ---------------- 帧 / 输出 ----------------

Napi::Value RendererWrap::Resolve(const Napi::CallbackInfo& info) {
    try {
        get().ctx.resolve();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::ReadPixels(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    try {
        Renderer& r = get();
        ensureResolved(r);
        const double x = Num(info, 0, 0);
        const double y = Num(info, 1, 0);
        const double w = Num(info, 2, 0);
        const double h = Num(info, 3, 0);
        if (w < 0 || h < 0 || x < -2147483647 || y < -2147483647 ||
            w > 2147483647 || h > 2147483647) {
            throw Napi::RangeError::New(env, "wbwopenglapi: readPixels 参数越界");
        }
        const int fw = r.win.framebufferWidth();
        const int fh = r.win.framebufferHeight();
        const std::vector<unsigned char> px = readRgba(
            fw, fh, static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
            static_cast<int>(h));
        Napi::Buffer<unsigned char> buf =
            Napi::Buffer<unsigned char>::New(env, px.size());
        std::memcpy(buf.Data(), px.data(), px.size());
        return buf;
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return env.Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value RendererWrap::ToBmp(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    try {
        Renderer& r = get();
        ensureResolved(r);
        const int fw = r.win.framebufferWidth();
        const int fh = r.win.framebufferHeight();
        const std::vector<unsigned char> bmp = encodeBmp(fw, fh);
        Napi::Buffer<unsigned char> buf =
            Napi::Buffer<unsigned char>::New(env, bmp.size());
        std::memcpy(buf.Data(), bmp.data(), bmp.size());
        return buf;
    } catch (const Napi::Error& e) {
        e.ThrowAsJavaScriptException();
        return env.Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

Napi::Value RendererWrap::SwapBuffers(const Napi::CallbackInfo& info) {
    try {
        get().win.swapBuffers();
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

Napi::Value RendererWrap::Close(const Napi::CallbackInfo& info) {
    try {
        if (r_ && !r_->closed) {
            // 显式释放：恢复 GL 上下文后析构全部资源（主线程，安全）
            glfwMakeContextCurrent(r_->win.nativeHandle());
            r_->closed = true;
            r_.reset();
        }
        return info.Env().Undefined();
    } catch (const std::exception& e) {
        Napi::Error::New(info.Env(), e.what()).ThrowAsJavaScriptException();
        return info.Env().Undefined();
    }
}

} // namespace wbw_napi

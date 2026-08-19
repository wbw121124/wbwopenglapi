// addon.cc - wbwopenglapi Node-API 插件入口（阶段1：构建链冒烟）
//   renderTest(): 隐藏窗口 + GL 上下文 + clear/fillRect + glReadPixels 回读
//   验证 MinGW 下 node-addon-api / node.lib / glfw3dll 链接与隐藏窗口渲染全链路
//   注意事项：
//   - Canvas 绘制走离屏 FBO（off 模式 1x），读回默认 framebuffer 前须
//     调用 resolve()（= present()），与示例 12 的验证协议一致
//   - glReadPixels 的 y 原点在左下，canvas 坐标（y 向下）须翻转 fh-1-y
#include <mutex> // 先于 napi.h：MinGW libstdc++ 下 node-addon-api 需要
#include <napi.h>
#include <wbwopenglapi.hpp>

#include <exception>
#include <string>

namespace {

Napi::Value Ping(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), "pong");
}

Napi::Value RenderTest(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    try {
        // visible=false：隐藏窗口（无头渲染，供 Node.js 绑定使用）
        wbwopenglapi::Window win(64, 64, "wbwopenglapi-node", false, false);
        wbwopenglapi::Canvas ctx(win);

        ctx.clear("#ff8000");
        ctx.fillStyle("#204060");
        ctx.fillRect(8, 8, 16, 16);
        ctx.resolve(); // present：离屏 FBO -> 默认 framebuffer back

        const int fw = win.framebufferWidth();
        const int fh = win.framebufferHeight();
        // canvas 坐标 -> GL 坐标（y 翻转）
        const auto readPx = [&](int x, int y) {
            unsigned char p[4] = {0, 0, 0, 0};
            glReadPixels(x, fh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
            Napi::Array a = Napi::Array::New(env, 4);
            for (uint32_t i = 0; i < 4; ++i) {
                a.Set(i, Napi::Number::New(env, p[i]));
            }
            return a;
        };

        Napi::Object obj = Napi::Object::New(env);
        obj.Set("w", Napi::Number::New(env, win.width()));
        obj.Set("h", Napi::Number::New(env, win.height()));
        obj.Set("fw", Napi::Number::New(env, fw));
        obj.Set("fh", Napi::Number::New(env, fh));
        obj.Set("glErr", Napi::Number::New(env, glGetError()));
        obj.Set("glVer",
                Napi::String::New(env, reinterpret_cast<const char*>(glGetString(GL_VERSION))));
        // 背景点 (60, 30) 应为 #ff8000
        obj.Set("bg", readPx(60, 30));
        // 矩形内逻辑点 (12, 12) 按 framebuffer 比例缩放后应为 #204060
        const int rx = static_cast<int>((12.0 * fw) / 64.0 + 0.5);
        const int ry = static_cast<int>((12.0 * fh) / 64.0 + 0.5);
        obj.Set("rect", readPx(rx, ry));
        return obj;
    } catch (const std::exception& e) {
        // 统一错误出口：C++ 异常 -> JS Error（消息含库内 GLFW 错误码）
        Napi::Error::New(env, std::string("wbwopenglapi: ") + e.what())
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
}

} // namespace

NAPI_MODULE_INIT() {
    // 注意：宏展开的函数已含参数 env/exports，局部变量不可重名
    Napi::Object result = Napi::Object::New(env);
    result.Set("ping", Napi::Function::New(env, Ping));
    result.Set("renderTest", Napi::Function::New(env, RenderTest));
    return result;
}
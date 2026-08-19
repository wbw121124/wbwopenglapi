// addon.cc - wbwopenglapi Node-API 插件入口
//   导出（经 lib/index.js 包装后对外）：
//     createCanvas(w, h) -> CanvasHandle  完整 Canvas 2D API（隐藏窗口无头渲染）
//     loadBMP(path)      -> ImageHandle   RGBA 位图（drawImage 用）
//   注意：
//   - Canvas 绘制全部经离屏 FBO（off 模式 1x），读回默认 framebuffer 前须
//     内部 resolve()（= present()），与示例 12 验证协议一致
//   - glReadPixels 的 y 原点在左下：BW 层 readPixels 用 canvas 坐标（y 向下），
//     内部翻转 fh-1-y（同示例 12 readPx）
#include <mutex> // 先于 napi.h：MinGW libstdc++ 下 node-addon-api 需要
#include <napi.h>

#include "renderer_wrap.h"

NAPI_MODULE_INIT() {
    // 注意：宏展开的函数已含参数 env/exports，局部变量不可重名
    Napi::Object result = Napi::Object::New(env);
    wbw_napi::RendererWrap::Init(env, result);
    wbw_napi::ImageWrap::Init(env, result);
    return result;
}
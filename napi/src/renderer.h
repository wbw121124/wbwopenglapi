// renderer.h - Renderer：JS 生命周期 ↔ GL 资源生命周期的桥
//
// 资源释放策略（两层保险）：
//   1. 显式 close()（推荐）：主线程调用，先恢复 GL 上下文再析构全部 GL 资源
//   2. GC 兜底（Finalize）：Node 的 finalizer 由 V8 weak callback 触发（主线程），
//      此处不直接析构 GL 资源，而是把 shared_ptr 移入全局 pending 队列，
//      在下一次任意导出函数入口 flush——保证析构发生在主线程且有可用 GL 上下文，
//      规避「finalizer 在无 current context 线程上执行 glDelete* 崩溃」的风险。
#ifndef WBW_NAPI_RENDERER_H
#define WBW_NAPI_RENDERER_H

#include <wbwopenglapi.hpp>

#include <memory>
#include <vector>

namespace wbw_napi {

// 单线程同步渲染：Window(隐藏) + Canvas + GLFW 生命周期
struct Renderer {
    std::shared_ptr<wbwopenglapi::detail::GlfwLife> glfw;
    wbwopenglapi::Window win;
    wbwopenglapi::Canvas ctx;
    bool closed = false;

    Renderer(int w, int h)
        : glfw(wbwopenglapi::detail::glfwLife()),
          win(w, h, "wbwopenglapi", false, false), // visible=false：无头渲染
          ctx(win) {}
    // 析构顺序：ctx -> win -> glfw（声明序逆序），Canvas::~Canvas 的
    // detachCanvas 先于 Window 析构执行，GL 资源在 win 销毁前释放
};

// 全局待清理队列（仅在 Node 主线程访问：finalizer 为 V8 weak callback）
inline std::vector<std::shared_ptr<Renderer>>& pendingCleanup() {
    static std::vector<std::shared_ptr<Renderer>> queue;
    return queue;
}

// 恢复上下文后安全析构（必须在主线程、且 GL 初始化完成后调用）
inline void flushPendingCleanup() {
    std::vector<std::shared_ptr<Renderer>> pending;
    pending.swap(pendingCleanup());
    for (auto& r : pending) {
        if (r->win.nativeHandle() != nullptr) {
            glfwMakeContextCurrent(r->win.nativeHandle());
        }
        r.reset(); // ctx/win/glfw 依次析构
    }
}

} // namespace wbw_napi

#endif // WBW_NAPI_RENDERER_H
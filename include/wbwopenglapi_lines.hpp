#pragma once
//
// wbwopenglapi_lines.hpp - 像素级直线光栅化算法（DDA / Bresenham / Xiaolin Wu）
// 与 wbwopenglapi.hpp 配套；本文件纯算法、无 GL/窗口依赖，可独立复用。
// 输出统一为"像素坐标 + 覆盖度"列表，由调用方（Canvas::strokePixels）生成
// 1x1 四边形并走现有 solid 管线绘制（支持 globalAlpha/变换/抗锯齿叠加）。
//
// 坐标系约定：与画布一致（x 向右、y 向下，单位像素），坐标可为小数。
//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace wbwopenglapi {

namespace detail {

// 线条光栅化算法模式（"default" 走矢量三角带，其余走像素级算法）
enum class LineAlgo { Default, Dda, Bresenham, Wu };

// 单个覆盖像素：逻辑像素坐标 + 覆盖率 0..1（1 = 完全不透明）
struct PixelRun {
    int x;
    int y;
    float alpha;
};

// DDA（Digital Differential Analyzer）：主方向按步数线性插值，逐像素取整。
// 无抗锯齿（二值 alpha），阶梯感明显；实现最简单。
inline void rasterDDA(double x0, double y0, double x1, double y1,
                      std::vector<PixelRun>& out) {
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double adx = std::abs(dx);
    const double ady = std::abs(dy);
    int steps = static_cast<int>(std::ceil(std::max(adx, ady)));
    if (steps < 1) {
        steps = 1;
    }
    const double sx = dx / steps;
    const double sy = dy / steps;
    double x = x0;
    double y = y0;
    for (int i = 0; i <= steps; ++i) {
        out.push_back({static_cast<int>(std::floor(x + 0.5)),
                       static_cast<int>(std::floor(y + 0.5)), 1.0f});
        x += sx;
        y += sy;
    }
}

// Bresenham：整数误差累积，无浮点运算（除端点取整）。全象限无抗锯齿。
inline void rasterBresenham(int x0, int y0, int x1, int y1,
                            std::vector<PixelRun>& out) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        out.push_back({x0, y0, 1.0f});
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Xiaolin Wu 抗锯齿直线：主方向逐像素，副方向以理想线与像素中心的
// 距离把覆盖度分配到相邻两像素（alpha 渐变），视觉平滑。
// 水平/垂直/单点退化时退化为二值填充。
inline void rasterWu(double x0, double y0, double x1, double y1,
                     std::vector<PixelRun>& out) {
    const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) {
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    if (x0 > x1) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    if (dx < 1e-9) {
        // 垂直（steep 交换后仍可能）或单点：二值填充
        const int ix = static_cast<int>(std::floor(x0 + 0.5));
        const int a = static_cast<int>(std::floor(std::min(y0, y1)));
        const int b = static_cast<int>(std::floor(std::max(y0, y1)));
        for (int y = a; y <= b; ++y) {
            out.push_back({steep ? y : ix, steep ? ix : y, 1.0f});
        }
        return;
    }
    const double grad = dy / dx;
    auto plot = [&](double x, double y, double c) {
        if (c <= 0.0 || c >= 1.0) {
            c = std::min(std::max(c, 0.0), 1.0);
        }
        if (c <= 0.0) {
            return;
        }
        const int xi = static_cast<int>(std::floor(x));
        const int yi = static_cast<int>(std::floor(y));
        if (steep) {
            out.push_back({yi, xi, static_cast<float>(c)});
        } else {
            out.push_back({xi, yi, static_cast<float>(c)});
        }
    };

    // 起点端点（x 取整对齐 + gap 修正）
    const double xend0 = std::round(x0);
    const double yend0 = y0 + grad * (xend0 - x0);
    const double xgap0 = 1.0 - std::fmod(x0 + 0.5, 1.0);
    const int xpxl0 = static_cast<int>(xend0);
    const double fracY0 = yend0 - std::floor(yend0);
    plot(xpxl0, yend0, (1.0 - fracY0) * xgap0);
    plot(xpxl0, yend0 + 1.0, fracY0 * xgap0);

    // 终点端点
    const double xend1 = std::round(x1);
    const double yend1 = y1 + grad * (xend1 - x1);
    const double xgap1 = 1.0 - std::fmod(x1 + 0.5, 1.0);
    const int xpxl1 = static_cast<int>(xend1);
    const double fracY1 = yend1 - std::floor(yend1);
    plot(xpxl1, yend1, (1.0 - fracY1) * xgap1);
    plot(xpxl1, yend1 + 1.0, fracY1 * xgap1);

    // 主循环（两端点之间的整数 x）
    double intery = yend0 + grad;
    const int xLast = xpxl1 - 1;
    for (int x = xpxl0 + 1; x <= xLast; ++x) {
        const double frac = intery - std::floor(intery);
        plot(x, intery, 1.0 - frac);
        plot(x, intery + 1.0, frac);
        intery += grad;
    }
}

} // namespace detail

} // namespace wbwopenglapi

// 21_transform3d - 三维变换验证（步 2/8：rotateZ 等价性 / rotateX 梯形解析 /
// painter's order / 近平面丢弃 / 3D 描边 / resetTransform 回落）
#include <wbwopenglapi.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

// 测试模式下的像素校验（Canvas 坐标 y 向下，GL 读坐标 y 向上）
static bool checkPixel(const char* name, int x, int y, int er, int eg, int eb,
                       int tol) {
    int fh = 0;
    glfwGetFramebufferSize(glfwGetCurrentContext(), nullptr, &fh);
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(x, fh - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    bool ok = std::abs(static_cast<int>(px[0]) - er) <= tol &&
              std::abs(static_cast<int>(px[1]) - eg) <= tol &&
              std::abs(static_cast<int>(px[2]) - eb) <= tol;
    std::printf("  %-30s (%3d,%3d,%3d) %s\n", name, px[0], px[1], px[2],
                ok ? "OK" : "FAIL");
    return ok;
}

struct P2 {
    double x, y;
};

// 独立复算：perspective(d) -> translate3d(cx,cy,0) -> rotateX(t) 投影到画布像素。
// 与库实现相同的数学（双精度），用于解析定位校验点。
static P2 projRX(double x, double y, double d, double cx, double cy, double t,
                 double fw, double fh) {
    const double c = std::cos(t);
    const double s = std::sin(t);
    const double y1 = c * y;
    const double z1 = s * y;
    const double x2 = x + cx;
    const double y2 = y1 + cy;
    const double w = 1.0 - z1 / d;
    const double nx = (2.0 * x2 / fw - 1.0) / w;
    const double ny = (1.0 - 2.0 * y2 / fh) / w;
    return {(nx + 1.0) * 0.5 * fw, (1.0 - ny) * 0.5 * fh};
}

int main(int argc, char** argv) {
    bool testMode = (argc > 1 && std::strcmp(argv[1], "-t") == 0);
    try {
        wbwopenglapi::Window win(800, 600, "wbwopenglapi 21_transform3d", true);
        wbwopenglapi::Canvas ctx(win);

        double t0 = glfwGetTime();
        bool verified = false;
        while (!win.shouldClose()) {
            ctx.clear("#f0f0f0");

            // A. translate3d(z=0) 与 2D translate 等价性
            ctx.save();
            ctx.translate(80, 80);
            ctx.fillStyle("#e74c3c");
            ctx.fillRect(0, 0, 60, 40);
            ctx.restore();
            ctx.save();
            ctx.translate3d(200, 80, 0);
            ctx.fillStyle("#3498db");
            ctx.fillRect(0, 0, 60, 40);
            ctx.restore();

            // B. rotateZ 与 2D rotate 等价性（45° 正方形 -> 菱形）
            ctx.save();
            ctx.translate(240, 300);
            ctx.rotateZ(0.7853981633974483);
            ctx.fillStyle("#2ecc71");
            ctx.fillRect(-40, -40, 80, 80);
            ctx.restore();
            ctx.save();
            ctx.translate(420, 300);
            ctx.rotate(0.7853981633974483);
            ctx.fillStyle("#9b59b6");
            ctx.fillRect(-40, -40, 80, 80);
            ctx.restore();

            // C. rotateX 梯形（透视投影，校验点由 projRX 解析计算）
            ctx.save();
            ctx.perspective(400);
            ctx.translate3d(600, 180, 0);
            ctx.rotateX(0.4);
            ctx.fillStyle("#e67e22");
            ctx.fillRect(-80, -50, 160, 100);
            ctx.restore();

            // D. painter's order：远大蓝先画、近小红后画 -> 重叠区红胜
            //    （无深度测试，绘制顺序即遮挡顺序）
            ctx.save();
            ctx.perspective(500);
            ctx.translate3d(150, 450, 0);
            ctx.translate3d(0, 0, -80);
            ctx.fillStyle("#3498db");
            ctx.fillRect(-100, -75, 200, 150);
            ctx.restore();
            ctx.save();
            ctx.perspective(500);
            ctx.translate3d(150, 450, 0);
            ctx.translate3d(0, 0, 80);
            ctx.fillStyle("#e74c3c");
            ctx.fillRect(-50, -37, 100, 75);
            ctx.restore();

            // E. 整体越过相机平面（z>d）-> 全部顶点 w<0 被裁剪，不绘制
            ctx.save();
            ctx.perspective(200);
            ctx.translate3d(520, 420, 260);
            ctx.fillStyle("#8e44ad");
            ctx.fillRect(-40, -30, 80, 60);
            ctx.restore();

            // F. 3D 描边（strokeRect 三角带经投影，宽度随深度变化）
            ctx.save();
            ctx.perspective(400);
            ctx.translate3d(430, 480, 0);
            ctx.rotateX(0.5);
            ctx.strokeStyle("#2c3e50");
            ctx.lineWidth(4);
            ctx.strokeRect(-70, -40, 140, 80);
            ctx.restore();

            // perspective(d<=0) 必须抛异常且不破坏状态
            bool threw = false;
            try {
                ctx.perspective(-5);
            } catch (const std::exception&) {
                threw = true;
            }

            // G. resetTransform 退出 3D 模式回到 2D mat3 快速通道
            ctx.resetTransform();
            ctx.fillStyle("#27ae60");
            ctx.fillRect(700, 520, 60, 40);

            if (testMode && !verified && glfwGetTime() - t0 > 0.5) {
                bool all = true;
                all &= checkPixel("A translate 红矩形", 110, 100, 231, 76, 60, 12);
                all &= checkPixel("A translate3d 蓝矩形", 230, 100, 52, 152, 219, 12);
                all &= checkPixel("B rotateZ 菱形内", 240, 346, 46, 204, 113, 12);
                all &= checkPixel("B rotate 菱形内", 420, 346, 155, 89, 182, 12);
                all &= checkPixel("B 下尖外背景", 240, 370, 240, 240, 240, 8);
                all &= checkPixel("B 对角外背景", 270, 270, 240, 240, 240, 8);

                // C 梯形：质心 + 四边内外采样（projRX 独立复算定位）
                const P2 tl = projRX(-80, -50, 400, 600, 180, 0.4, 800, 600);
                const P2 tr = projRX(80, -50, 400, 600, 180, 0.4, 800, 600);
                const P2 br = projRX(80, 50, 400, 600, 180, 0.4, 800, 600);
                const P2 bl = projRX(-80, 50, 400, 600, 180, 0.4, 800, 600);
                const P2 cen{(tl.x + tr.x + br.x + bl.x) / 4,
                             (tl.y + tr.y + br.y + bl.y) / 4};
                const P2 edge[4][2] = {{tl, tr}, {tr, br}, {br, bl}, {bl, tl}};
                const char* ename[4] = {"C 上边", "C 右边", "C 下边", "C 左边"};
                all &= checkPixel("C 质心", (int)cen.x, (int)cen.y, 230, 126, 34, 16);
                for (int i = 0; i < 4; ++i) {
                    const double mx = (edge[i][0].x + edge[i][1].x) / 2;
                    const double my = (edge[i][0].y + edge[i][1].y) / 2;
                    double nx = edge[i][1].y - edge[i][0].y;
                    double ny = -(edge[i][1].x - edge[i][0].x);
                    const double len = std::sqrt(nx * nx + ny * ny);
                    nx /= len;
                    ny /= len;
                    if (nx * (cen.x - mx) + ny * (cen.y - my) > 0) {
                        nx = -nx; // 法线朝外
                        ny = -ny;
                    }
                    char label[64];
                    std::snprintf(label, sizeof(label), "%s 内侧", ename[i]);
                    all &= checkPixel(label, (int)(mx - nx * 3), (int)(my - ny * 3),
                                      230, 126, 34, 18);
                    std::snprintf(label, sizeof(label), "%s 外侧", ename[i]);
                    all &= checkPixel(label, (int)(mx + nx * 3), (int)(my + ny * 3),
                                      240, 240, 240, 8);
                }

                all &= checkPixel("D 重叠中心红胜", 150, 450, 231, 76, 60, 12);
                all &= checkPixel("D 仅蓝区域", 80, 450, 52, 152, 219, 12);
                all &= checkPixel("E 相机后丢弃", 520, 420, 240, 240, 240, 8);

                // F 上边中点（projRX 复算定位；描边条带中心）
                const P2 fmid = projRX(0, -40, 400, 430, 480, 0.5, 800, 600);
                all &= checkPixel("F 3D 描边上边", (int)fmid.x, (int)fmid.y,
                                  44, 62, 80, 24);

                all &= checkPixel("G reset 后 2D 矩形", 730, 540, 39, 174, 96, 12);
                if (!threw) {
                    std::printf("  perspective(-5) 未抛异常           FAIL\n");
                    all = false;
                } else {
                    std::printf("  perspective(-5) 抛异常             OK\n");
                }
                verified = true;
                win.close();
                return all ? 0 : 2;
            }

            win.swapBuffers();
            win.pollEvents();
            if (win.keyPressed(GLFW_KEY_ESCAPE)) {
                win.close();
            }
            if (testMode && glfwGetTime() - t0 > 3.0) {
                win.close();
            }
        }
        std::printf("21_transform3d 正常退出\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
}

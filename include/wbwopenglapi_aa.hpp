#pragma once
//
// wbwopenglapi_aa.hpp - 抗锯齿渲染管线支撑
// 与 wbwopenglapi.hpp 配套：
//   1) detail::FrameBuffer —— 离屏 FBO RAII（RGBA8 + depth24stencil8，
//      samples>0 时为多重采样 renderbuffer，供 SSAA/MSAA 使用；
//      samples==0 时颜色为可采样纹理，供 FXAA/MLAA 后处理使用）
//   2) FXAA / MLAA（简化版）片元着色器源码
// 本文件依赖 GL 3.3+（GLAD），不包含窗口/Canvas 逻辑。
//

namespace wbwopenglapi {

namespace detail {

// =====================================================================
// 离屏 FBO（RGBA8 颜色 + depth24stencil8；文本 fill 的 stencil even-odd
// 两遍法依赖 stencil，后处理模式下也必须保留）
// =====================================================================
class FrameBuffer {
public:
    FrameBuffer() = default;
    FrameBuffer(int w, int h, int samples) { create(w, h, samples); }
    ~FrameBuffer() { destroy(); }
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    // 创建（重复调用自动重建）；失败抛出异常（已完成性校验）
    void create(int w, int h, int samples) {
        destroy();
        w_ = w;
        h_ = h;
        samples_ = samples;
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        if (samples > 0) {
            // 多重采样：颜色 + 深度模板均为 multisample renderbuffer
            glGenRenderbuffers(2, rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, rb_[0]);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_RENDERBUFFER, rb_[0]);
            glBindRenderbuffer(GL_RENDERBUFFER, rb_[1]);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                             GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, rb_[1]);
        } else {
            // 非多重采样：颜色为可采样纹理（FXAA/MLAA 后处理输入）
            glGenTextures(1, &color_);
            glBindTexture(GL_TEXTURE_2D, color_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, color_, 0);
            glGenRenderbuffers(1, &ds_);
            glBindRenderbuffer(GL_RENDERBUFFER, ds_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_RENDERBUFFER, ds_);
        }
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            destroy();
            throw std::runtime_error(
                "wbwopenglapi: 离屏帧缓冲不完整 (FBO 状态 " +
                std::to_string(static_cast<int>(status)) + ")");
        }
    }

    void destroy() {
        if (rb_[0] || rb_[1]) {
            glDeleteRenderbuffers(2, rb_);
            rb_[0] = rb_[1] = 0;
        }
        if (ds_) {
            glDeleteRenderbuffers(1, &ds_);
            ds_ = 0;
        }
        if (color_) {
            glDeleteTextures(1, &color_);
            color_ = 0;
        }
        if (fbo_) {
            glDeleteFramebuffers(1, &fbo_);
            fbo_ = 0;
        }
    }

    void bindDraw() const { glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_); }
    void bindRead() const { glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_); }
    void bindBoth() const { glBindFramebuffer(GL_FRAMEBUFFER, fbo_); }

    GLuint colorTexture() const { return color_; } // samples==0 时有效
    int width() const { return w_; }
    int height() const { return h_; }
    int samples() const { return samples_; }

private:
    GLuint fbo_ = 0;
    GLuint color_ = 0; // 颜色纹理（samples==0）或 renderbuffer（samples>0）
    GLuint rb_[2] = {0, 0}; // samples>0: [0]=颜色 [1]=深度模板
    GLuint ds_ = 0;         // samples==0: 深度模板 renderbuffer
    int w_ = 0;
    int h_ = 0;
    int samples_ = 0;
};

// =====================================================================
// FXAA 片元着色器（简化 FXAA 3.11：亮度边缘检测 + 方向估计 +
// 边缘方向 ±8 像素内两点采样混合，min/max 截断防过度模糊）
// =====================================================================
inline constexpr const char* kFxaaFS = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
uniform vec2 u_texel; // 1.0 / (宽, 高)

float rgb2luma(vec3 c) { return sqrt(dot(c, vec3(0.299, 0.587, 0.114))); }

void main() {
    vec3 color = texture(u_tex, v_uv).rgb;
    float lumaC = rgb2luma(color);
    float lumaNW = rgb2luma(texture(u_tex, v_uv + vec2(-1.0, -1.0) * u_texel).rgb);
    float lumaNE = rgb2luma(texture(u_tex, v_uv + vec2( 1.0, -1.0) * u_texel).rgb);
    float lumaSW = rgb2luma(texture(u_tex, v_uv + vec2(-1.0,  1.0) * u_texel).rgb);
    float lumaSE = rgb2luma(texture(u_tex, v_uv + vec2( 1.0,  1.0) * u_texel).rgb);
    float lumaN  = rgb2luma(texture(u_tex, v_uv + vec2( 0.0, -1.0) * u_texel).rgb);
    float lumaS  = rgb2luma(texture(u_tex, v_uv + vec2( 0.0,  1.0) * u_texel).rgb);
    float lumaW  = rgb2luma(texture(u_tex, v_uv + vec2(-1.0,  0.0) * u_texel).rgb);
    float lumaE  = rgb2luma(texture(u_tex, v_uv + vec2( 1.0,  0.0) * u_texel).rgb);

    float lumaMin = min(lumaC, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    lumaMin = min(lumaMin, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaC, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    lumaMax = max(lumaMax, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float lumaRange = lumaMax - lumaMin;

    // 非边缘（范围低于阈值）直接输出
    if (lumaRange < max(0.0312, lumaMax * 0.0625)) {
        frag = vec4(color, 1.0);
        return;
    }

    // 边缘方向估计（FXAA 3.11 梯度法）：dir 指向亮度变化最剧烈方向
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * 0.0833,
                          1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(vec2(8.0, 8.0), max(vec2(-8.0, -8.0), dir * rcpDirMin)) * u_texel;

    // 沿方向三分之二步长采样两组取平均（中心附近的近似）
    vec3 rgbA = 0.5 * (texture(u_tex, v_uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(u_tex, v_uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(u_tex, v_uv + dir * -0.5).rgb +
                                     texture(u_tex, v_uv + dir * 0.5).rgb);
    float lumaB = rgb2luma(rgbB);
    // 若混合结果超出局部亮度范围（过模糊），退回更保守的混合
    if (lumaB < lumaMin || lumaB > lumaMax) {
        rgbA = rgbB;
    }
    frag = vec4(rgbA, 1.0);
}
)GLSL";

// =====================================================================
// MLAA（简化版，两 pass）：
//   pass1 边缘图：输出亮度梯度 gx/gy + 梯度模长 mag
//   pass2 平滑：沿边缘方向（垂直于梯度）取 ±1/±2 像素，按与中心
//   亮度接近度加权混合；混合强度随边缘强度缩放
// =====================================================================
inline constexpr const char* kMlaaEdgeFS = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
uniform vec2 u_texel;

float rgb2luma(vec3 c) { return sqrt(dot(c, vec3(0.299, 0.587, 0.114))); }

void main() {
    float l  = rgb2luma(texture(u_tex, v_uv + vec2(-1.0,  0.0) * u_texel).rgb);
    float r  = rgb2luma(texture(u_tex, v_uv + vec2( 1.0,  0.0) * u_texel).rgb);
    float u  = rgb2luma(texture(u_tex, v_uv + vec2( 0.0, -1.0) * u_texel).rgb);
    float d  = rgb2luma(texture(u_tex, v_uv + vec2( 0.0,  1.0) * u_texel).rgb);
    float gx = (r - l) * 0.5;
    float gy = (d - u) * 0.5;
    float mag = sqrt(gx * gx + gy * gy);
    frag = vec4(gx, gy, mag, 1.0);
}
)GLSL";

inline constexpr const char* kMlaaSmoothFS = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_color;
uniform sampler2D u_edge;
uniform vec2 u_texel;

float rgb2luma(vec3 c) { return sqrt(dot(c, vec3(0.299, 0.587, 0.114))); }

void main() {
    vec2 uv = v_uv;
    vec4 e = texture(u_edge, uv);
    float gx = e.r;
    float gy = e.g;
    float mag = e.b;
    vec3 color = texture(u_color, uv).rgb;
    if (mag < 0.0312) {
        frag = vec4(color, 1.0);
        return;
    }
    // 梯度方向 = 边缘法线。沿法线两侧各取 1 像素，向亮度相差更远
    // （跨边）的邻居按边缘强度混合——硬边两侧像素摊薄出中间色。
    vec2 n = vec2(gx, gy);
    if (length(n) < 1e-5) {
        frag = vec4(color, 1.0);
        return;
    }
    n = normalize(n);
    vec3 c0 = texture(u_color, uv + n * u_texel).rgb;
    vec3 c1 = texture(u_color, uv - n * u_texel).rgb;
    float l = rgb2luma(color);
    float l0 = rgb2luma(c0);
    float l1 = rgb2luma(c1);
    vec3 nb = (abs(l0 - l) > abs(l1 - l)) ? c0 : c1;
    float a = clamp(mag * 4.0, 0.0, 0.5);
    frag = vec4(mix(color, nb, a), 1.0);
}
)GLSL";

} // namespace detail

} // namespace wbwopenglapi

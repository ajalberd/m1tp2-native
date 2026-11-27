#include "glide_gl.h"
#include "video_sdl.h"
#include "heap_guard.h"
#include "gl_loader.h"
#include "common.h"
#include "patch.h"
#include <windows.h>
#include <SDL3/SDL_opengl.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace m1 {

namespace {

using FxU8    = uint8_t;
using FxU16   = uint16_t;
using FxU32   = uint32_t;
using FxI32   = int32_t;
using FxBool  = int32_t;
using GrColor = uint32_t;

struct GrTmuVertex {
    float sow;
    float tow;
    float oow;
};

struct GrVertex {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    GrTmuVertex tmuvtx[2];
};

struct GrTexInfo {
    int32_t smallLod;
    int32_t largeLod;
    int32_t aspectRatio;
    int32_t format;
    void*   data;
};

struct GrLfbInfo {
    int32_t  size;
    void*    lfbPtr;
    uint32_t strideInBytes;
    int32_t  writeMode;
    int32_t  origin;
};

// Exact Glide 2.43 enums
constexpr int32_t GR_COMBINE_FUNCTION_ZERO                  = 0;
constexpr int32_t GR_COMBINE_FUNCTION_LOCAL                 = 1;
constexpr int32_t GR_COMBINE_FUNCTION_LOCAL_ALPHA           = 2;
constexpr int32_t GR_COMBINE_FUNCTION_SCALE_OTHER           = 3;
constexpr int32_t GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL = 4;

constexpr int32_t GR_COMBINE_FACTOR_ZERO                    = 0;
constexpr int32_t GR_COMBINE_FACTOR_LOCAL                   = 1;
constexpr int32_t GR_COMBINE_FACTOR_OTHER_ALPHA             = 2;
constexpr int32_t GR_COMBINE_FACTOR_LOCAL_ALPHA             = 3;
constexpr int32_t GR_COMBINE_FACTOR_TEXTURE_ALPHA           = 4;
constexpr int32_t GR_COMBINE_FACTOR_ONE                     = 8;

constexpr int32_t GR_COMBINE_LOCAL_ITERATED                 = 0;
constexpr int32_t GR_COMBINE_LOCAL_CONSTANT                 = 1;

constexpr int32_t GR_COMBINE_OTHER_ITERATED                 = 0;
constexpr int32_t GR_COMBINE_OTHER_TEXTURE                  = 1;
constexpr int32_t GR_COMBINE_OTHER_CONSTANT                 = 2;

constexpr int32_t GR_FOG_DISABLE                            = 0;
constexpr int32_t GR_LFB_WRITE_ONLY                         = 1;
constexpr int32_t GR_LFBWRITEMODE_565                       = 0;

// ---- OpenGL 3.3 Native Glide Resources ----
GLuint g_glideProg = 0;
GLuint g_glideVao  = 0;
GLuint g_glideVbo  = 0;
GLuint g_lfbProg   = 0;
GLuint g_lfbTex    = 0;

GLuint g_msaaFbo = 0;
GLuint g_msaaRbo = 0;
int    g_msaaW   = 0;
int    g_msaaH   = 0;
int    g_msaaCur = 0;
bool   g_fboDirty = true;

GLint g_uMode        = -1; // 0=const, 1=iterated, 2=tex*iterated, 3=tex, 4=tex*const, 5=black
GLint g_uConstColor  = -1;
GLint g_uChromakeyEn = -1;
GLint g_uBlendEn     = -1;
GLint g_uFogEn       = -1;
GLint g_uFogColor    = -1;
GLint g_uFogNear     = -1;
GLint g_uFogFar      = -1;
GLint g_uGamma       = -1;
GLint g_uTex         = -1;
GLint g_uLfbTex      = -1;

// Exact Glide pipeline state
int32_t  g_ccFunc   = GR_COMBINE_FUNCTION_LOCAL;
int32_t  g_ccFactor = GR_COMBINE_FACTOR_ZERO;
int32_t  g_ccLocal  = GR_COMBINE_LOCAL_CONSTANT;
int32_t  g_ccOther  = GR_COMBINE_OTHER_CONSTANT;
int32_t  g_ccInvert = 0;

uint32_t g_constColorARGB = 0xFFFFFFFFu;
bool     g_chromakeyEn = false;
uint32_t g_chromakeyARGB = 0;
bool     g_fogEn = false;
uint32_t g_fogColorARGB = 0;
float    g_fogNear = 32.0f;
float    g_fogFar  = 98304.0f;
float    g_gameGamma = 1.0f;
bool     g_gameTexLinear = true;
bool     g_texClampS = false; // 0 = WRAP (GL_REPEAT) by default in M1TP2!
bool     g_texClampT = false;
bool     g_blendEn   = false;
uint32_t g_curTexAddr = 0xFFFFFFFFu;
float    g_curTexMaxS = 256.0f;
float    g_curTexMaxT = 256.0f;

uint32_t g_palRGBA[256] = {};

struct TexSlot {
    GLuint glTex = 0;
    int    width = 0;
    int    height = 0;
    float  maxS = 256.0f;
    float  maxT = 256.0f;
    std::vector<uint8_t> rawIdx;
};
std::unordered_map<uint32_t, TexSlot> g_texCache;
std::vector<uint32_t> g_texExpandScratch;

constexpr int LFB_W = 640;
constexpr int LFB_H = 480;
uint16_t g_lfb565[LFB_W * LFB_H] = {};
uint32_t g_lfbRGBA[LFB_W * LFB_H] = {};
uint16_t g_lfbSentinel = 0x0001;
bool     g_lfbWrittenThisFrame = false;
bool     g_lfbHasPixels = false;
bool     g_lfbLockWritePending = false;
bool     g_glideOpen = false;
bool     g_glInited  = false;

int g_frameDrawCalls = 0;
int g_frameTris      = 0;

struct GLVert {
    float x, y;
    float r, g, b, a;
    float u, v;
    float w;
};
std::vector<GLVert> g_batchVerts;

static const char* kGlideVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aW;

out vec4  vCol;
out vec2  vUV;
out float vW;

void main() {
    vCol = aCol;
    vUV  = aUV;
    vW   = aW;
    float w = clamp(aW, 0.001, 100000.0);
    gl_Position = vec4(aPos * w, 0.0, w);
}
)GLSL";

static const char* kGlideFS = R"GLSL(
#version 330 core
in vec4  vCol;
in vec2  vUV;
in float vW;

uniform int       uMode;
uniform vec4      uConstColor;
uniform int       uChromakeyEn;
uniform int       uBlendEn;
uniform int       uFogEn;
uniform vec3      uFogColor;
uniform float     uFogNear;
uniform float     uFogFar;
uniform float     uGamma;
uniform sampler2D uTex;

out vec4 FragColor;

void main() {
    vec3 rgb;
    float alpha = (uBlendEn != 0) ? vCol.a : 1.0;

    if (uMode == 0) {
        rgb = uConstColor.rgb;
    } else if (uMode == 1) {
        rgb = vCol.rgb;
    } else if (uMode == 5) {
        rgb = vec3(0.0);
    } else {
        vec4 texel = texture(uTex, vUV);
        if (uChromakeyEn != 0 && texel.a < 0.5) {
            discard;
        }
        if (uMode == 2) {
            rgb = texel.rgb * vCol.rgb;
        } else if (uMode == 4) {
            rgb = texel.rgb * uConstColor.rgb;
        } else {
            // uMode == 3: GR_COMBINE_FUNCTION_SCALE_OTHER with GR_COMBINE_FACTOR_ONE (pure texture!)
            rgb = texel.rgb;
        }
    }

    if (uFogEn != 0 && uFogFar > uFogNear) {
        float f = clamp((vW - uFogNear) / (uFogFar - uFogNear), 0.0, 1.0);
        rgb = mix(rgb, uFogColor, f);
    }

    if (uGamma > 0.05 && abs(uGamma - 1.0) > 0.01) {
        rgb = pow(max(rgb, vec3(0.0)), vec3(1.0 / uGamma));
    }

    FragColor = vec4(rgb, alpha);
}
)GLSL";

static const char* kLfbVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* kLfbFS = R"GLSL(
#version 330 core
in vec2 vUV;
uniform sampler2D uLfbTex;
out vec4 FragColor;
void main() {
    vec4 c = texture(uLfbTex, vUV);
    if (c.a < 0.5) discard;
    FragColor = c;
}
)GLSL";

static void UploadExpandedTexture(TexSlot& slot) {
    if (!slot.glTex || slot.rawIdx.empty() || slot.width <= 0 || slot.height <= 0) return;
    int w = slot.width;
    int h = slot.height;
    size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (g_texExpandScratch.size() < n) g_texExpandScratch.resize(n);

    for (size_t i = 0; i < n; ++i) {
        uint8_t idx = slot.rawIdx[i];
        uint32_t c = g_palRGBA[idx];
        g_texExpandScratch[i] = (idx == 0) ? (c & 0x00FFFFFFu) : (c | 0xFF000000u);
    }
    // Bleed neighbor RGB into transparent (index 0) texels so mipmaps & AF never create dark fringes
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x;
            if (slot.rawIdx[i] != 0) continue;
            int sr = 0, sg = 0, sb = 0, cnt = 0;
            const int dx[4] = {-1, 1, 0, 0};
            const int dy[4] = { 0, 0,-1, 1};
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d], ny = y + dy[d];
                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    uint8_t nidx = slot.rawIdx[static_cast<size_t>(ny) * w + nx];
                    if (nidx != 0) {
                        uint32_t nc = g_palRGBA[nidx];
                        sr += (nc & 0xFF);
                        sg += ((nc >> 8) & 0xFF);
                        sb += ((nc >> 16) & 0xFF);
                        cnt++;
                    }
                }
            }
            if (cnt > 0) {
                g_texExpandScratch[i] = (uint32_t(sb / cnt) << 16) | (uint32_t(sg / cnt) << 8) | uint32_t(sr / cnt);
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, slot.glTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_texExpandScratch.data());
    gl::GenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void EnsureMsaaFbo() {
    int winW, winH;
    VideoGetWindowSize(winW, winH);
    int targetW = std::max(640, winW);
    int targetH = std::max(480, winH);
    int wantSamples = std::clamp(g_cfg.msaaSamples, 0, g_stats.maxMsaa);

    if (!g_fboDirty && g_msaaW == targetW && g_msaaH == targetH && g_msaaCur == wantSamples) {
        return;
    }
    g_fboDirty = false;

    if (g_msaaFbo) { gl::DeleteFramebuffers(1, &g_msaaFbo); g_msaaFbo = 0; }
    if (g_msaaRbo) { gl::DeleteRenderbuffers(1, &g_msaaRbo); g_msaaRbo = 0; }

    g_msaaW = targetW;
    g_msaaH = targetH;
    g_msaaCur = 0;

    if (wantSamples >= 2) {
        gl::GenFramebuffers(1, &g_msaaFbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER, g_msaaFbo);
        gl::GenRenderbuffers(1, &g_msaaRbo);
        gl::BindRenderbuffer(GL_RENDERBUFFER, g_msaaRbo);
        gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, wantSamples, GL_RGBA8, targetW, targetH);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_msaaRbo);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            g_msaaCur = wantSamples;
            LogPrintf("[GLIDE-GL] MSAA FBO ready: %dx%d @ %dx MSAA (zero-realloc dynamic viewport)\n",
                      targetW, targetH, g_msaaCur);
        } else {
            LogPrintf("[GLIDE-GL] MSAA FBO incomplete, falling back to 0x MSAA\n");
            gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl::DeleteFramebuffers(1, &g_msaaFbo); g_msaaFbo = 0;
            gl::DeleteRenderbuffers(1, &g_msaaRbo); g_msaaRbo = 0;
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    int vx, vy, vw, vh;
    VideoGetViewport(true, vx, vy, vw, vh);
    g_stats.renderW    = vw;
    g_stats.renderH    = vh;
    g_stats.activeMsaa = g_msaaCur;
}

static void EnsureGlideGLInit() {
    if (g_glInited) return;
    g_glInited = true;

    // Seed palette from M1TP2's current 0x518150 RGB palette if already populated
    const uint8_t* p768 = reinterpret_cast<const uint8_t*>(addr::g_Palette768);
    for (int i = 0; i < 256; ++i) {
        g_palRGBA[i] = 0xFF000000u |
                       (uint32_t(p768[i * 3 + 2]) << 16) |
                       (uint32_t(p768[i * 3 + 1]) << 8)  |
                        uint32_t(p768[i * 3 + 0]);
    }

    g_glideProg     = gl::BuildProgram(kGlideVS, kGlideFS, "Glide3D_RGBA8");
    g_uMode         = gl::GetUniformLocation(g_glideProg, "uMode");
    g_uConstColor   = gl::GetUniformLocation(g_glideProg, "uConstColor");
    g_uChromakeyEn  = gl::GetUniformLocation(g_glideProg, "uChromakeyEn");
    g_uBlendEn      = gl::GetUniformLocation(g_glideProg, "uBlendEn");
    g_uFogEn        = gl::GetUniformLocation(g_glideProg, "uFogEn");
    g_uFogColor     = gl::GetUniformLocation(g_glideProg, "uFogColor");
    g_uFogNear      = gl::GetUniformLocation(g_glideProg, "uFogNear");
    g_uFogFar       = gl::GetUniformLocation(g_glideProg, "uFogFar");
    g_uGamma        = gl::GetUniformLocation(g_glideProg, "uGamma");
    g_uTex          = gl::GetUniformLocation(g_glideProg, "uTex");

    gl::GenVertexArrays(1, &g_glideVao);
    gl::BindVertexArray(g_glideVao);
    gl::GenBuffers(1, &g_glideVbo);
    gl::BindBuffer(GL_ARRAY_BUFFER, g_glideVbo);
    gl::BufferData(GL_ARRAY_BUFFER, 4096 * sizeof(GLVert), nullptr, GL_STREAM_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVert), (void*)offsetof(GLVert, x));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GLVert), (void*)offsetof(GLVert, r));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GLVert), (void*)offsetof(GLVert, u));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GLVert), (void*)offsetof(GLVert, w));
    gl::BindVertexArray(0);

    g_lfbProg = gl::BuildProgram(kLfbVS, kLfbFS, "GlideLFB");
    g_uLfbTex = gl::GetUniformLocation(g_lfbProg, "uLfbTex");

    glGenTextures(1, &g_lfbTex);
    glBindTexture(GL_TEXTURE_2D, g_lfbTex);
    GLint lf = g_cfg.linearFilterLfb ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    g_batchVerts.reserve(256);
    g_fboDirty = true;
    LogPrintf("[GLIDE-GL] OpenGL 3.3 HD Glide renderer initialized (exact combiner + RGBA8 mipmaps + AF + MSAA)\n");
}

static uint16_t PickUnusedLfbSentinel() {
    const uint16_t* lut = reinterpret_cast<const uint16_t*>(addr::g_GlideLut565);
    bool used[65536] = {};
    used[0x0000] = true;
    used[0xFFFF] = true;
    for (int i = 0; i < 256; ++i) used[lut[i]] = true;
    for (uint32_t c : { 0xF81Eu, 0xF81Du, 0x0001u, 0x0821u, 0xF01Fu }) {
        if (!used[c]) return static_cast<uint16_t>(c);
    }
    for (uint32_t c = 1; c < 65535; ++c) {
        if (!used[c]) return static_cast<uint16_t>(c);
    }
    return 0x0001;
}

static void ClearLfbOverlay() {
    g_lfbSentinel = PickUnusedLfbSentinel();
    std::fill_n(g_lfb565, LFB_W * LFB_H, g_lfbSentinel);
    g_lfbWrittenThisFrame = false;
    g_lfbHasPixels = false;
    g_lfbLockWritePending = false;
}

static bool CheckLfbHasPixels(bool clearPending) {
    if (!g_lfbHasPixels && g_lfbLockWritePending) {
        const uint16_t sentinel = g_lfbSentinel;
        for (int i = 0; i < LFB_W * LFB_H; ++i) {
            if (g_lfb565[i] != sentinel) {
                g_lfbHasPixels = true;
                break;
            }
        }
    }
    if (clearPending) {
        g_lfbLockWritePending = false;
    }
    return g_lfbHasPixels;
}

static int ResolveColorCombineMode();

static uint16_t ResolveCurrentGlideColor565(const GrVertex& v) {
    uint32_t r8 = 255, g8 = 255, b8 = 255;
    int mode = ResolveColorCombineMode();
    if (mode == 1) {
        r8 = std::isfinite(v.r) ? static_cast<uint32_t>(std::clamp(std::lround(v.r), 0L, 255L)) : 255u;
        g8 = std::isfinite(v.g) ? static_cast<uint32_t>(std::clamp(std::lround(v.g), 0L, 255L)) : 255u;
        b8 = std::isfinite(v.b) ? static_cast<uint32_t>(std::clamp(std::lround(v.b), 0L, 255L)) : 255u;
    } else if (mode == 5) {
        r8 = g8 = b8 = 0u;
    } else {
        r8 = (g_constColorARGB >> 16) & 0xFFu;
        g8 = (g_constColorARGB >> 8)  & 0xFFu;
        b8 =  g_constColorARGB        & 0xFFu;
    }
    uint16_t c565 = static_cast<uint16_t>(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3));
    if (c565 == g_lfbSentinel) {
        c565 ^= 0x0001u;
    }
    return c565;
}

static inline void PutLfbPixel(int x, int y, uint16_t c565) {
    if (x >= 0 && x < LFB_W && y >= 0 && y < LFB_H) {
        g_lfb565[y * LFB_W + x] = c565;
    }
}

static void RasterizeLfbLine(float fx0, float fy0, float fx1, float fy1, uint16_t c565) {
    if (!std::isfinite(fx0) || !std::isfinite(fy0) || !std::isfinite(fx1) || !std::isfinite(fy1)) return;

    // Cohen-Sutherland clip to [0, LFB_W - 1] x [0, LFB_H - 1]
    constexpr float kMinX = 0.0f, kMaxX = float(LFB_W - 1);
    constexpr float kMinY = 0.0f, kMaxY = float(LFB_H - 1);
    auto outCode = [](float x, float y) -> int {
        int code = 0;
        if (x < kMinX)      code |= 1;
        else if (x > kMaxX) code |= 2;
        if (y < kMinY)      code |= 4;
        else if (y > kMaxY) code |= 8;
        return code;
    };

    int c0 = outCode(fx0, fy0);
    int c1 = outCode(fx1, fy1);
    while (true) {
        if ((c0 | c1) == 0) break;
        if ((c0 & c1) != 0) return;
        int cOut = c0 ? c0 : c1;
        float x = 0.0f, y = 0.0f;
        float dx = fx1 - fx0;
        float dy = fy1 - fy0;
        if (cOut & 8) {
            x = (std::fabs(dy) > 1e-6f) ? (fx0 + dx * (kMaxY - fy0) / dy) : fx0;
            y = kMaxY;
        } else if (cOut & 4) {
            x = (std::fabs(dy) > 1e-6f) ? (fx0 + dx * (kMinY - fy0) / dy) : fx0;
            y = kMinY;
        } else if (cOut & 2) {
            y = (std::fabs(dx) > 1e-6f) ? (fy0 + dy * (kMaxX - fx0) / dx) : fy0;
            x = kMaxX;
        } else {
            y = (std::fabs(dx) > 1e-6f) ? (fy0 + dy * (kMinX - fx0) / dx) : fy0;
            x = kMinX;
        }
        if (cOut == c0) {
            fx0 = x; fy0 = y; c0 = outCode(fx0, fy0);
        } else {
            fx1 = x; fy1 = y; c1 = outCode(fx1, fy1);
        }
    }

    int x0 = std::clamp(static_cast<int>(std::lround(fx0)), 0, LFB_W - 1);
    int y0 = std::clamp(static_cast<int>(std::lround(fy0)), 0, LFB_H - 1);
    int x1 = std::clamp(static_cast<int>(std::lround(fx1)), 0, LFB_W - 1);
    int y1 = std::clamp(static_cast<int>(std::lround(fy1)), 0, LFB_H - 1);

    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        PutLfbPixel(x0, y0, c565);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void UnpackARGB(uint32_t c, float& r, float& g, float& b, float& a) {
    a = ((c >> 24) & 0xFF) / 255.0f;
    r = ((c >> 16) & 0xFF) / 255.0f;
    g = ((c >> 8)  & 0xFF) / 255.0f;
    b = ( c        & 0xFF) / 255.0f;
}

static int ResolveColorCombineMode() {
    if (g_ccFunc == GR_COMBINE_FUNCTION_ZERO) {
        return 5; // black
    }
    if (g_ccFunc == GR_COMBINE_FUNCTION_LOCAL || g_ccFunc == GR_COMBINE_FUNCTION_LOCAL_ALPHA) {
        return (g_ccLocal == GR_COMBINE_LOCAL_CONSTANT) ? 0 : 1;
    }
    if (g_ccFunc == GR_COMBINE_FUNCTION_SCALE_OTHER ||
        g_ccFunc == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL) {
        if (g_ccFactor == GR_COMBINE_FACTOR_ZERO) {
            return (g_ccFunc == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL)
                       ? ((g_ccLocal == GR_COMBINE_LOCAL_CONSTANT) ? 0 : 1)
                       : 5;
        }
        if (g_ccOther == GR_COMBINE_OTHER_TEXTURE) {
            // CRITICAL: M1TP2 uses GR_COMBINE_FACTOR_ONE (8) for unlit/fullbright textured polygons!
            if (g_ccFactor == GR_COMBINE_FACTOR_ONE) {
                return 3; // Pure texture RGB!
            }
            if (g_ccFactor == GR_COMBINE_FACTOR_LOCAL) {
                return (g_ccLocal == GR_COMBINE_LOCAL_CONSTANT) ? 4 : 2;
            }
            return 3;
        }
        if (g_ccOther == GR_COMBINE_OTHER_CONSTANT) return 0;
        if (g_ccOther == GR_COMBINE_OTHER_ITERATED) return 1;
    }
    return 3;
}

static void BindGlideTargetAndState() {
    EnsureMsaaFbo();

    int vx, vy, vw, vh;
    VideoGetViewport(true, vx, vy, vw, vh);
    if (g_msaaFbo && g_msaaCur >= 2) {
        int bw = std::min(vw, g_msaaW);
        int bh = std::min(vh, g_msaaH);
        gl::BindFramebuffer(GL_FRAMEBUFFER, g_msaaFbo);
        glEnable(GL_MULTISAMPLE);
        glViewport(0, 0, bw, bh);
    } else {
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(vx, vy, vw, vh);
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (g_blendEn) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    gl::UseProgram(g_glideProg);

    gl::Uniform1i(g_uMode, ResolveColorCombineMode());
    gl::Uniform1i(g_uBlendEn, g_blendEn ? 1 : 0);

    float cr, cg, cb, ca;
    UnpackARGB(g_constColorARGB, cr, cg, cb, ca);
    gl::Uniform4f(g_uConstColor, cr, cg, cb, 1.0f);
    gl::Uniform1i(g_uChromakeyEn, g_chromakeyEn ? 1 : 0);

    bool fogActive = g_fogEn && (g_cfg.fogMode != FOG_OFF);
    float fogMult = 1.0f;
    if (g_cfg.fogMode == FOG_EXT_4X) {
        fogMult = 4.0f;
    } else if (g_cfg.fogMode == FOG_EXT_2X) {
        fogMult = 2.0f;
    } else {
        // Auto-scale default fog smoothly with Extended Draw Distance so distant hills/tanks are visible
        static const float kAutoFogScale[4] = { 1.0f, 1.5f, 2.2f, 3.2f };
        fogMult = kAutoFogScale[std::clamp(g_cfg.drawDistMode, 0, 3)];
    }
    float fr, fg, fb, fa;
    UnpackARGB(g_fogColorARGB, fr, fg, fb, fa);
    gl::Uniform1i(g_uFogEn, fogActive ? 1 : 0);
    gl::Uniform3f(g_uFogColor, fr, fg, fb);
    gl::Uniform1f(g_uFogNear, g_fogNear * fogMult);
    gl::Uniform1f(g_uFogFar,  g_fogFar  * fogMult);
    gl::Uniform1f(g_uGamma,   g_gameGamma * g_cfg.gamma);

    gl::ActiveTexture(GL_TEXTURE0);
    auto it = g_texCache.find(g_curTexAddr);
    if (it != g_texCache.end() && it->second.glTex) {
        glBindTexture(GL_TEXTURE_2D, it->second.glTex);
        GLint minF = GL_LINEAR_MIPMAP_LINEAR;
        GLint magF = GL_LINEAR;
        switch (g_cfg.texFilter3D) {
            case TEXFILT_TRILINEAR:
                minF = GL_LINEAR_MIPMAP_LINEAR;
                magF = GL_LINEAR;
                break;
            case TEXFILT_BILINEAR:
                minF = GL_LINEAR;
                magF = GL_LINEAR;
                break;
            case TEXFILT_NEAREST:
                minF = GL_NEAREST;
                magF = GL_NEAREST;
                break;
            case TEXFILT_GAME:
            default:
                minF = g_gameTexLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST;
                magF = g_gameTexLinear ? GL_LINEAR : GL_NEAREST;
                break;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, g_texClampS ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, g_texClampT ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        if (g_stats.maxAniso > 1) {
            float af = static_cast<float>(std::clamp(g_cfg.anisotropy, 1, g_stats.maxAniso));
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, af);
        }
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    gl::Uniform1i(g_uTex, 0);
}

static inline float SafeByteFloat(float val, float fallback) {
    if (!std::isfinite(val)) return fallback;
    return std::clamp(val / 255.0f, 0.0f, 1.0f);
}

static GLVert ConvertVertex(const GrVertex& v) {
    GLVert out;
    out.x = (v.x / 320.0f) - 1.0f;
    out.y = 1.0f - (v.y / 240.0f);
    out.r = SafeByteFloat(v.r, 1.0f);
    out.g = SafeByteFloat(v.g, 1.0f);
    out.b = SafeByteFloat(v.b, 1.0f);
    out.a = SafeByteFloat(v.a, 0.5f);

    float oow = (std::isfinite(v.oow) && v.oow > 1e-7f) ? v.oow : 1.0f;
    float w = 1.0f / oow;
    out.w = w;
    out.u = (std::isfinite(v.tmuvtx[0].sow) ? (v.tmuvtx[0].sow * w) : 0.0f) / g_curTexMaxS;
    out.v = (std::isfinite(v.tmuvtx[0].tow) ? (v.tmuvtx[0].tow * w) : 0.0f) / g_curTexMaxT;
    return out;
}

static void DrawVerts(GLenum prim, const GLVert* verts, int count) {
    if (count <= 0) return;
    BindGlideTargetAndState();
    gl::BindVertexArray(g_glideVao);
    gl::BindBuffer(GL_ARRAY_BUFFER, g_glideVbo);
    gl::BufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(GLVert), verts);
    glDrawArrays(prim, 0, count);
    gl::BindVertexArray(0);
    g_frameDrawCalls++;
    if (prim == GL_TRIANGLES) g_frameTris += count / 3;
}

static void CompositeLfbOverlay() {
    if (!g_lfbWrittenThisFrame || !CheckLfbHasPixels(false)) return;

    for (int i = 0; i < LFB_W * LFB_H; ++i) {
        uint16_t p = g_lfb565[i];
        if (p == g_lfbSentinel) {
            g_lfbRGBA[i] = 0x00000000u;
        } else {
            uint8_t r = static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
            uint8_t g = static_cast<uint8_t>(((p >> 5)  & 0x3F) * 255 / 63);
            uint8_t b = static_cast<uint8_t>(( p        & 0x1F) * 255 / 31);
            g_lfbRGBA[i] = 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
        }
    }

    int vx, vy, vw, vh;
    if (GlideIsHorPlusView()) {
        // Keep 640x480 LFB cockpit grips, reticles, text & cursor 100% un-stretched in a centered 4:3 safe zone!
        VideoGet43Viewport(vx, vy, vw, vh);
    } else {
        VideoGetViewport(true, vx, vy, vw, vh);
    }
    glViewport(vx, vy, vw, vh);
    glDisable(GL_BLEND);
    gl::UseProgram(g_lfbProg);
    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_lfbTex);
    GLint lf = g_cfg.linearFilterLfb ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, LFB_W, LFB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_lfbRGBA);
    gl::Uniform1i(g_uLfbTex, 0);
    VideoDrawFullscreenQuad();
}

// ==================== 32 Native Glide 2.43 Functions (__stdcall) ====================

void __stdcall gl_grGlideInit() {
    LogPrintf("[GLIDE-GL] grGlideInit()\n");
    EnsureGlideGLInit();
}

void __stdcall gl_grGlideShutdown() {
    LogPrintf("[GLIDE-GL] grGlideShutdown()\n");
}

FxBool __stdcall gl_grSstQueryHardware(void* hwconfig) {
    if (hwconfig) {
        std::memset(hwconfig, 0, 128);
        uint32_t* p = reinterpret_cast<uint32_t*>(hwconfig);
        p[0] = 1; p[1] = 0; p[2] = 4; p[3] = 1;
        p[4] = 1; p[5] = 0; p[6] = 1; p[7] = 4;
    }
    return 1;
}

void __stdcall gl_grSstSelect(int) {}

FxBool __stdcall gl_grSstWinOpen(uint32_t, int32_t res, int32_t, int32_t, int32_t, int32_t, int32_t) {
    EnsureGlideGLInit();
    g_glideOpen = true;
    g_stats.inGlide3D = true;
    g_fboDirty = true;
    EnsureMsaaFbo();
    ClearLfbOverlay();
    LogPrintf("[GLIDE-GL] grSstWinOpen(res=%d) -> HD %dx%d @ %dx MSAA, %dx AF active!\n",
              res, g_stats.renderW, g_stats.renderH, g_stats.activeMsaa, g_cfg.anisotropy);
    return 1;
}

void __stdcall gl_grSstWinClose() {
    LogPrintf("[GLIDE-GL] grSstWinClose()\n");
    g_glideOpen = false;
    g_stats.inGlide3D = false;
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void __stdcall gl_grBufferClear(GrColor color, FxU8, FxU16) {
    EnsureMsaaFbo();
    float r, g, b, a;
    UnpackARGB(color, r, g, b, a);

    int vx, vy, vw, vh;
    VideoGetViewport(true, vx, vy, vw, vh);
    g_stats.renderW       = vw;
    g_stats.renderH       = vh;
    g_stats.horPlusActive = GlideIsHorPlusView();
    g_stats.currentView   = mem<int32_t>(addr::g_CurrentView);

    if (g_msaaFbo && g_msaaCur >= 2) {
        int bw = std::min(vw, g_msaaW);
        int bh = std::min(vh, g_msaaH);
        gl::BindFramebuffer(GL_FRAMEBUFFER, g_msaaFbo);
        glViewport(0, 0, bw, bh);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, bw, bh);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    } else {
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        int winW, winH;
        VideoGetWindowSize(winW, winH);
        glViewport(0, 0, winW, winH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_SCISSOR_TEST);
        glScissor(vx, vy, vw, vh);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }
    ClearLfbOverlay();
}

void __stdcall gl_grBufferSwap(int) {
    int vx, vy, vw, vh;
    VideoGetViewport(true, vx, vy, vw, vh);
    g_stats.renderW       = vw;
    g_stats.renderH       = vh;
    g_stats.horPlusActive = GlideIsHorPlusView();
    g_stats.currentView   = mem<int32_t>(addr::g_CurrentView);

    if (g_msaaFbo && g_msaaCur >= 2) {
        int winW, winH;
        VideoGetWindowSize(winW, winH);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, winW, winH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        int bw = std::min(vw, g_msaaW);
        int bh = std::min(vh, g_msaaH);
        gl::BindFramebuffer(GL_READ_FRAMEBUFFER, g_msaaFbo);
        gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl::BlitFramebuffer(0, 0, bw, bh,
                            vx, vy, vx + bw, vy + bh,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        // When MSAA is disabled, keep pillarbox/letterbox margins cleanly black on aspect transitions
        int winW, winH;
        VideoGetWindowSize(winW, winH);
        if (vx > 0 || vy > 0 || vw < winW || vh < winH) {
            gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
            glEnable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (vx > 0) {
                glScissor(0, 0, vx, winH);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (vx + vw < winW) {
                glScissor(vx + vw, 0, winW - (vx + vw), winH);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (vy > 0) {
                glScissor(0, 0, winW, vy);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            if (vy + vh < winH) {
                glScissor(0, vy + vh, winW, winH - (vy + vh));
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glDisable(GL_SCISSOR_TEST);
        }
    }

    CompositeLfbOverlay();

    g_stats.drawCalls = g_frameDrawCalls;
    g_stats.triangles = g_frameTris;
    g_stats.cachedTex = static_cast<int>(g_texCache.size());
    g_frameDrawCalls  = 0;
    g_frameTris       = 0;

    VideoPresentSwap();
    ClearLfbOverlay();
}

void __stdcall gl_grColorCombine(int32_t func, int32_t factor, int32_t local,
                                 int32_t other, FxBool invert) {
    g_ccFunc   = func;
    g_ccFactor = factor;
    g_ccLocal  = local;
    g_ccOther  = other;
    g_ccInvert = invert;
}

void __stdcall gl_grAlphaCombine(int32_t, int32_t, int32_t, int32_t, FxBool) {}

void __stdcall gl_grAlphaBlendFunction(int32_t rgbSf, int32_t rgbDf, int32_t, int32_t) {
    g_blendEn = (rgbSf == 1 && rgbDf == 5);
}

void __stdcall gl_grChromakeyMode(int32_t mode) { g_chromakeyEn = (mode != 0); }
void __stdcall gl_grChromakeyValue(GrColor value) { g_chromakeyARGB = value; }
void __stdcall gl_grConstantColorValue(GrColor value) { g_constColorARGB = value; }
void __stdcall gl_grFogColorValue(GrColor value) { g_fogColorARGB = value; }
void __stdcall gl_grFogMode(int32_t mode) { g_fogEn = (mode != GR_FOG_DISABLE); }
void __stdcall gl_grFogTable(const FxU8*) {}

void __stdcall gl_guFogGenerateLinear(FxU8* fogtable, float nearZ, float farZ) {
    g_fogNear = nearZ;
    g_fogFar  = farZ;
    if (fogtable) {
        for (int i = 0; i < 64; ++i) fogtable[i] = static_cast<FxU8>((i * 255) / 63);
    }
}

void __stdcall gl_grGammaCorrectionValue(float value) {
    g_gameGamma = (value > 0.1f) ? value : 1.0f;
}

FxU32 __stdcall gl_grTexMinAddress(int32_t) { return 0; }
FxU32 __stdcall gl_grTexMaxAddress(int32_t) { return 0x007FFFF8u; }

void __stdcall gl_grTexClampMode(int32_t, int32_t sClamp, int32_t tClamp) {
    g_texClampS = (sClamp != 0);
    g_texClampT = (tClamp != 0);
}

void __stdcall gl_grTexCombine(int32_t, int32_t, int32_t, int32_t, int32_t, FxBool, FxBool) {}

void __stdcall gl_grTexFilterMode(int32_t, int32_t minFilt, int32_t magFilt) {
    g_gameTexLinear = (minFilt != 0 || magFilt != 0);
}

void __stdcall gl_grTexDownloadTable(int32_t, int32_t, void* data) {
    if (!data) return;
    const uint32_t* src = reinterpret_cast<const uint32_t*>(data);
    bool changed = false;
    for (int i = 0; i < 256; ++i) {
        uint32_t c = src[i];
        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >> 8)  & 0xFF;
        uint8_t b =  c        & 0xFF;
        uint32_t rgba = 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
        if (g_palRGBA[i] != rgba) {
            g_palRGBA[i] = rgba;
            changed = true;
        }
    }
    if (changed) {
        for (auto& kv : g_texCache) {
            UploadExpandedTexture(kv.second);
        }
    }
}

static uint32_t g_texClampWarnCount = 0;

void __stdcall gl_grTexDownloadMipMap(int32_t, FxU32 startAddress, FxU32, GrTexInfo* info) {
    if (!info || !info->data) return;
    int dim = 256 >> std::clamp(info->largeLod, 0, 8);
    if (dim < 1) dim = 1;

    // Exact Glide 2.43 aspect ratios:
    //   0=8x1, 1=4x1, 2=2x1, 3=1x1, 4=1x2, 5=1x4, 6=1x8
    int aspect = std::clamp(info->aspectRatio, 0, 6);
    int w = dim;
    int h = dim;
    float maxS = 256.0f;
    float maxT = 256.0f;
    if (aspect < 3) {
        int shift = 3 - aspect; // 1=2x1, 2=4x1, 3=8x1
        h = std::max(1, dim >> shift);
        maxT = 256.0f / static_cast<float>(1 << shift);
    } else if (aspect > 3) {
        int shift = aspect - 3; // 1=1x2, 2=1x4, 3=1x8
        w = std::max(1, dim >> shift);
        maxS = 256.0f / static_cast<float>(1 << shift);
    }

    TexSlot& slot = g_texCache[startAddress];
    if (!slot.glTex) glGenTextures(1, &slot.glTex);
    slot.width  = w;
    slot.height = h;
    slot.maxS   = maxS;
    slot.maxT   = maxT;

    size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
    slot.rawIdx.assign(n, 0);

    // Guard against M1TP2's 0x42394D quirk (surfaces with height <= 16 inherit stale aspectRatio)
    // or any undersized source allocation so memcpy can never read off the end of a page.
    size_t avail = HeapGuardQueryReadableBytes(info->data);
    size_t copyBytes = std::min(n, avail);
    if (copyBytes < n) {
        g_texClampWarnCount++;
        if (g_texClampWarnCount <= 16) {
            LogPrintf("[GLIDE-GL] Clamped texture upload at 0x%X (%dx%d aspect=%d lod=%d): requested=%zu avail=%zu ptr=%p\n",
                      startAddress, w, h, aspect, info->largeLod, n, avail, info->data);
        }
    }
    if (copyBytes > 0) {
        std::memcpy(slot.rawIdx.data(), info->data, copyBytes);
    }
    UploadExpandedTexture(slot);

    if (g_curTexAddr == startAddress) {
        g_curTexMaxS = slot.maxS;
        g_curTexMaxT = slot.maxT;
    }
}

void __stdcall gl_grTexSource(int32_t, FxU32 startAddress, FxU32, GrTexInfo*) {
    g_curTexAddr = startAddress;
    auto it = g_texCache.find(startAddress);
    if (it != g_texCache.end()) {
        g_curTexMaxS = it->second.maxS;
        g_curTexMaxT = it->second.maxT;
    } else {
        g_curTexMaxS = 256.0f;
        g_curTexMaxT = 256.0f;
    }
}

FxBool __stdcall gl_grLfbLock(int32_t type, int32_t, int32_t, int32_t, FxBool, GrLfbInfo* info) {
    if (!info) return 0;
    info->lfbPtr        = g_lfb565;
    info->strideInBytes = LFB_W * 2;
    info->writeMode     = GR_LFBWRITEMODE_565;
    info->origin        = 0;
    if (type == GR_LFB_WRITE_ONLY) {
        g_lfbWrittenThisFrame = true;
        g_lfbLockWritePending = true;
    }
    return 1;
}

FxBool __stdcall gl_grLfbUnlock(int32_t, int32_t) {
    CheckLfbHasPixels(true);
    return 1;
}

void __stdcall gl_grDrawPoint(const GrVertex* pt) {
    if (!pt) return;
    if (CheckLfbHasPixels(false)) {
        if (std::isfinite(pt->x) && std::isfinite(pt->y)) {
            PutLfbPixel(static_cast<int>(std::lround(pt->x)),
                        static_cast<int>(std::lround(pt->y)),
                        ResolveCurrentGlideColor565(*pt));
        }
        return;
    }
    GLVert v = ConvertVertex(*pt);
    glPointSize(std::max(2.0f, g_stats.renderH / 240.0f));
    DrawVerts(GL_POINTS, &v, 1);
}

void __stdcall gl_grDrawLine(const GrVertex* v1, const GrVertex* v2) {
    if (!v1 || !v2) return;
    // In real Glide, 2D HUD indicators (GPS/GAS tank hull & turret dial 0x435CB0, TC CITV azimuth lines)
    // draw via grDrawLine AFTER the cockpit LFB mask is blitted. Rasterize them directly into g_lfb565
    // so they appear in hardware-exact Z-order on top of the cockpit mask and under any later LFB cursor/frame.
    if (CheckLfbHasPixels(false)) {
        RasterizeLfbLine(v1->x, v1->y, v2->x, v2->y, ResolveCurrentGlideColor565(*v1));
        return;
    }
    if (std::fabs(v1->x - v2->x) < 0.5f && std::fabs(v1->y - v2->y) < 0.5f) {
        gl_grDrawPoint(v1);
        return;
    }
    GLVert vs[2] = { ConvertVertex(*v1), ConvertVertex(*v2) };
    glLineWidth(std::max(1.5f, g_stats.renderH / 480.0f));
    DrawVerts(GL_LINES, vs, 2);
}

void __stdcall gl_guDrawPolygonVertexListWithClip(int nverts, const GrVertex* vlist) {
    if (nverts < 3 || !vlist) return;
    g_batchVerts.clear();
    GLVert v0 = ConvertVertex(vlist[0]);
    for (int i = 1; i + 1 < nverts; ++i) {
        g_batchVerts.push_back(v0);
        g_batchVerts.push_back(ConvertVertex(vlist[i]));
        g_batchVerts.push_back(ConvertVertex(vlist[i + 1]));
    }
    DrawVerts(GL_TRIANGLES, g_batchVerts.data(), static_cast<int>(g_batchVerts.size()));
}

int __stdcall gl_UnusedStub() { return 0; }

int __cdecl Hook_D3DInitRedirect() {
    if (g_cfg.forceGlideOnD3D) {
        LogPrintf("[GLIDE-GL] Redirecting Direct3D selection -> Native OpenGL Glide HD!\n");
        using GlideInitFn = int(__cdecl*)();
        return reinterpret_cast<GlideInitFn>(addr::GlideInitEntry)();
    }
    return 0;
}

int __cdecl Hook_LoadGlide2x() {
    uint32_t* tbl = reinterpret_cast<uint32_t*>(addr::g_GlideTableBase);
    tbl[0] = 0x1337BEEFu;
    for (int i = 1; i <= 122; ++i) {
        tbl[i] = reinterpret_cast<uint32_t>(&gl_UnusedStub);
    }

    auto setPtr = [](uint32_t va, auto fn) {
        mem<uint32_t>(va) = reinterpret_cast<uint32_t>(fn);
    };
    setPtr(0x46F1C4, &gl_grAlphaBlendFunction);
    setPtr(0x46F1C8, &gl_grAlphaCombine);
    setPtr(0x46F1D8, &gl_grBufferClear);
    setPtr(0x46F1E0, &gl_grBufferSwap);
    setPtr(0x46F1E8, &gl_grChromakeyMode);
    setPtr(0x46F1EC, &gl_grChromakeyValue);
    setPtr(0x46F1F4, &gl_grColorCombine);
    setPtr(0x46F1FC, &gl_grConstantColorValue);
    setPtr(0x46F220, &gl_grDrawLine);
    setPtr(0x46F22C, &gl_grDrawPoint);
    setPtr(0x46F240, &gl_grFogColorValue);
    setPtr(0x46F244, &gl_grFogMode);
    setPtr(0x46F248, &gl_grFogTable);
    setPtr(0x46F24C, &gl_grGammaCorrectionValue);
    setPtr(0x46F258, &gl_grGlideInit);
    setPtr(0x46F264, &gl_grGlideShutdown);
    setPtr(0x46F274, &gl_grLfbLock);
    setPtr(0x46F27C, &gl_grLfbUnlock);
    setPtr(0x46F2B0, &gl_grSstQueryHardware);
    setPtr(0x46F2C0, &gl_grSstSelect);
    setPtr(0x46F2D0, &gl_grSstWinClose);
    setPtr(0x46F2D4, &gl_grSstWinOpen);
    setPtr(0x46F2DC, &gl_grTexClampMode);
    setPtr(0x46F2E0, &gl_grTexCombine);
    setPtr(0x46F2EC, &gl_grTexDownloadMipMap);
    setPtr(0x46F2F8, &gl_grTexDownloadTable);
    setPtr(0x46F300, &gl_grTexFilterMode);
    setPtr(0x46F308, &gl_grTexMaxAddress);
    setPtr(0x46F30C, &gl_grTexMinAddress);
    setPtr(0x46F320, &gl_grTexSource);
    setPtr(0x46F340, &gl_guDrawPolygonVertexListWithClip);
    setPtr(0x46F35C, &gl_guFogGenerateLinear);

    LogPrintf("[GLIDE-GL] Native OpenGL Glide 2.43 table registered (32 active functions)\n");
    return 1;
}

// ==================== True Hor+ 3D Projection & Frustum Hooks ====================

struct M1Vertex3D {
    int32_t  x;       // +0x00
    int32_t  y;       // +0x04
    int32_t  z;       // +0x08
    int32_t  screenX; // +0x0C
    int32_t  screenY; // +0x10
    uint8_t  pad[10]; // +0x14..+0x1D
    uint16_t flags;   // +0x1E
};
static_assert(offsetof(M1Vertex3D, flags) == 0x1E, "M1Vertex3D flags offset mismatch");

static void ApplyDrawDistanceAndLod() {
    int mode = std::clamp(g_cfg.drawDistMode, 0, 3);
    // 0x4447D6: mov eax, imm32 (added to 0x177000 for total terrain + vehicle render box half-extent)
    static const uint32_t kDrawBaseImm[4] = {
        0x0007D000u, // 0: Default 2.0 km (0x1F4000)
        0x00177000u, // 1: Extended 3.0 km (0x2EE000)
        0x00271000u, // 2: Long 4.0 km (0x3E8000)
        0x00465000u, // 3: Max 6.0 km (0x5DC000)
    };
    static const int32_t kLodStep[4] = {
        179200,      // 1x stock
        358400,      // 2x high-detail terrain distance
        537600,      // 3x ultra terrain distance
        716800,      // 4x max terrain distance
    };
    uint32_t imm = kDrawBaseImm[mode];
    WriteBytes(addr::PatchDrawRadiusImm, &imm, sizeof(imm));
    mem<int32_t>(addr::g_TerrainLodStep) = kLodStep[mode];
}

static int32_t ComputeHorPlusFocalX() {
    if (!GlideIsHorPlusView()) {
        return 512;
    }
    int32_t clipW = mem<int32_t>(addr::g_ClipRight) - mem<int32_t>(addr::g_ClipLeft) + 1;
    if (clipW < 600) return 512; // Keep sub-viewports (interior monitors/CITV) untouched
    int winW = 0, winH = 0;
    VideoGetWindowSize(winW, winH);
    if (winW <= 0 || winH <= 0) return 512;
    double winAspect = static_cast<double>(winW) / static_cast<double>(winH);
    constexpr double kBaseAspect = 4.0 / 3.0;
    if (winAspect <= kBaseAspect + 0.01) {
        return 512;
    }
    return std::clamp(static_cast<int32_t>(std::lround(512.0 * (kBaseAspect / winAspect))), 180, 512);
}

// Replaces 0x44DF1D..0x44DF96 (the bounding-sphere frustum cull tail of 0x44DEC0).
// Stock 1998 code did `shl eax, 9` in 32-bit signed math on unshifted 1.15 fixed-point camX/camY,
// overflowing whenever |camX| >= 0x400000 (happening frequently near the left/right edges of 21:9
// Hor+ views and causing 3D tank models to flicker in and out). Also uses true sphere-plane
// margin `radius * sqrt(1 + slope^2)` so wide models & gun barrels never pop early at screen edges.
extern "C" int32_t __cdecl Impl_Cull3dzSphere() {
    const uint8_t* ctx = mem<const uint8_t*>(addr::g_3dzCullCtxPtr);
    if (!ctx) return 0;
    const uint16_t* hdr = *reinterpret_cast<const uint16_t* const*>(ctx + 0x00);
    if (!hdr) return 0;

    int32_t shift  = static_cast<int32_t>(mem<uint8_t>(addr::g_3dzPosShift));
    int32_t rShift = std::clamp(15 - shift, 0, 30);
    int64_t radius = static_cast<int64_t>(hdr[0]) << rShift;

    int64_t camX = static_cast<int64_t>(*reinterpret_cast<const int32_t*>(ctx + 0x18));
    int64_t camY = static_cast<int64_t>(*reinterpret_cast<const int32_t*>(ctx + 0x1C));
    int64_t camZ = static_cast<int64_t>(*reinterpret_cast<const int32_t*>(ctx + 0x20));

    if (camZ + radius < 0) return 1;

    int32_t focalX = ComputeHorPlusFocalX();
    double slopeX = 320.0 / static_cast<double>(std::max(180, focalX)); // 0.625 in 4:3, ~1.12 in 21:9
    double slopeY = 320.0 / 512.0;                                      // generous 320 vs 240 half-height

    double radX = static_cast<double>(radius) * std::sqrt(1.0 + slopeX * slopeX) * 1.15;
    double radY = static_cast<double>(radius) * std::sqrt(1.0 + slopeY * slopeY) * 1.15;

    double zD = static_cast<double>(camZ);
    if (std::fabs(static_cast<double>(camX)) > zD * slopeX + radX) return 1;
    if (std::fabs(static_cast<double>(camY)) > zD * slopeY + radY) return 1;
    return 0;
}

__attribute__((naked)) static void Thunk_Cull3dzTail() {
    asm volatile(
        "call _Impl_Cull3dzSphere\n\t"
        "popl %%ebp\n\t"
        "popl %%edi\n\t"
        "popl %%esi\n\t"
        "popl %%ebx\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

extern "C" void __cdecl Impl_ClassifyFrustumVertex(M1Vertex3D* v) {
    if (!v) return;
    int32_t clipL = mem<int32_t>(addr::g_ClipLeft);
    int32_t clipR = mem<int32_t>(addr::g_ClipRight);
    int32_t nearZ = mem<int32_t>(addr::g_NearZ);

    int32_t width = clipR - clipL + 1;
    int64_t prod  = static_cast<int64_t>(v->z) * (static_cast<int64_t>(width) << 8);
    int32_t limY  = static_cast<int32_t>(prod >> 15);

    int32_t focalX = ComputeHorPlusFocalX();
    int32_t limX = limY;
    if (focalX < 512) {
        limX = static_cast<int32_t>((static_cast<int64_t>(limY) * 512) / focalX);
        if (limX > 0) {
            limX += (limX >> 4); // +6.25% safety margin prevents edge-tile pop-in on wide screens
        }
    }

    uint16_t flags = 0;
    int32_t x8    = static_cast<int32_t>(static_cast<uint32_t>(v->x) << 3);
    int32_t negX8 = static_cast<int32_t>(0u - static_cast<uint32_t>(x8));
    if (x8 > limX)    flags |= 0x02u;
    if (negX8 > limX) flags |= 0x01u;

    int32_t y8    = static_cast<int32_t>(static_cast<uint32_t>(v->y) << 3);
    int32_t negY8 = static_cast<int32_t>(0u - static_cast<uint32_t>(y8));
    if (y8 > limY)    flags |= 0x08u;
    if (negY8 > limY) flags |= 0x04u;

    if (v->z < nearZ) flags |= 0x10u;
    flags |= 0x40u;
    v->flags = flags;
}

extern "C" void __cdecl Impl_Project3DVertex(M1Vertex3D* v) {
    if (!v) return;
    uint32_t flags = v->flags & 0xFFu;
    int32_t z = v->z;
    int32_t nearZ = mem<int32_t>(addr::g_NearZ);

    if (z >= nearZ && z != 0) {
        int32_t focalX = ComputeHorPlusFocalX();

        int32_t sx = static_cast<int32_t>((static_cast<int64_t>(v->x) * focalX) / z)
                     + mem<int32_t>(addr::g_CenterX);
        if (sx < mem<int32_t>(addr::g_ClipLeft))  flags |= 0x100u;
        if (sx > mem<int32_t>(addr::g_ClipRight)) flags |= 0x200u;
        v->screenX = sx - mem<int32_t>(addr::g_ViewX);

        int32_t sy = static_cast<int32_t>((static_cast<int64_t>(v->y) * 512) / z)
                     + mem<int32_t>(addr::g_CenterY);
        if (sy < mem<int32_t>(addr::g_ClipTop))    flags |= 0x400u;
        if (sy > mem<int32_t>(addr::g_ClipBottom)) flags |= 0x800u;
        v->screenY = sy - mem<int32_t>(addr::g_ViewY);

        flags |= 0x1000u;
    }
    v->flags = static_cast<uint16_t>(flags);
}

// Hand-written MicroProse 3D assembly callers expect ECX and EDX preserved across 0x4599ED / 0x459A5D!
__attribute__((naked)) static void Thunk_ClassifyFrustumVertex() {
    asm volatile(
        "pushl %%ecx\n\t"
        "pushl %%edx\n\t"
        "pushl 12(%%esp)\n\t"
        "call _Impl_ClassifyFrustumVertex\n\t"
        "addl $4, %%esp\n\t"
        "popl %%edx\n\t"
        "popl %%ecx\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

__attribute__((naked)) static void Thunk_Project3DVertex() {
    asm volatile(
        "pushl %%ecx\n\t"
        "pushl %%edx\n\t"
        "pushl 12(%%esp)\n\t"
        "call _Impl_Project3DVertex\n\t"
        "addl $4, %%esp\n\t"
        "popl %%edx\n\t"
        "popl %%ecx\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

} // namespace

bool GlideIsHorPlusView() {
    if (!g_stats.inGlide3D) return false;
    if (g_cfg.aspectMode != ASPECT3D_SMART_HORPLUS && g_cfg.aspectMode != ASPECT3D_ALL_HORPLUS) {
        return false;
    }
    int winW = 0, winH = 0;
    VideoGetWindowSize(winW, winH);
    if (winW <= 0 || winH <= 0 || (winW * 3 <= winH * 4)) return false;

    int32_t view = mem<int32_t>(addr::g_CurrentView);
    // Wide views with open 3D vistas: 6=TC Outhatch (.50 cal), 7=Chase View, 8=External/Missile
    if (view == 6 || view == 7 || view == 8) return true;
    // Optional "ALL 3D HOR+" also widens 3=GPS Primary Sight & 5=TC Vision Blocks
    // Interior cockpits (0, 1), IVIS Map (2), and Circular GAS Tube (4) ALWAYS stay crisp 4:3!
    if (g_cfg.aspectMode == ASPECT3D_ALL_HORPLUS && (view == 3 || view == 5)) return true;
    return false;
}

const char* GlideGetViewName(int viewId) {
    switch (viewId) {
        case 0: return "TC COCKPIT";
        case 1: return "GUNNER PIT";
        case 2: return "IVIS MAP";
        case 3: return "GPS SIGHT";
        case 4: return "GAS SIGHT";
        case 5: return "VISION BLKS";
        case 6: return "TC HATCH";
        case 7: return "CHASE VIEW";
        case 8: return "EXTERNAL";
        default: return "3D VIEW";
    }
}

void GlideApplySettings() {
    g_fboDirty = true;
    ApplyDrawDistanceAndLod();
}

void GlideGLInstallHooks() {
    InstallJump(addr::LoadGlide2x,           reinterpret_cast<void*>(&Hook_LoadGlide2x));
    InstallCall(addr::D3DInitCallSite,       reinterpret_cast<void*>(&Hook_D3DInitRedirect));
    InstallJump(addr::ClassifyFrustumVertex, reinterpret_cast<void*>(&Thunk_ClassifyFrustumVertex));
    InstallJump(addr::Project3DVertex,       reinterpret_cast<void*>(&Thunk_Project3DVertex));
    InstallJump(addr::Patch3dzCullTail,      reinterpret_cast<void*>(&Thunk_Cull3dzTail));
    ApplyDrawDistanceAndLod();
    LogPrintf("[GLIDE-GL] LoadGlide2x, True Hor+ 3D, 64-bit 3DZ cull fix & Extended Draw Distance/LOD hooks installed\n");
}

void GlideGLShutdown() {
    for (auto& kv : g_texCache) {
        if (kv.second.glTex) glDeleteTextures(1, &kv.second.glTex);
    }
    g_texCache.clear();
    if (g_msaaFbo)   { gl::DeleteFramebuffers(1, &g_msaaFbo); g_msaaFbo = 0; }
    if (g_msaaRbo)   { gl::DeleteRenderbuffers(1, &g_msaaRbo); g_msaaRbo = 0; }
    if (g_lfbTex)    { glDeleteTextures(1, &g_lfbTex); g_lfbTex = 0; }
    if (g_glideProg) { gl::DeleteProgram(g_glideProg); g_glideProg = 0; }
    if (g_lfbProg)   { gl::DeleteProgram(g_lfbProg); g_lfbProg = 0; }
    if (g_glideVbo)  { gl::DeleteBuffers(1, &g_glideVbo); g_glideVbo = 0; }
    if (g_glideVao)  { gl::DeleteVertexArrays(1, &g_glideVao); g_glideVao = 0; }
    g_glInited = false;
}

} // namespace m1

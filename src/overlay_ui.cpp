#include "overlay_ui.h"
#include "video_sdl.h"
#include "glide_gl.h"
#include "vfs_mod.h"
#include "gl_loader.h"
#include "common.h"
#include <SDL3/SDL_opengl.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace m1 {

namespace {

bool g_open = false;
int  g_selected = 0;
bool g_inited = false;

GLuint g_uiProg = 0;
GLuint g_uiVao  = 0;
GLuint g_uiVbo  = 0;
GLint  g_uRes   = -1;

struct UIVert {
    float x, y;
    float r, g, b, a;
};
std::vector<UIVert> g_verts;

static const char* kUIVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aCol;
uniform vec2 uRes;
out vec4 vCol;
void main() {
    vCol = aCol;
    vec2 ndc = vec2((aPos.x / uRes.x) * 2.0 - 1.0, 1.0 - (aPos.y / uRes.y) * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

static const char* kUIFS = R"GLSL(
#version 330 core
in vec4 vCol;
out vec4 FragColor;
void main() {
    FragColor = vCol;
}
)GLSL";

// Compact 5x7 uppercase/digits/punctuation bitmap font (covers ASCII 32..95)
static const uint8_t kFont5x7[64][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // '!'
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // '#'
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // '$'
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // '%'
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // '&'
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // '''
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // '('
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // ')'
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // '*'
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, // ','
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // '.'
    {0x01,0x02,0x04,0x04,0x08,0x10,0x00}, // '/'
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // '0'
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // '1'
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // '2'
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // '3'
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // '4'
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // '5'
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // '6'
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // '7'
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // '8'
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // '9'
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // ':'
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, // ';'
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // '<'
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // '='
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // '>'
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // '?'
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // '@'
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'A'
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 'B'
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 'C'
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 'D'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 'E'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 'F'
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 'G'
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'H'
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'I'
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 'J'
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 'K'
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 'L'
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 'M'
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 'N'
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'O'
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 'P'
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 'Q'
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 'R'
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 'S'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 'T'
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'U'
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 'V'
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 'W'
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 'X'
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 'Y'
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 'Z'
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // '['
    {0x10,0x08,0x04,0x04,0x02,0x01,0x00}, // '\'
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ']'
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // '_'
};

static void PushRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    UIVert v0{x,     y,     r,g,b,a};
    UIVert v1{x + w, y,     r,g,b,a};
    UIVert v2{x + w, y + h, r,g,b,a};
    UIVert v3{x,     y + h, r,g,b,a};
    g_verts.push_back(v0); g_verts.push_back(v1); g_verts.push_back(v2);
    g_verts.push_back(v0); g_verts.push_back(v2); g_verts.push_back(v3);
}

static void PushText(float x, float y, float scale, const char* text, float r, float g, float b, float a = 1.0f) {
    float cx = x;
    while (*text) {
        char ch = *text++;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (ch >= 32 && ch <= 95) {
            const uint8_t* rows = kFont5x7[ch - 32];
            for (int ry = 0; ry < 7; ++ry) {
                uint8_t bits = rows[ry];
                for (int rx = 0; rx < 5; ++rx) {
                    if (bits & (0x10 >> rx)) {
                        PushRect(cx + rx * scale, y + ry * scale, scale, scale, r, g, b, a);
                    }
                }
            }
        }
        cx += 6.0f * scale;
    }
}

constexpr int kNumItems = 15;

static void FormatItem(int idx, char* label, char* value) {
    switch (idx) {
        case 0:
            std::strcpy(label, "DISPLAY MODE");
            std::strcpy(value, g_cfg.fullscreen ? "BORDERLESS FULLSCREEN" : "WINDOWED");
            break;
        case 1: {
            std::strcpy(label, "2D MENU / MAP ASPECT");
            const char* names[] = { "FILL SCREEN", "4:3 ORIGINAL", "16:10 WIDE", "CROP ZOOM" };
            std::strcpy(value, names[std::clamp(g_cfg.menuAspectMode, 0, 3)]);
            break;
        }
        case 2: {
            std::strcpy(label, "3D COMBAT ASPECT");
            const char* names[] = {
                "SMART HOR+ WIDESCREEN",
                "ALL 3D HOR+ (INC. GPS)",
                "4:3 ORIGINAL",
                "FILL SCREEN (STRETCH)"
            };
            std::strcpy(value, names[std::clamp(g_cfg.aspectMode, 0, 3)]);
            break;
        }
        case 3:
            std::strcpy(label, "VSYNC");
            std::strcpy(value, g_cfg.vsync ? "ON" : "OFF");
            break;
        case 4:
            std::strcpy(label, "3D MSAA ANTI-ALIASING");
            if (g_cfg.msaaSamples <= 0) std::strcpy(value, "OFF");
            else std::snprintf(value, 64, "%dX MSAA", g_cfg.msaaSamples);
            break;
        case 5:
            std::strcpy(label, "3D ANISOTROPIC FILTERING");
            if (g_cfg.anisotropy <= 1) std::strcpy(value, "OFF (1X)");
            else std::snprintf(value, 64, "%dX AF", g_cfg.anisotropy);
            break;
        case 6: {
            std::strcpy(label, "3D TEXTURE FILTER");
            const char* names[] = { "TRILINEAR (MIPMAPS)", "BILINEAR", "NEAREST (RETRO)", "GAME DEFAULT" };
            std::strcpy(value, names[std::clamp(g_cfg.texFilter3D, 0, 3)]);
            break;
        }
        case 7:
            std::strcpy(label, "2D MENU FILTER");
            std::strcpy(value, g_cfg.linearFilter2D ? "BILINEAR" : "NEAREST (CRISP)");
            break;
        case 8:
            std::strcpy(label, "3D COCKPIT/HUD FILTER");
            std::strcpy(value, g_cfg.linearFilterLfb ? "BILINEAR" : "NEAREST (CRISP)");
            break;
        case 9: {
            std::strcpy(label, "3D FOG DISTANCE");
            const char* names[] = { "GAME DEFAULT (1X)", "EXTENDED 2X", "EXTENDED 4X", "OFF (CLEAR HORIZON)" };
            std::strcpy(value, names[std::clamp(g_cfg.fogMode, 0, 3)]);
            break;
        }
        case 10:
            std::strcpy(label, "3D GAMMA");
            std::snprintf(value, 64, "%.2f", g_cfg.gamma);
            break;
        case 11:
            std::strcpy(label, "FORCE NATIVE GLIDE ON D3D");
            std::strcpy(value, g_cfg.forceGlideOnD3D ? "ON" : "OFF");
            break;
        case 12:
            std::strcpy(label, "SKIP INTRO VIDEOS");
            std::strcpy(value, g_cfg.skipIntro ? "ON" : "OFF");
            break;
        case 13:
            std::strcpy(label, "MODS VFS (MODS/)");
            if (!g_cfg.enableMods) {
                std::strcpy(value, "OFF");
            } else {
                std::snprintf(value, 64, "ON (%d MODS / %d FILES)", g_stats.activeMods, g_stats.modFiles);
            }
            break;
        case 14:
            std::strcpy(label, "SHOW FPS / RENDER STATS");
            std::strcpy(value, g_cfg.showStatsHud ? "ON" : "OFF");
            break;
    }
}

static void AdjustItem(int idx, int dir) {
    switch (idx) {
        case 0:
            g_cfg.fullscreen = !g_cfg.fullscreen;
            VideoApplyDisplaySettings();
            break;
        case 1:
            g_cfg.menuAspectMode = (g_cfg.menuAspectMode + dir + 4) % 4;
            break;
        case 2:
            g_cfg.aspectMode = (g_cfg.aspectMode + dir + 4) % 4;
            GlideApplySettings();
            break;
        case 3:
            g_cfg.vsync = !g_cfg.vsync;
            VideoApplyDisplaySettings();
            break;
        case 4: {
            const int opts[] = { 0, 2, 4, 8 };
            int cur = 0;
            for (int i = 0; i < 4; ++i) if (opts[i] == g_cfg.msaaSamples) cur = i;
            do {
                cur = (cur + dir + 4) % 4;
            } while (opts[cur] > g_stats.maxMsaa && cur != 0);
            g_cfg.msaaSamples = opts[cur];
            GlideApplySettings();
            break;
        }
        case 5: {
            const int opts[] = { 1, 2, 4, 8, 16 };
            int cur = 0;
            for (int i = 0; i < 5; ++i) if (opts[i] == g_cfg.anisotropy) cur = i;
            do {
                cur = (cur + dir + 5) % 5;
            } while (opts[cur] > g_stats.maxAniso && cur != 0);
            g_cfg.anisotropy = opts[cur];
            GlideApplySettings();
            break;
        }
        case 6:
            g_cfg.texFilter3D = (g_cfg.texFilter3D + dir + 4) % 4;
            GlideApplySettings();
            break;
        case 7:
            g_cfg.linearFilter2D = !g_cfg.linearFilter2D;
            VideoApplyDisplaySettings();
            break;
        case 8:
            g_cfg.linearFilterLfb = !g_cfg.linearFilterLfb;
            GlideApplySettings();
            break;
        case 9:
            g_cfg.fogMode = (g_cfg.fogMode + dir + 4) % 4;
            break;
        case 10:
            g_cfg.gamma = std::clamp(std::round((g_cfg.gamma + dir * 0.05f) * 20.0f) / 20.0f, 0.70f, 1.60f);
            break;
        case 11:
            g_cfg.forceGlideOnD3D = !g_cfg.forceGlideOnD3D;
            break;
        case 12:
            g_cfg.skipIntro = !g_cfg.skipIntro;
            break;
        case 13:
            g_cfg.enableMods = !g_cfg.enableMods;
            if (g_cfg.enableMods) VfsModReload();
            break;
        case 14:
            g_cfg.showStatsHud = !g_cfg.showStatsHud;
            break;
    }
    SaveConfig();
}

static void FlushUI(int winW, int winH) {
    if (g_verts.empty()) return;
    glViewport(0, 0, winW, winH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    gl::UseProgram(g_uiProg);
    gl::Uniform2f(g_uRes, static_cast<float>(winW), static_cast<float>(winH));
    gl::BindVertexArray(g_uiVao);
    gl::BindBuffer(GL_ARRAY_BUFFER, g_uiVbo);
    gl::BufferData(GL_ARRAY_BUFFER, g_verts.size() * sizeof(UIVert), g_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_verts.size()));
    gl::BindVertexArray(0);
    g_verts.clear();
}

} // namespace

void OverlayInit() {
    if (g_inited) return;
    g_uiProg = gl::BuildProgram(kUIVS, kUIFS, "RecompOverlayUI");
    g_uRes = gl::GetUniformLocation(g_uiProg, "uRes");
    gl::GenVertexArrays(1, &g_uiVao);
    gl::BindVertexArray(g_uiVao);
    gl::GenBuffers(1, &g_uiVbo);
    gl::BindBuffer(GL_ARRAY_BUFFER, g_uiVbo);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)offsetof(UIVert, x));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)offsetof(UIVert, r));
    gl::BindVertexArray(0);
    g_verts.reserve(16384);
    g_inited = true;
}

void OverlayShutdown() {
    if (!g_inited) return;
    if (g_uiVbo)  { gl::DeleteBuffers(1, &g_uiVbo); g_uiVbo = 0; }
    if (g_uiVao)  { gl::DeleteVertexArrays(1, &g_uiVao); g_uiVao = 0; }
    if (g_uiProg) { gl::DeleteProgram(g_uiProg); g_uiProg = 0; }
    g_inited = false;
}

bool OverlayIsOpen() { return g_open; }

void OverlayToggle() {
    g_open = !g_open;
    if (g_open) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
        SaveConfig();
    }
}

bool OverlayHandleEvent(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
        if (ev.key.scancode == SDL_SCANCODE_F12 || ev.key.scancode == SDL_SCANCODE_INSERT) {
            OverlayToggle();
            return true;
        }
    }
    if (!g_open) return false;

    if (ev.type == SDL_EVENT_KEY_DOWN) {
        switch (ev.key.scancode) {
            case SDL_SCANCODE_ESCAPE:
                OverlayToggle();
                return true;
            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_W:
                g_selected = (g_selected - 1 + kNumItems) % kNumItems;
                return true;
            case SDL_SCANCODE_DOWN:
            case SDL_SCANCODE_S:
                g_selected = (g_selected + 1) % kNumItems;
                return true;
            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_A:
                AdjustItem(g_selected, -1);
                return true;
            case SDL_SCANCODE_RIGHT:
            case SDL_SCANCODE_D:
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_SPACE:
                AdjustItem(g_selected, +1);
                return true;
            default:
                return true; // swallow all keys while menu is open
        }
    }

    if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        int winW = 1280, winH = 960;
        VideoGetWindowSize(winW, winH);
        float scale = std::clamp(winH / 480.0f, 1.5f, 4.0f);
        float panelW = 340.0f * scale;
        float panelH = (38.0f + kNumItems * 15.0f + 34.0f) * scale;
        float px = (winW - panelW) * 0.5f;
        float py = (winH - panelH) * 0.5f;
        float listTop = py + 34.0f * scale;
        float rowH = 15.0f * scale;

        float mx = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.x : ev.button.x;
        float my = (ev.type == SDL_EVENT_MOUSE_MOTION) ? ev.motion.y : ev.button.y;

        if (mx >= px && mx <= px + panelW && my >= listTop && my < listTop + kNumItems * rowH) {
            int row = static_cast<int>((my - listTop) / rowH);
            if (row >= 0 && row < kNumItems) {
                g_selected = row;
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    int dir = (ev.button.button == SDL_BUTTON_RIGHT) ? -1 : +1;
                    AdjustItem(g_selected, dir);
                }
            }
        }
        return true; // swallow all mouse input while menu is open
    }

    if (ev.type == SDL_EVENT_TEXT_INPUT || ev.type == SDL_EVENT_KEY_UP) {
        return true;
    }
    return false;
}

void OverlayRender(int winW, int winH) {
    if (!g_inited || winW <= 0 || winH <= 0) return;
    g_verts.clear();

    float scale = std::clamp(winH / 480.0f, 1.5f, 4.0f);

    // Compact top-left FPS / Renderer Stats HUD if enabled
    if (g_cfg.showStatsHud) {
        char buf[192];
        if (g_stats.inGlide3D) {
            std::snprintf(buf, sizeof(buf),
                "GLIDE HD %dx%d [%s %s] | %.0f FPS | MSAA %dX | AF %dX | TRIS %d",
                g_stats.renderW, g_stats.renderH,
                GlideGetViewName(g_stats.currentView),
                g_stats.horPlusActive ? "HOR+" : "4:3",
                g_stats.fps,
                g_stats.activeMsaa, g_cfg.anisotropy, g_stats.triangles);
        } else {
            std::snprintf(buf, sizeof(buf),
                "2D/SW MODE %dx%d | %.0f FPS | PRESS F12 FOR RECOMP MENU",
                winW, winH, g_stats.fps);
        }
        float s = std::max(1.5f, scale * 0.8f);
        float bw = (std::strlen(buf) * 6.0f + 8.0f) * s;
        float bh = 11.0f * s;
        PushRect(8.0f, 8.0f, bw, bh, 0.04f, 0.07f, 0.05f, 0.78f);
        PushText(12.0f, 10.0f + 1.0f * s, s, buf, 0.45f, 0.98f, 0.55f);
    }

    if (g_open) {
        float panelW = 340.0f * scale;
        float rowH   = 15.0f * scale;
        float panelH = (38.0f + kNumItems * 15.0f + 34.0f) * scale;
        float px = (winW - panelW) * 0.5f;
        float py = (winH - panelH) * 0.5f;

        // Dim background + military tactical panel border
        PushRect(0, 0, float(winW), float(winH), 0.0f, 0.0f, 0.0f, 0.45f);
        PushRect(px - 2.0f, py - 2.0f, panelW + 4.0f, panelH + 4.0f, 0.35f, 0.78f, 0.42f, 0.95f);
        PushRect(px, py, panelW, panelH, 0.05f, 0.08f, 0.06f, 0.92f);

        // Header bar
        PushRect(px, py, panelW, 24.0f * scale, 0.10f, 0.22f, 0.12f, 0.95f);
        PushText(px + 10.0f * scale, py + 6.0f * scale, scale * 1.4f,
                 "M1TP2 NATIVE RECOMP [OPENGL 3.3]", 0.75f, 1.0f, 0.75f);

        float listTop = py + 34.0f * scale;
        for (int i = 0; i < kNumItems; ++i) {
            float ry = listTop + i * rowH;
            bool sel = (i == g_selected);
            if (sel) {
                PushRect(px + 6.0f * scale, ry - 2.0f * scale, panelW - 12.0f * scale, rowH - 1.0f * scale,
                         0.16f, 0.38f, 0.20f, 0.85f);
            }
            char label[64], val[64];
            FormatItem(i, label, val);
            float lr = sel ? 1.0f : 0.78f;
            float lg = sel ? 0.95f : 0.82f;
            float lb = sel ? 0.45f : 0.78f;
            PushText(px + 12.0f * scale, ry + 1.5f * scale, scale * 1.05f, label, lr, lg, lb);

            char valBox[80];
            std::snprintf(valBox, sizeof(valBox), sel ? "< %s >" : "  %s", val);
            float vr = sel ? 0.45f : 0.55f;
            float vg = sel ? 1.0f  : 0.90f;
            float vb = sel ? 0.55f : 0.60f;
            PushText(px + 178.0f * scale, ry + 1.5f * scale, scale * 1.05f, valBox, vr, vg, vb);
        }

        // Footer status & hints
        float fy = listTop + kNumItems * rowH + 6.0f * scale;
        PushRect(px + 8.0f * scale, fy - 3.0f * scale, panelW - 16.0f * scale, 1.5f, 0.25f, 0.50f, 0.30f, 0.8f);
        char status[180];
        if (g_stats.inGlide3D) {
            std::snprintf(status, sizeof(status),
                "ACTIVE: %s [%s %dx%d] | MSAA %dX | AF %dX | VFS HITS: %d",
                GlideGetViewName(g_stats.currentView),
                g_stats.horPlusActive ? "TRUE HOR+" : "4:3",
                g_stats.renderW, g_stats.renderH,
                g_stats.activeMsaa, g_cfg.anisotropy, g_stats.vfsHits);
        } else {
            std::snprintf(status, sizeof(status),
                "ACTIVE: 2D MENU %dx%d | GAME CFG: %s | VFS HITS: %d",
                winW, winH,
                mem<uint32_t>(addr::g_GameCfgRenderMode) == 1 ? "GLIDE" :
                (mem<uint32_t>(addr::g_GameCfgRenderMode) == 2 ? "D3D->GLIDE" : "SOFTWARE"),
                g_stats.vfsHits);
        }
        PushText(px + 12.0f * scale, fy + 2.0f * scale, scale * 0.85f, status, 0.65f, 0.88f, 0.70f);
        PushText(px + 12.0f * scale, fy + 13.0f * scale, scale * 0.82f,
                 "ARROWS/CLICK: CHANGE | F12/ESC: CLOSE & SAVE | TAB IN CHASE: LETTERBOX",
                 0.62f, 0.68f, 0.64f);
    }

    FlushUI(winW, winH);
}

} // namespace m1

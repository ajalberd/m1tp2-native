#include "video_sdl.h"
#include "overlay_ui.h"
#include "glide_gl.h"
#include "gunnery.h"
#include "common.h"
#include "gl_loader.h"
#include "patch.h"
#include <windows.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <vector>
#include <cstring>
#include <algorithm>

namespace m1 {

namespace {

SDL_Window*    g_window = nullptr;
SDL_GLContext  g_glCtx  = nullptr;
HWND           g_hwnd   = nullptr;

int g_winW = 1920;
int g_winH = 1080;

int g_modeW = 640;
int g_modeH = 480;
int g_modeBpp = 8;
int g_modePitch = 640;
std::vector<uint8_t> g_primaryRam;
std::vector<uint8_t> g_backRam;
std::vector<uint32_t> g_rgbaUpload;
uint32_t g_paletteRGBA[256] = {};

GLuint g_quadVao = 0;
GLuint g_quadVbo = 0;
GLuint g_2dProg  = 0;
GLuint g_2dTex   = 0;
GLint  g_2dLocTex = -1;

uint64_t g_lastPresentTicks = 0;
uint64_t g_fpsLastTicks = 0;
int      g_fpsFrames = 0;
uint32_t g_mouseBtnMask = 0;

using PushCharFn       = void(__cdecl*)(uint8_t);
using SysCharFn        = void(__cdecl*)(uint32_t);
using SetLastKeyFn     = void(__cdecl*)(uint32_t);
using MouseHandlerFn   = void(__cdecl*)(uint32_t wParam, uint32_t lParam);
using GameShutdownFn   = int(__cdecl*)();
using CrtExitFn        = void(__cdecl*)(int);
using GlideUpdatePalFn = void(__cdecl*)(int start, int end);

static const char* k2DVS = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* k2DFS = R"GLSL(
#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    FragColor = texture(uTex, vUV);
}
)GLSL";

struct KeyMap {
    uint16_t vk;
    uint8_t  atScan;
};

static KeyMap MapSDLKey(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_ESCAPE:     return { VK_ESCAPE, 0x01 };
        case SDL_SCANCODE_1:          return { '1', 0x02 };
        case SDL_SCANCODE_2:          return { '2', 0x03 };
        case SDL_SCANCODE_3:          return { '3', 0x04 };
        case SDL_SCANCODE_4:          return { '4', 0x05 };
        case SDL_SCANCODE_5:          return { '5', 0x06 };
        case SDL_SCANCODE_6:          return { '6', 0x07 };
        case SDL_SCANCODE_7:          return { '7', 0x08 };
        case SDL_SCANCODE_8:          return { '8', 0x09 };
        case SDL_SCANCODE_9:          return { '9', 0x0A };
        case SDL_SCANCODE_0:          return { '0', 0x0B };
        case SDL_SCANCODE_MINUS:      return { VK_OEM_MINUS, 0x0C };
        case SDL_SCANCODE_EQUALS:     return { VK_OEM_PLUS, 0x0D };
        case SDL_SCANCODE_BACKSPACE:  return { VK_BACK, 0x0E };
        case SDL_SCANCODE_TAB:        return { VK_TAB, 0x0F };
        case SDL_SCANCODE_Q:          return { 'Q', 0x10 };
        case SDL_SCANCODE_W:          return { 'W', 0x11 };
        case SDL_SCANCODE_E:          return { 'E', 0x12 };
        case SDL_SCANCODE_R:          return { 'R', 0x13 };
        case SDL_SCANCODE_T:          return { 'T', 0x14 };
        case SDL_SCANCODE_Y:          return { 'Y', 0x15 };
        case SDL_SCANCODE_U:          return { 'U', 0x16 };
        case SDL_SCANCODE_I:          return { 'I', 0x17 };
        case SDL_SCANCODE_O:          return { 'O', 0x18 };
        case SDL_SCANCODE_P:          return { 'P', 0x19 };
        case SDL_SCANCODE_LEFTBRACKET:  return { VK_OEM_4, 0x1A };
        case SDL_SCANCODE_RIGHTBRACKET: return { VK_OEM_6, 0x1B };
        case SDL_SCANCODE_RETURN:     return { VK_RETURN, 0x1C };
        case SDL_SCANCODE_LCTRL:      return { VK_CONTROL, 0x1D };
        case SDL_SCANCODE_RCTRL:      return { VK_CONTROL, 0x1D };
        case SDL_SCANCODE_A:          return { 'A', 0x1E };
        case SDL_SCANCODE_S:          return { 'S', 0x1F };
        case SDL_SCANCODE_D:          return { 'D', 0x20 };
        case SDL_SCANCODE_F:          return { 'F', 0x21 };
        case SDL_SCANCODE_G:          return { 'G', 0x22 };
        case SDL_SCANCODE_H:          return { 'H', 0x23 };
        case SDL_SCANCODE_J:          return { 'J', 0x24 };
        case SDL_SCANCODE_K:          return { 'K', 0x25 };
        case SDL_SCANCODE_L:          return { 'L', 0x26 };
        case SDL_SCANCODE_SEMICOLON:  return { VK_OEM_1, 0x27 };
        case SDL_SCANCODE_APOSTROPHE: return { VK_OEM_7, 0x28 };
        case SDL_SCANCODE_GRAVE:      return { VK_OEM_3, 0x29 };
        case SDL_SCANCODE_LSHIFT:     return { VK_SHIFT, 0x2A };
        case SDL_SCANCODE_BACKSLASH:  return { VK_OEM_5, 0x2B };
        case SDL_SCANCODE_Z:          return { 'Z', 0x2C };
        case SDL_SCANCODE_X:          return { 'X', 0x2D };
        case SDL_SCANCODE_C:          return { 'C', 0x2E };
        case SDL_SCANCODE_V:          return { 'V', 0x2F };
        case SDL_SCANCODE_B:          return { 'B', 0x30 };
        case SDL_SCANCODE_N:          return { 'N', 0x31 };
        case SDL_SCANCODE_M:          return { 'M', 0x32 };
        case SDL_SCANCODE_COMMA:      return { VK_OEM_COMMA, 0x33 };
        case SDL_SCANCODE_PERIOD:     return { VK_OEM_PERIOD, 0x34 };
        case SDL_SCANCODE_SLASH:      return { VK_OEM_2, 0x35 };
        case SDL_SCANCODE_RSHIFT:     return { VK_SHIFT, 0x36 };
        case SDL_SCANCODE_KP_MULTIPLY:return { VK_MULTIPLY, 0x37 };
        case SDL_SCANCODE_LALT:       return { VK_MENU, 0x38 };
        case SDL_SCANCODE_RALT:       return { VK_MENU, 0x38 };
        case SDL_SCANCODE_SPACE:      return { VK_SPACE, 0x39 };
        case SDL_SCANCODE_CAPSLOCK:   return { VK_CAPITAL, 0x3A };
        case SDL_SCANCODE_F1:         return { VK_F1, 0x3B };
        case SDL_SCANCODE_F2:         return { VK_F2, 0x3C };
        case SDL_SCANCODE_F3:         return { VK_F3, 0x3D };
        case SDL_SCANCODE_F4:         return { VK_F4, 0x3E };
        case SDL_SCANCODE_F5:         return { VK_F5, 0x3F };
        case SDL_SCANCODE_F6:         return { VK_F6, 0x40 };
        case SDL_SCANCODE_F7:         return { VK_F7, 0x41 };
        case SDL_SCANCODE_F8:         return { VK_F8, 0x42 };
        case SDL_SCANCODE_F9:         return { VK_F9, 0x43 };
        case SDL_SCANCODE_F10:        return { VK_F10, 0x44 };
        case SDL_SCANCODE_NUMLOCKCLEAR: return { VK_NUMLOCK, 0x45 };
        case SDL_SCANCODE_SCROLLLOCK: return { VK_SCROLL, 0x46 };
        case SDL_SCANCODE_KP_7:       return { VK_NUMPAD7, 0x47 };
        case SDL_SCANCODE_KP_8:       return { VK_NUMPAD8, 0x48 };
        case SDL_SCANCODE_KP_9:       return { VK_NUMPAD9, 0x49 };
        case SDL_SCANCODE_KP_MINUS:   return { VK_SUBTRACT, 0x4A };
        case SDL_SCANCODE_KP_4:       return { VK_NUMPAD4, 0x4B };
        case SDL_SCANCODE_KP_5:       return { VK_NUMPAD5, 0x4C };
        case SDL_SCANCODE_KP_6:       return { VK_NUMPAD6, 0x4D };
        case SDL_SCANCODE_KP_PLUS:    return { VK_ADD, 0x4E };
        case SDL_SCANCODE_KP_1:       return { VK_NUMPAD1, 0x4F };
        case SDL_SCANCODE_KP_2:       return { VK_NUMPAD2, 0x50 };
        case SDL_SCANCODE_KP_3:       return { VK_NUMPAD3, 0x51 };
        case SDL_SCANCODE_KP_0:       return { VK_NUMPAD0, 0x52 };
        case SDL_SCANCODE_KP_PERIOD:  return { VK_DECIMAL, 0x53 };
        case SDL_SCANCODE_F11:        return { VK_F11, 0x57 };
        case SDL_SCANCODE_F12:        return { VK_F12, 0x58 };
        case SDL_SCANCODE_KP_ENTER:   return { VK_RETURN, 0x1C };
        case SDL_SCANCODE_KP_DIVIDE:  return { VK_DIVIDE, 0x35 };
        case SDL_SCANCODE_HOME:       return { VK_HOME, 0x47 };
        case SDL_SCANCODE_UP:         return { VK_UP, 0x48 };
        case SDL_SCANCODE_PAGEUP:     return { VK_PRIOR, 0x49 };
        case SDL_SCANCODE_LEFT:       return { VK_LEFT, 0x4B };
        case SDL_SCANCODE_RIGHT:      return { VK_RIGHT, 0x4D };
        case SDL_SCANCODE_END:        return { VK_END, 0x4F };
        case SDL_SCANCODE_DOWN:       return { VK_DOWN, 0x50 };
        case SDL_SCANCODE_PAGEDOWN:   return { VK_NEXT, 0x51 };
        case SDL_SCANCODE_INSERT:     return { VK_INSERT, 0x52 };
        case SDL_SCANCODE_DELETE:     return { VK_DELETE, 0x53 };
        case SDL_SCANCODE_PAUSE:      return { VK_PAUSE, 0x45 };
        default:                      return { 0, 0 };
    }
}

static bool InitGLResources() {
    if (!gl::LoadFunctions()) return false;

    const float quadVerts[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };
    gl::GenVertexArrays(1, &g_quadVao);
    gl::BindVertexArray(g_quadVao);
    gl::GenBuffers(1, &g_quadVbo);
    gl::BindBuffer(GL_ARRAY_BUFFER, g_quadVbo);
    gl::BufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    gl::BindVertexArray(0);

    g_2dProg = gl::BuildProgram(k2DVS, k2DFS, "2D_Present");
    if (!g_2dProg) return false;
    g_2dLocTex = gl::GetUniformLocation(g_2dProg, "uTex");

    glGenTextures(1, &g_2dTex);
    glBindTexture(GL_TEXTURE_2D, g_2dTex);
    GLint filt = g_cfg.linearFilter2D ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    OverlayInit(g_window, g_glCtx);

    LogPrintf("[GL] Renderer: %s | Version: %s\n",
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    return true;
}

static void MapWindowToGameCoordsFloat(float wx, float wy, float& fx, float& fy, int& gx, int& gy) {
    bool is3D = (mem<uint32_t>(addr::g_RenderMode) == 1);
    int vx, vy, vw, vh;
    if (is3D && GlideIsHorPlusView()) {
        // In Hor+ widescreen views, the 640x480 LFB cursor & clickable HUD live in the centered 4:3 safe zone
        VideoGet43Viewport(vx, vy, vw, vh);
    } else {
        VideoGetViewport(is3D, vx, vy, vw, vh);
    }
    int topY = g_winH - (vy + vh);
    float nx = (vw > 0) ? ((wx - vx) / float(vw)) : 0.0f;
    float ny = (vh > 0) ? ((wy - topY) / float(vh)) : 0.0f;
    nx = std::clamp(nx, 0.0f, 1.0f);
    ny = std::clamp(ny, 0.0f, 1.0f);
    fx = nx * float(g_modeW);
    fy = ny * float(g_modeH);
    gx = std::clamp(int(fx), 0, g_modeW - 1);
    gy = std::clamp(int(fy), 0, g_modeH - 1);
}

static void MapWindowToGameCoords(float wx, float wy, int& gx, int& gy) {
    float fx, fy;
    MapWindowToGameCoordsFloat(wx, wy, fx, fy, gx, gy);
}

static uint8_t* GetActivePrimaryPixels() {
    SurfaceObj* prim = mem<SurfaceObj*>(addr::g_PrimarySurfObj);
    if (prim && prim->pixels) return prim->pixels;
    return g_primaryRam.empty() ? nullptr : g_primaryRam.data();
}

static void UpdatePaletteTable(const uint8_t* rgb768, int start, int count) {
    if (!rgb768) return;
    int end = std::min(256, start + count);
    for (int i = std::max(0, start); i < end; ++i) {
        uint8_t r = rgb768[i * 3 + 0];
        uint8_t g = rgb768[i * 3 + 1];
        uint8_t b = rgb768[i * 3 + 2];
        g_paletteRGBA[i] = 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
    }
}

HWND __cdecl Hook_CreateGameWindow(HINSTANCE /*hInst*/) {
    LogPrintf("[VIDEO] CreateGameWindow -> initializing SDL3 + OpenGL 3.3\n");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK)) {
        LogPrintf("[VIDEO] SDL_Init FAILED: %s\n", SDL_GetError());
        return nullptr;
    }

    // Default to full native desktop resolution if not specified in INI
    int deskW = 1920, deskH = 1080;
    SDL_DisplayID disp = SDL_GetPrimaryDisplay();
    if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(disp)) {
        deskW = dm->w;
        deskH = dm->h;
    }
    g_winW = (g_cfg.windowWidth > 0)  ? g_cfg.windowWidth  : (g_cfg.fullscreen ? deskW : std::min(1600, deskW - 80));
    g_winH = (g_cfg.windowHeight > 0) ? g_cfg.windowHeight : (g_cfg.fullscreen ? deskH : std::min(1000, deskH - 80));

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (g_cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    g_window = SDL_CreateWindow("M1 Tank Platoon II [Native SDL3/OpenGL]", g_winW, g_winH, flags);
    if (!g_window) {
        LogPrintf("[VIDEO] SDL_CreateWindow FAILED: %s\n", SDL_GetError());
        return nullptr;
    }

    g_glCtx = SDL_GL_CreateContext(g_window);
    if (!g_glCtx) {
        LogPrintf("[VIDEO] SDL_GL_CreateContext FAILED: %s\n", SDL_GetError());
        return nullptr;
    }
    SDL_GL_MakeCurrent(g_window, g_glCtx);
    SDL_GL_SetSwapInterval(g_cfg.vsync ? 1 : 0);

    if (!InitGLResources()) {
        LogPrintf("[VIDEO] InitGLResources FAILED\n");
        return nullptr;
    }

    SDL_GetWindowSizeInPixels(g_window, &g_winW, &g_winH);
    SDL_HideCursor();
    SDL_StartTextInput(g_window);

    SDL_PropertiesID props = SDL_GetWindowProperties(g_window);
    g_hwnd = reinterpret_cast<HWND>(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));

    mem<HWND>(addr::g_hWnd) = g_hwnd;
    mem<uint32_t>(addr::g_AppActive) = 1;

    int32_t* rc = reinterpret_cast<int32_t*>(addr::g_ClientRect);
    rc[0] = 0; rc[1] = 0; rc[2] = 640; rc[3] = 480;

    LogPrintf("[VIDEO] SDL3 window created (%dx%d native, fs=%d, hwnd=%p)\n",
              g_winW, g_winH, g_cfg.fullscreen, g_hwnd);
    return g_hwnd;
}

int __cdecl Hook_MsgPump() {
    if (!g_window) return 0;

    SDL_Event ev;
    int processed = 0;

    while (SDL_PollEvent(&ev)) {
        processed = 1;

        if (ev.type == SDL_EVENT_QUIT) {
            LogPrintf("[INPUT] SDL_EVENT_QUIT -> shutting down cleanly\n");
            SaveConfig();
            reinterpret_cast<GameShutdownFn>(addr::GameShutdown)();
            reinterpret_cast<CrtExitFn>(addr::CrtExit)(0);
            return 0;
        }
        if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || ev.type == SDL_EVENT_WINDOW_RESIZED) {
            SDL_GetWindowSizeInPixels(g_window, &g_winW, &g_winH);
            GlideApplySettings();
            continue;
        }
        if (ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            mem<uint32_t>(addr::g_AppActive) = 1;
            continue;
        }
        if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            mem<uint32_t>(addr::g_AppActive) = 0;
            continue;
        }

        // Let the F12 Recomp Settings Overlay intercept events first
        if (OverlayHandleEvent(ev)) {
            continue;
        }

        switch (ev.type) {
            case SDL_EVENT_KEY_DOWN: {
                if (ev.key.scancode == SDL_SCANCODE_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                    g_cfg.fullscreen = !g_cfg.fullscreen;
                    VideoApplyDisplaySettings();
                    SaveConfig();
                    break;
                }
                if (ev.key.scancode == SDL_SCANCODE_F11 && !ev.key.repeat) {
                    g_cfg.linearFilter2D = !g_cfg.linearFilter2D;
                    VideoApplyDisplaySettings();
                    SaveConfig();
                    break;
                }

                KeyMap km = MapSDLKey(ev.key.scancode);
                if (km.vk != 0) {
                    uint32_t lParam = (uint32_t(km.atScan) << 16) | 1u;
                    reinterpret_cast<SetLastKeyFn>(addr::SetLastKey)(km.vk);
                    mem<uint16_t>(addr::g_KeyVkCode) = km.vk;
                    mem<uint32_t>(addr::g_KeyScanCode) = lParam;
                    mem<uint32_t>(addr::g_KeyConsumed) = 0;

                    uint32_t mods = mem<uint32_t>(addr::g_KeyModFlags);
                    if (km.vk == VK_SHIFT)   mods |= 3;
                    if (km.vk == VK_MENU)    mods |= 8;
                    if (km.vk == VK_CONTROL) mods |= 4;
                    mem<uint32_t>(addr::g_KeyModFlags) = mods;

                    if (km.vk == VK_BACK || km.vk == VK_RETURN || km.vk == VK_ESCAPE || km.vk == VK_TAB) {
                        reinterpret_cast<PushCharFn>(addr::PushChar)(static_cast<uint8_t>(km.vk));
                    }
                    if (ev.key.mod & SDL_KMOD_ALT) {
                        if (km.vk == 'F' || km.vk == 'P') {
                            reinterpret_cast<SysCharFn>(addr::SysCharHandler)(km.vk);
                        }
                    }
                }
                break;
            }
            case SDL_EVENT_KEY_UP: {
                KeyMap km = MapSDLKey(ev.key.scancode);
                mem<uint16_t>(addr::g_KeyVkCode) = 0;
                mem<uint32_t>(addr::g_KeyConsumed) = 0;
                uint32_t mods = mem<uint32_t>(addr::g_KeyModFlags);
                if (km.vk == VK_SHIFT)   mods &= ~3u;
                if (km.vk == VK_MENU)    mods &= ~8u;
                if (km.vk == VK_CONTROL) mods &= ~4u;
                mem<uint32_t>(addr::g_KeyModFlags) = mods;
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                const char* s = ev.text.text;
                auto pushChar = reinterpret_cast<PushCharFn>(addr::PushChar);
                while (s && *s) {
                    uint8_t ch = static_cast<uint8_t>(*s++);
                    if (ch >= 32 && ch < 127) pushChar(ch);
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                float fx, fy;
                int gx, gy;
                MapWindowToGameCoordsFloat(ev.motion.x, ev.motion.y, fx, fy, gx, gy);
                GunneryOnMouseMotion(fx, fy, ev.motion.xrel, ev.motion.yrel);
                uint32_t lp = (uint32_t(gy & 0xFFFF) << 16) | uint32_t(gx & 0xFFFF);
                reinterpret_cast<MouseHandlerFn>(addr::MouseHandler)(g_mouseBtnMask, lp);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (ev.button.button == SDL_BUTTON_LEFT)  g_mouseBtnMask |= 1u;
                if (ev.button.button == SDL_BUTTON_RIGHT) g_mouseBtnMask |= 2u;
                int gx, gy;
                MapWindowToGameCoords(ev.button.x, ev.button.y, gx, gy);
                uint32_t lp = (uint32_t(gy & 0xFFFF) << 16) | uint32_t(gx & 0xFFFF);
                reinterpret_cast<MouseHandlerFn>(addr::MouseHandler)(g_mouseBtnMask, lp);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (ev.button.button == SDL_BUTTON_LEFT)  g_mouseBtnMask &= ~1u;
                if (ev.button.button == SDL_BUTTON_RIGHT) g_mouseBtnMask &= ~2u;
                int gx, gy;
                MapWindowToGameCoords(ev.button.x, ev.button.y, gx, gy);
                uint32_t lp = (uint32_t(gy & 0xFFFF) << 16) | uint32_t(gx & 0xFFFF);

                mem<uint32_t>(addr::g_MouseResetFlag) = 0;
                if (mem<uint32_t>(addr::g_MouseLockFlag)) {
                    mem<uint32_t>(addr::g_MouseLockFlag) = 0;
                    mem<uint32_t>(addr::g_MouseLockFlag2) = 0;
                    mem<uint32_t>(addr::g_MouseSavedX) = static_cast<uint32_t>(gx);
                    mem<uint32_t>(addr::g_MouseSavedY) = static_cast<uint32_t>(gy);
                }
                reinterpret_cast<MouseHandlerFn>(addr::MouseHandler)(g_mouseBtnMask, lp);
                break;
            }
            default:
                break;
        }
    }

    if (mem<uint32_t>(addr::g_RenderMode) == 0) {
        uint64_t now = SDL_GetTicks();
        if (now - g_lastPresentTicks >= 16) {
            VideoPresent2DNow();
        }
    }

    if (!processed && mem<uint32_t>(addr::g_RenderMode) == 0) {
        SDL_Delay(1);
    }
    return processed;
}

int __cdecl Hook_ddCreate() { return 0; }

int __cdecl Hook_ddSetMode(int width, int height, int bpp) {
    if (width <= 0 || height <= 0 || bpp <= 0) {
        width = 640; height = 480; bpp = 8;
    }
    g_modeW = width;
    g_modeH = height;
    g_modeBpp = bpp;
    int bytesPerPx = (bpp + 7) / 8;
    g_modePitch = width * bytesPerPx;

    mem<int32_t>(addr::g_ScreenWidth)  = width;
    mem<int32_t>(addr::g_ScreenHeight) = height;
    mem<int32_t>(addr::g_Bpp)          = bpp;
    mem<int32_t>(addr::g_SurfacePitch) = g_modePitch;
    mem<uint32_t>(addr::g_RenderMode)  = 0;
    g_stats.inGlide3D = false;

    size_t bufBytes = static_cast<size_t>(g_modePitch) * static_cast<size_t>(height);
    g_primaryRam.assign(bufBytes, 0);
    g_backRam.assign(bufBytes, 0);
    g_rgbaUpload.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0xFF000000u);

    if (bpp == 8) {
        uint8_t* saved = reinterpret_cast<uint8_t*>(addr::g_SavedPalette768);
        uint8_t* cur   = reinterpret_cast<uint8_t*>(addr::g_Palette768);
        for (int i = 0; i < 256; ++i) {
            saved[i * 3 + 0] = saved[i * 3 + 1] = saved[i * 3 + 2] = static_cast<uint8_t>(i);
            cur[i * 3 + 0]   = cur[i * 3 + 1]   = cur[i * 3 + 2]   = static_cast<uint8_t>(i);
        }
        UpdatePaletteTable(cur, 0, 256);
    }

    int32_t* rc = reinterpret_cast<int32_t*>(addr::g_ClientRect);
    rc[0] = 0; rc[1] = 0; rc[2] = width; rc[3] = height;

    LogPrintf("[VIDEO] ddSetMode(%dx%dx%d) pitch=%d\n", width, height, bpp, g_modePitch);
    return 0;
}

void* __cdecl Hook_ddGetSurfacePtr(int which) {
    mem<int32_t>(addr::g_SurfacePitch) = g_modePitch;
    if (which == 1) return g_primaryRam.data();
    if (which == 2) return g_backRam.data();
    return nullptr;
}

int __cdecl Hook_ddFlip() {
    SurfaceObj* back = mem<SurfaceObj*>(addr::g_BackSurfObj);
    if (back && back->pixels) {
        SurfaceObj* prim = mem<SurfaceObj*>(addr::g_PrimarySurfObj);
        uint8_t* oldPrim = prim ? prim->pixels : nullptr;
        if (prim) prim->pixels = back->pixels;
        VideoPresent2DNow();
        if (prim) prim->pixels = oldPrim;
    } else {
        VideoPresent2DNow();
    }
    return 0;
}

int __cdecl Hook_ddBlit() {
    SurfaceObj* prim = mem<SurfaceObj*>(addr::g_PrimarySurfObj);
    SurfaceObj* back = mem<SurfaceObj*>(addr::g_BackSurfObj);
    if (prim && back && prim->pixels && back->pixels && prim->pixels != back->pixels) {
        std::memcpy(prim->pixels, back->pixels, static_cast<size_t>(g_modePitch) * g_modeH);
    }
    VideoPresent2DNow();
    return 0;
}

int __cdecl Hook_ddBlast() { return Hook_ddBlit(); }
int __cdecl Hook_ddWaitNoop() { return 0; }

int __cdecl Hook_ddWaitVBlank(int /*flag*/) {
    if (mem<uint32_t>(addr::g_RenderMode) == 0) {
        uint64_t now = SDL_GetTicks();
        if (now - g_lastPresentTicks >= 16) {
            VideoPresent2DNow();
        }
    }
    return 0;
}

int __cdecl Hook_ddGetPalette(uint8_t* out768) {
    if (g_modeBpp != 8) return static_cast<int>(0x88760140u);
    if (out768) std::memcpy(out768, reinterpret_cast<const void*>(addr::g_Palette768), 768);
    return 0;
}

int __cdecl Hook_ddSetPalette(const uint8_t* rgb768) {
    if (!rgb768) return 0;
    std::memcpy(reinterpret_cast<void*>(addr::g_Palette768), rgb768, 768);
    UpdatePaletteTable(rgb768, 0, 256);

    if (mem<uint32_t>(addr::g_RenderMode) == 1) {
        reinterpret_cast<GlideUpdatePalFn>(addr::GlideUpdatePalette)(-1, 255);
        return 0;
    }
    if (mem<uint32_t>(addr::g_RenderMode) == 0) {
        VideoPresent2DNow();
    }
    return 0;
}

int __cdecl Hook_ddSetPaletteRange(const uint8_t* rgb768, short start, short count) {
    if (!rgb768) return 0;
    std::memcpy(reinterpret_cast<void*>(addr::g_Palette768), rgb768, 768);
    UpdatePaletteTable(rgb768, start, count);
    if (mem<uint32_t>(addr::g_RenderMode) == 0) {
        VideoPresent2DNow();
    }
    return 0;
}

void __cdecl Hook_ddReleaseSurfaces() {}
void __cdecl Hook_ddReleaseAll() {}

void __cdecl Hook_ddShutdownAll() {
    LogPrintf("[VIDEO] ddShutdownAll called\n");
    VideoSDLShutdown();
}

uint32_t __cdecl Hook_SmackerGetPixelFmt(void* /*lpDDS*/) {
    return (g_modeBpp == 8) ? 0u : 0xC0000000u;
}

void __cdecl Hook_IntroSmacker(const char* path, int x, int y, int flags) {
    if (g_cfg.skipIntro) {
        LogPrintf("[VIDEO] SkipIntro: skipping %s\n", path ? path : "(null)");
        return;
    }
    using PlaySmkFn = void(__cdecl*)(const char*, int, int, int);
    reinterpret_cast<PlaySmkFn>(addr::PlaySmacker)(path, x, y, flags);
}

// High-precision 10 ms tick counter replacing M1TP2's GetTickCount() / 10 (0x438CB0).
// Eliminates Windows' 15.625 ms GetTickCount quantum jitter and yields cleanly during
// the main loop's frame-wait spin at 0x41E372 while keeping simulation speed 100% 1x real-time.
bool          g_qpcInited     = false;
uint64_t      g_qpcFreq       = 10000000ULL;
LARGE_INTEGER g_qpcBase       = {};
uint32_t      g_tickBase      = 0;
uint32_t      g_lastTickRet   = 0;
int           g_sameTickSpins = 0;

uint32_t __cdecl Hook_GetTicks10ms() {
    if (!g_qpcInited) {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        if (freq.QuadPart > 0) g_qpcFreq = static_cast<uint64_t>(freq.QuadPart);
        QueryPerformanceCounter(&g_qpcBase);
        g_tickBase = GetTickCount() / 10u;
        g_qpcInited = true;
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    uint64_t deltaCounts = (now.QuadPart >= g_qpcBase.QuadPart)
        ? static_cast<uint64_t>(now.QuadPart - g_qpcBase.QuadPart)
        : 0ULL;
    uint64_t secs = deltaCounts / g_qpcFreq;
    uint64_t rem  = deltaCounts % g_qpcFreq;
    uint64_t ticks64 = secs * 100ULL + (rem * 100ULL) / g_qpcFreq;
    uint32_t ret = g_tickBase + static_cast<uint32_t>(ticks64);

    if (ret == g_lastTickRet) {
        if (++g_sameTickSpins >= 4) {
            uint64_t remUs      = (rem * 1000000ULL) / g_qpcFreq;
            uint64_t usIntoTick = remUs % 10000ULL;
            uint64_t usLeft     = 10000ULL - usIntoTick;
            if (g_window && usLeft > 1500ULL) {
                SDL_DelayNS(1000000ULL);
            } else {
                SwitchToThread();
            }
        }
    } else {
        g_lastTickRet = ret;
        g_sameTickSpins = 0;
    }
    return ret;
}

} // namespace

void VideoApplyFrameRateCap() {
    const uint8_t* ins = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(addr::PatchFrameCapImm - 2));
    if (ins[0] == 0x83 && ins[1] == 0xF8 && ins[3] == 0x7E) {
        // 0x00 => `cmp eax, 0; jle` (>= 10ms = 100 FPS max; 72 FPS with 144Hz VSync)
        // 0x01 => `cmp eax, 1; jle` (>= 20ms = stock 50 FPS max)
        uint8_t imm = g_cfg.unlock100Fps ? 0x00u : 0x01u;
        WriteBytes(addr::PatchFrameCapImm, &imm, 1);
        LogPrintf("[VIDEO] Frame rate cap: %s (0x41E386=0x%02X, QPC 10ms timer active)\n",
                  g_cfg.unlock100Fps ? "100 FPS (Hi-Res)" : "50 FPS (Stock Cap)", imm);
    } else {
        LogPrintf("[VIDEO] Frame cap signature mismatch at 0x41E384 (%02X %02X %02X %02X), skipping patch\n",
                  ins[0], ins[1], ins[2], ins[3]);
    }
}

SDL_Window* VideoGetWindow() { return g_window; }

void VideoGetWindowSize(int& w, int& h) {
    w = g_winW;
    h = g_winH;
}

void VideoApplyDisplaySettings() {
    if (!g_window) return;
    SDL_SetWindowFullscreen(g_window, g_cfg.fullscreen);
    SDL_GL_SetSwapInterval(g_cfg.vsync ? 1 : 0);
    SDL_GetWindowSizeInPixels(g_window, &g_winW, &g_winH);

    if (g_2dTex) {
        GLint filt = g_cfg.linearFilter2D ? GL_LINEAR : GL_NEAREST;
        glBindTexture(GL_TEXTURE_2D, g_2dTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    GlideApplySettings();
    LogPrintf("[VIDEO] Display applied: %dx%d fs=%d vsync=%d menuAspect=%d 3dAspect=%d\n",
              g_winW, g_winH, g_cfg.fullscreen, g_cfg.vsync, g_cfg.menuAspectMode, g_cfg.aspectMode);
}

static void VideoGetAspectViewport(float targetAspect, int& vx, int& vy, int& vw, int& vh) {
    if (g_winW <= 0 || g_winH <= 0) {
        vx = vy = 0; vw = 640; vh = 480;
        return;
    }
    float winAspect = float(g_winW) / float(g_winH);
    if (winAspect > targetAspect) {
        vh = g_winH;
        vw = int(g_winH * targetAspect + 0.5f);
        vx = (g_winW - vw) / 2;
        vy = 0;
    } else {
        vw = g_winW;
        vh = int(g_winW / targetAspect + 0.5f);
        vx = 0;
        vy = (g_winH - vh) / 2;
    }
}

void VideoGet43Viewport(int& vx, int& vy, int& vw, int& vh) {
    VideoGetAspectViewport(4.0f / 3.0f, vx, vy, vw, vh);
}

void VideoGetViewport(bool is3D, int& vx, int& vy, int& vw, int& vh) {
    if (g_winW <= 0 || g_winH <= 0) {
        vx = vy = 0; vw = 640; vh = 480;
        return;
    }

    if (is3D) {
        // Dedicated Tactical Map / Battleplanner aspect when in UberMapScreen (view == 2):
        // The planner map is natively a 640x480 (4:3) UI with a 400x400 1:1 square map pane.
        // Auto-native (default) automatically preserves crisp 4:3 with zero stretch on 16:9 / 21:9 / 32:9.
        if (mem<int32_t>(addr::g_CurrentView) == 2) {
            int mapMode = g_cfg.mapAspectMode;
            if (mapMode == MAP_ASPECT_STRETCH) {
                vx = 0; vy = 0; vw = g_winW; vh = g_winH;
                return;
            }
            if (mapMode == MAP_ASPECT_16_9) {
                VideoGetAspectViewport(16.0f / 9.0f, vx, vy, vw, vh);
                return;
            }
            if (mapMode == MAP_ASPECT_16_10) {
                VideoGetAspectViewport(16.0f / 10.0f, vx, vy, vw, vh);
                return;
            }
            VideoGet43Viewport(vx, vy, vw, vh);
            return;
        }
        if (g_cfg.aspectMode == ASPECT3D_STRETCH || GlideIsHorPlusView()) {
            vx = 0; vy = 0; vw = g_winW; vh = g_winH;
            return;
        }
        VideoGet43Viewport(vx, vy, vw, vh);
        return;
    }

    int mode = g_cfg.menuAspectMode;
    if (mode == ASPECT_FILL) {
        vx = 0; vy = 0; vw = g_winW; vh = g_winH;
        return;
    }
    if (mode == ASPECT_CROP) {
        float targetAspect = 4.0f / 3.0f;
        float winAspect = float(g_winW) / float(g_winH);
        if (winAspect > targetAspect) {
            vw = g_winW;
            vh = int(g_winW / targetAspect + 0.5f);
            vx = 0;
            vy = (g_winH - vh) / 2;
        } else {
            vh = g_winH;
            vw = int(g_winH * targetAspect + 0.5f);
            vx = (g_winW - vw) / 2;
            vy = 0;
        }
        return;
    }

    float targetAspect = (mode == ASPECT_16_10) ? (16.0f / 10.0f) : (4.0f / 3.0f);
    float winAspect = float(g_winW) / float(g_winH);
    if (winAspect > targetAspect) {
        vh = g_winH;
        vw = int(g_winH * targetAspect + 0.5f);
        vx = (g_winW - vw) / 2;
        vy = 0;
    } else {
        vw = g_winW;
        vh = int(g_winW / targetAspect + 0.5f);
        vx = 0;
        vy = (g_winH - vh) / 2;
    }
}

void VideoDrawFullscreenQuad() {
    gl::BindVertexArray(g_quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
}

void VideoPresentSwap() {
    if (!g_window) return;
    GunnerySyncRelativeMouse(g_window, OverlayIsOpen());
    OverlayRender(g_winW, g_winH);
    SDL_GL_SwapWindow(g_window);

    uint64_t now = SDL_GetTicks();
    g_lastPresentTicks = now;
    g_fpsFrames++;
    if (now - g_fpsLastTicks >= 500) {
        g_stats.fps = g_fpsFrames * 1000.0f / float(now - g_fpsLastTicks);
        g_fpsFrames = 0;
        g_fpsLastTicks = now;
    }
}

void VideoPresent2DNow() {
    if (!g_window || !g_glCtx || g_modeW <= 0 || g_modeH <= 0) return;
    uint8_t* src = GetActivePrimaryPixels();
    if (!src) return;

    const size_t totalPx = static_cast<size_t>(g_modeW) * static_cast<size_t>(g_modeH);
    if (g_rgbaUpload.size() < totalPx) g_rgbaUpload.resize(totalPx);

    uint32_t* dst = g_rgbaUpload.data();
    if (g_modeBpp == 8) {
        for (int y = 0; y < g_modeH; ++y) {
            const uint8_t* row = src + y * g_modePitch;
            uint32_t* out = dst + y * g_modeW;
            for (int x = 0; x < g_modeW; ++x) out[x] = g_paletteRGBA[row[x]];
        }
    } else {
        for (int y = 0; y < g_modeH; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(src + y * g_modePitch);
            uint32_t* out = dst + y * g_modeW;
            for (int x = 0; x < g_modeW; ++x) {
                uint16_t p = row[x];
                uint8_t r = static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
                uint8_t g = static_cast<uint8_t>(((p >> 5)  & 0x3F) * 255 / 63);
                uint8_t b = static_cast<uint8_t>(( p        & 0x1F) * 255 / 31);
                out[x] = 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
            }
        }
    }

    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, g_winW, g_winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int vx, vy, vw, vh;
    VideoGetViewport(false, vx, vy, vw, vh);
    glViewport(vx, vy, vw, vh);

    gl::UseProgram(g_2dProg);
    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_2dTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_modeW, g_modeH, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
    gl::Uniform1i(g_2dLocTex, 0);

    VideoDrawFullscreenQuad();
    VideoPresentSwap();
}

void VideoSDLInstallHooks() {
    InstallJump(addr::CreateGameWindow,   reinterpret_cast<void*>(&Hook_CreateGameWindow));
    InstallJump(addr::MsgPump,            reinterpret_cast<void*>(&Hook_MsgPump));

    InstallJump(addr::ddRestore,          reinterpret_cast<void*>(&Hook_ddWaitNoop));
    InstallJump(addr::ddGetSurfacePtr,    reinterpret_cast<void*>(&Hook_ddGetSurfacePtr));
    InstallJump(addr::ddFlipToGDISurface, reinterpret_cast<void*>(&Hook_ddWaitNoop));
    InstallJump(addr::ddFlip,             reinterpret_cast<void*>(&Hook_ddFlip));
    InstallJump(addr::ddBlit,             reinterpret_cast<void*>(&Hook_ddBlit));
    InstallJump(addr::ddBlast,            reinterpret_cast<void*>(&Hook_ddBlast));
    InstallJump(addr::ddWaitBltStatus,    reinterpret_cast<void*>(&Hook_ddWaitNoop));
    InstallJump(addr::ddWaitVBlank,       reinterpret_cast<void*>(&Hook_ddWaitVBlank));
    InstallJump(addr::ddCreate,           reinterpret_cast<void*>(&Hook_ddCreate));
    InstallJump(addr::ddSetMode,          reinterpret_cast<void*>(&Hook_ddSetMode));
    InstallJump(addr::ddGetPalette,       reinterpret_cast<void*>(&Hook_ddGetPalette));
    InstallJump(addr::ddSetPalette,       reinterpret_cast<void*>(&Hook_ddSetPalette));
    InstallJump(addr::ddSetPaletteRange,  reinterpret_cast<void*>(&Hook_ddSetPaletteRange));
    InstallJump(addr::ddShutdownAll,      reinterpret_cast<void*>(&Hook_ddShutdownAll));
    InstallJump(addr::ddReleaseSurfaces,  reinterpret_cast<void*>(&Hook_ddReleaseSurfaces));
    InstallJump(addr::ddReleaseAll,       reinterpret_cast<void*>(&Hook_ddReleaseAll));

    InstallJump(addr::SmackerGetPixelFmt, reinterpret_cast<void*>(&Hook_SmackerGetPixelFmt));
    InstallCall(addr::IntroCallMps,       reinterpret_cast<void*>(&Hook_IntroSmacker));
    InstallCall(addr::IntroCallMain,      reinterpret_cast<void*>(&Hook_IntroSmacker));
    InstallCall(addr::IntroCallCredits,   reinterpret_cast<void*>(&Hook_IntroSmacker));
    InstallJump(addr::GetTicks10ms,       reinterpret_cast<void*>(&Hook_GetTicks10ms));
    VideoApplyFrameRateCap();
    LogPrintf("[VIDEO] SDL3/OpenGL 2D + Window + Input + Smacker + SkipIntro + Hi-Res Timer hooks installed\n");
}

void VideoSDLShutdown() {
    if (g_glCtx) {
        OverlayShutdown();
        if (g_2dTex)  { glDeleteTextures(1, &g_2dTex); g_2dTex = 0; }
        if (g_2dProg) { gl::DeleteProgram(g_2dProg); g_2dProg = 0; }
        if (g_quadVbo){ gl::DeleteBuffers(1, &g_quadVbo); g_quadVbo = 0; }
        if (g_quadVao){ gl::DeleteVertexArrays(1, &g_quadVao); g_quadVao = 0; }
        SDL_GL_DestroyContext(g_glCtx);
        g_glCtx = nullptr;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}

} // namespace m1

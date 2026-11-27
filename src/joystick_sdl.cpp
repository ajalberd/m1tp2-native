#include "joystick_sdl.h"
#include "common.h"
#include "patch.h"
#include <windows.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace m1 {

namespace {

// Minimal WinMM JOYCAPSA / JOYINFOEX layouts (avoid needing mmsystem.h / winmm link)
#pragma pack(push, 1)
struct M1JoyCapsA {
    uint16_t wMid;
    uint16_t wPid;
    char     szPname[32];
    uint32_t wXmin, wXmax;
    uint32_t wYmin, wYmax;
    uint32_t wZmin, wZmax;
    uint32_t wNumButtons;
    uint32_t wPeriodMin, wPeriodMax;
    uint32_t wRmin, wRmax;
    uint32_t wUmin, wUmax;
    uint32_t wVmin, wVmax;
    uint32_t wCaps;
    uint32_t wMaxAxes;
    uint32_t wNumAxes;
    uint32_t wMaxButtons;
    char     szRegKey[32];
    char     szOEMVxD[260];
};
#pragma pack(pop)
static_assert(sizeof(M1JoyCapsA) == 0x194, "JOYCAPSA size must be 0x194");

struct M1JoyInfoEx {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwXpos;
    uint32_t dwYpos;
    uint32_t dwZpos;
    uint32_t dwRpos;
    uint32_t dwUpos;
    uint32_t dwVpos;
    uint32_t dwButtons;
    uint32_t dwButtonNumber;
    uint32_t dwPOV;
    uint32_t dwReserved1;
    uint32_t dwReserved2;
};
static_assert(sizeof(M1JoyInfoEx) == 0x34, "JOYINFOEX size must be 0x34");

constexpr uint32_t kJoyErrNoError   = 0u;
constexpr uint32_t kJoyErrUnplugged = 167u; // JOYERR_UNPLUGGED
constexpr uint32_t kJoyCapsHasZ     = 0x01u;
constexpr uint32_t kJoyCapsHasR     = 0x02u;
constexpr uint32_t kJoyCapsHasPOV   = 0x10u;
constexpr uint32_t kJoyCapsPOV4Dir  = 0x20u;

SDL_Joystick* g_joy = nullptr;
std::string   g_joyActiveName;
bool          g_sdlJoyInited = false;
uint64_t      g_lastRescanMs = 0;

static void EnsureSdlJoySubsys() {
    if (!g_sdlJoyInited) {
        if (SDL_WasInit(SDL_INIT_JOYSTICK) || SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
            g_sdlJoyInited = true;
        }
    }
}

static void CloseCurrentJoystick() {
    if (g_joy) {
        SDL_CloseJoystick(g_joy);
        g_joy = nullptr;
    }
    g_joyActiveName.clear();
}

static void EnsureJoystickOpen() {
    if (g_cfg.disableJoystick) {
        if (g_joy) CloseCurrentJoystick();
        return;
    }
    EnsureSdlJoySubsys();
    if (!g_sdlJoyInited) return;

    if (g_joy && !SDL_JoystickConnected(g_joy)) {
        CloseCurrentJoystick();
    }
    if (g_joy) return;

    uint64_t now = SDL_GetTicks();
    if (now - g_lastRescanMs < 500) return;
    g_lastRescanMs = now;

    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (!ids || count <= 0) {
        if (ids) SDL_free(ids);
        return;
    }

    SDL_JoystickID chosen = 0;
    const bool wantAuto = (g_cfg.joyDeviceName[0] == '\0' || _stricmp(g_cfg.joyDeviceName, "Auto") == 0);
    if (!wantAuto) {
        for (int i = 0; i < count; ++i) {
            const char* nm = SDL_GetJoystickNameForID(ids[i]);
            if (nm && _stricmp(nm, g_cfg.joyDeviceName) == 0) {
                chosen = ids[i];
                break;
            }
        }
    }
    if (chosen == 0) {
        chosen = ids[0];
    }
    SDL_free(ids);

    g_joy = SDL_OpenJoystick(chosen);
    if (g_joy) {
        const char* nm = SDL_GetJoystickName(g_joy);
        g_joyActiveName = nm ? nm : "SDL3 Joystick";
        LogPrintf("[INPUT] SDL3 Joystick opened: \"%s\" (axes=%d, btns=%d, hats=%d)\n",
                  g_joyActiveName.c_str(),
                  SDL_GetNumJoystickAxes(g_joy),
                  SDL_GetNumJoystickButtons(g_joy),
                  SDL_GetNumJoystickHats(g_joy));
    }
}

static uint32_t ReadMappedAxisU16(int axisIdx, bool invert, float deadzone) {
    if (!g_joy || axisIdx < 0 || axisIdx >= SDL_GetNumJoystickAxes(g_joy)) {
        return 32768u;
    }
    int16_t raw = SDL_GetJoystickAxis(g_joy, axisIdx);
    float norm = (raw >= 0) ? (raw / 32767.0f) : (raw / 32768.0f);
    if (invert) norm = -norm;

    float dz = std::clamp(deadzone, 0.0f, 0.45f);
    float a = std::fabs(norm);
    if (a <= dz) {
        norm = 0.0f;
    } else if (dz > 0.001f) {
        float scaled = (a - dz) / (1.0f - dz);
        norm = (norm >= 0.0f) ? scaled : -scaled;
    }
    norm = std::clamp(norm, -1.0f, 1.0f);
    int val = static_cast<int>(std::lround((norm * 0.5f + 0.5f) * 65535.0f));
    return static_cast<uint32_t>(std::clamp(val, 0, 65535));
}

static uint32_t ReadHatPov() {
    if (!g_joy || SDL_GetNumJoystickHats(g_joy) <= 0) return 0xFFFFu;
    uint8_t hat = SDL_GetJoystickHat(g_joy, 0);
    switch (hat) {
        case SDL_HAT_UP:        return 0u;
        case SDL_HAT_RIGHTUP:   return 4500u;
        case SDL_HAT_RIGHT:     return 9000u;
        case SDL_HAT_RIGHTDOWN: return 13500u;
        case SDL_HAT_DOWN:      return 18000u;
        case SDL_HAT_LEFTDOWN:  return 22500u;
        case SDL_HAT_LEFT:      return 27000u;
        case SDL_HAT_LEFTUP:    return 31500u;
        default:                return 0xFFFFu;
    }
}

uint32_t __stdcall Hook_joyGetDevCapsA(uintptr_t uJoyID, void* pjcVoid, uint32_t cbjc) {
    if (g_cfg.disableJoystick || uJoyID != 0 || !pjcVoid || cbjc < sizeof(M1JoyCapsA)) {
        return kJoyErrUnplugged;
    }
    EnsureJoystickOpen();
    if (!g_joy) return kJoyErrUnplugged;

    auto* caps = reinterpret_cast<M1JoyCapsA*>(pjcVoid);
    std::memset(caps, 0, sizeof(M1JoyCapsA));
    std::strncpy(caps->szPname, g_joyActiveName.c_str(), sizeof(caps->szPname) - 1);
    caps->wXmin = 0; caps->wXmax = 65535;
    caps->wYmin = 0; caps->wYmax = 65535;
    caps->wZmin = 0; caps->wZmax = 65535;
    caps->wRmin = 0; caps->wRmax = 65535;
    caps->wUmin = 0; caps->wUmax = 65535;
    caps->wVmin = 0; caps->wVmax = 65535;
    caps->wPeriodMin = 10;
    caps->wPeriodMax = 1000;
    caps->wMaxButtons = 32;
    caps->wNumButtons = static_cast<uint32_t>(std::clamp(SDL_GetNumJoystickButtons(g_joy), 4, 32));
    caps->wMaxAxes = 6;
    caps->wNumAxes = static_cast<uint32_t>(std::clamp(SDL_GetNumJoystickAxes(g_joy), 2, 6));

    uint32_t wCaps = 0;
    int numAxes = SDL_GetNumJoystickAxes(g_joy);
    if (g_cfg.joyAxisZ >= 0 && g_cfg.joyAxisZ < numAxes) wCaps |= kJoyCapsHasZ;
    if (g_cfg.joyAxisR >= 0 && g_cfg.joyAxisR < numAxes) wCaps |= kJoyCapsHasR;
    if (SDL_GetNumJoystickHats(g_joy) > 0) wCaps |= (kJoyCapsHasPOV | kJoyCapsPOV4Dir);
    caps->wCaps = wCaps;
    return kJoyErrNoError;
}

uint32_t __stdcall Hook_joyGetPosEx(uint32_t uJoyID, void* pjiVoid) {
    if (g_cfg.disableJoystick || uJoyID != 0 || !pjiVoid) {
        return kJoyErrUnplugged;
    }
    EnsureJoystickOpen();
    if (!g_joy) return kJoyErrUnplugged;

    SDL_UpdateJoysticks();
    if (!SDL_JoystickConnected(g_joy)) {
        CloseCurrentJoystick();
        return kJoyErrUnplugged;
    }

    auto* ji = reinterpret_cast<M1JoyInfoEx*>(pjiVoid);
    ji->dwXpos = ReadMappedAxisU16(g_cfg.joyAxisX, g_cfg.joyInvertX, g_cfg.joyDeadzone);
    ji->dwYpos = ReadMappedAxisU16(g_cfg.joyAxisY, g_cfg.joyInvertY, g_cfg.joyDeadzone);
    ji->dwZpos = ReadMappedAxisU16(g_cfg.joyAxisZ, g_cfg.joyInvertZ, g_cfg.joyDeadzone);
    ji->dwRpos = ReadMappedAxisU16(g_cfg.joyAxisR, g_cfg.joyInvertR, g_cfg.joyDeadzone);
    ji->dwUpos = 32768u;
    ji->dwVpos = 32768u;
    ji->dwPOV  = ReadHatPov();

    uint32_t btns = 0;
    uint32_t btnCount = 0;
    int numBtns = SDL_GetNumJoystickButtons(g_joy);
    const int mapBtns[4] = { g_cfg.joyBtn1, g_cfg.joyBtn2, g_cfg.joyBtn3, g_cfg.joyBtn4 };
    for (int i = 0; i < 4; ++i) {
        if (mapBtns[i] >= 0 && mapBtns[i] < numBtns && SDL_GetJoystickButton(g_joy, mapBtns[i])) {
            btns |= (1u << i);
            btnCount++;
        }
    }
    // Pass through buttons 5..32 directly if not already bound
    for (int b = 4; b < std::min(numBtns, 32); ++b) {
        if (SDL_GetJoystickButton(g_joy, b)) {
            btns |= (1u << b);
            btnCount++;
        }
    }
    ji->dwButtons = btns;
    ji->dwButtonNumber = btnCount;
    return kJoyErrNoError;
}

} // namespace

void JoystickSDLOnConfigChanged() {
    CloseCurrentJoystick();
    g_lastRescanMs = 0;
    // Reset M1TP2's joystick probe state so it re-queries joyGetDevCapsA immediately
    mem<int32_t>(addr::g_JoyInitState) = 0;
}

const char* JoystickSDLGetActiveName() {
    if (g_cfg.disableJoystick) return "Disabled";
    return g_joyActiveName.empty() ? "None detected" : g_joyActiveName.c_str();
}

void JoystickSDLInstallHooks() {
    uintptr_t pPos  = reinterpret_cast<uintptr_t>(&Hook_joyGetPosEx);
    uintptr_t pCaps = reinterpret_cast<uintptr_t>(&Hook_joyGetDevCapsA);
    WriteBytes(addr::IatJoyGetPosEx,    &pPos,  sizeof(pPos));
    WriteBytes(addr::IatJoyGetDevCapsA, &pCaps, sizeof(pCaps));
    LogPrintf("[INPUT] SDL3 Joystick/HOTAS backend hooked onto WinMM IAT (disableJoystick=%d, device=\"%s\")\n",
              g_cfg.disableJoystick ? 1 : 0, g_cfg.joyDeviceName);
}

void JoystickSDLShutdown() {
    CloseCurrentJoystick();
}

} // namespace m1

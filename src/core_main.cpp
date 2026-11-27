#include "common.h"
#include "heap_guard.h"
#include "vfs_mod.h"
#include "campaign_hud.h"
#include "gunnery.h"
#include "audio_mixer.h"
#include "joystick_sdl.h"
#include "video_sdl.h"
#include "glide_gl.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace m1 {

Config     g_cfg;
FrameStats g_stats;

namespace {
FILE* g_logFile = nullptr;
CRITICAL_SECTION g_logLock;
bool g_logInit = false;

static bool FormatAddressModule(uintptr_t addr, char* out, size_t cap) {
    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &hMod) && hMod) {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(hMod, path, MAX_PATH) > 0) {
            const char* base = std::strrchr(path, '\\');
            base = base ? (base + 1) : path;
            uintptr_t rva = addr - reinterpret_cast<uintptr_t>(hMod);
            std::snprintf(out, cap, "%s+0x%08lX", base, static_cast<unsigned long>(rva));
            return true;
        }
    }
    std::snprintf(out, cap, "0x%08lX", static_cast<unsigned long>(addr));
    return false;
}

static bool IsReadableRange(uintptr_t addr, size_t len) {
    if (addr < 0x10000u) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD p = mbi.Protect & 0xFFu;
    if (p == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD)) return false;
    uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return (addr + len <= end);
}

static void WriteCrashReport(FILE* out, EXCEPTION_POINTERS* ep) {
    if (!out || !ep || !ep->ExceptionRecord || !ep->ContextRecord) return;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    const auto* ctx = ep->ContextRecord;
    char eipLoc[160];
    FormatAddressModule(ctx->Eip, eipLoc, sizeof(eipLoc));

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(out, "\n!!! EXCEPTION 0x%08lX at %04u-%02u-%02u %02u:%02u:%02u EIP=0x%08lX (%s) !!!\n",
                 code, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 ctx->Eip, eipLoc);
    std::fprintf(out, "  EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX\n",
                 ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx);
    std::fprintf(out, "  ESI=0x%08lX EDI=0x%08lX EBP=0x%08lX ESP=0x%08lX\n",
                 ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        std::fprintf(out, "  AccessViolation: %s address 0x%08lX\n",
                     ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                     static_cast<unsigned long>(ep->ExceptionRecord->ExceptionInformation[1]));
    }
    std::fprintf(out, "  State: inGlide3D=%d view=%d render=%dx%d msaa=%dx tris=%d cachedTex=%d\n",
                 g_stats.inGlide3D ? 1 : 0, g_stats.currentView,
                 g_stats.renderW, g_stats.renderH, g_stats.activeMsaa,
                 g_stats.triangles, g_stats.cachedTex);

    std::fprintf(out, "  Stack Trace (EBP chain):\n");
    uintptr_t frame = ctx->Ebp;
    for (int i = 0; i < 16 && IsReadableRange(frame, 8); ++i) {
        uintptr_t nextFrame = *reinterpret_cast<const uint32_t*>(frame);
        uintptr_t retAddr   = *reinterpret_cast<const uint32_t*>(frame + 4);
        if (retAddr < 0x10000u) break;
        char loc[160];
        FormatAddressModule(retAddr, loc, sizeof(loc));
        std::fprintf(out, "    #%02d 0x%08lX (%s)\n", i, static_cast<unsigned long>(retAddr), loc);
        if (nextFrame <= frame || nextFrame - frame > 0x100000u) break;
        frame = nextFrame;
    }

    std::fprintf(out, "  Stack Code Pointers (ESP scan):\n");
    int found = 0;
    for (int i = 0; i < 64 && found < 12; ++i) {
        uintptr_t slot = static_cast<uintptr_t>(ctx->Esp) + static_cast<uintptr_t>(i) * 4u;
        if (!IsReadableRange(slot, 4)) break;
        uintptr_t val = *reinterpret_cast<const uint32_t*>(slot);
        if (val < 0x400000u) continue;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(val), &mbi, sizeof(mbi)) &&
            mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            char loc[160];
            FormatAddressModule(val, loc, sizeof(loc));
            std::fprintf(out, "    [ESP+0x%02X] 0x%08lX (%s)\n", i * 4, static_cast<unsigned long>(val), loc);
            found++;
        }
    }
    std::fflush(out);
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0xE06D7363 || code == DBG_PRINTEXCEPTION_C) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_logInit && g_logFile) {
        EnterCriticalSection(&g_logLock);
        WriteCrashReport(g_logFile, ep);
        LeaveCriticalSection(&g_logLock);
    }
    if (FILE* cf = std::fopen("m1tp2_crash.log", "a")) {
        WriteCrashReport(cf, ep);
        std::fclose(cf);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void LogInit() {
    InitializeCriticalSection(&g_logLock);
    g_logInit = true;
    CopyFileA("m1tp2_recomp.log", "m1tp2_recomp.prev.log", FALSE);
    g_logFile = std::fopen("m1tp2_recomp.log", "w");
    LogPrintf("=== M1TP2 Native Recomp Core (SDL3 + OpenGL 3.3) ===\n");
}

void LogShutdown() {
    if (g_logInit) {
        LogPrintf("=== M1TP2 Native Recomp Shutdown ===\n");
        if (g_logFile) {
            std::fclose(g_logFile);
            g_logFile = nullptr;
        }
        DeleteCriticalSection(&g_logLock);
        g_logInit = false;
    }
}

void LogPrintf(const char* fmt, ...) {
    if (!g_logInit) return;
    EnterCriticalSection(&g_logLock);
    if (g_logFile) {
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(g_logFile, fmt, ap);
        va_end(ap);
        std::fflush(g_logFile);
    }
    LeaveCriticalSection(&g_logLock);
}

void LoadConfig() {
    FILE* f = std::fopen("m1tp2_recomp.ini", "r");
    if (!f) {
        SaveConfig();
        return;
    }
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == ';') continue;
        char key[128], val[128];
        if (std::sscanf(line, " %127[^= \t] = %127[^\r\n]", key, val) == 2) {
            size_t vlen = std::strlen(val);
            while (vlen > 0 && (val[vlen - 1] == ' ' || val[vlen - 1] == '\t')) val[--vlen] = '\0';
            if (_stricmp(key, "WindowWidth") == 0)     g_cfg.windowWidth     = std::atoi(val);
            if (_stricmp(key, "WindowHeight") == 0)    g_cfg.windowHeight    = std::atoi(val);
            if (_stricmp(key, "Fullscreen") == 0)      g_cfg.fullscreen      = (std::atoi(val) != 0);
            if (_stricmp(key, "VSync") == 0)           g_cfg.vsync           = (std::atoi(val) != 0);
            if (_stricmp(key, "Unlock100Fps") == 0)    g_cfg.unlock100Fps    = (std::atoi(val) != 0);
            if (_stricmp(key, "MenuAspectMode") == 0)  g_cfg.menuAspectMode  = std::atoi(val);
            if (_stricmp(key, "AspectMode") == 0)      g_cfg.aspectMode      = std::atoi(val);
            if (_stricmp(key, "MapAspectMode") == 0) {
                int v = std::atoi(val);
                g_cfg.mapAspectMode = (v == MAP_ASPECT_4_3_LEGACY) ? MAP_ASPECT_AUTO_NATIVE : v;
            }
            if (_stricmp(key, "MSAA") == 0)            g_cfg.msaaSamples     = std::atoi(val);
            if (_stricmp(key, "Anisotropy") == 0)      g_cfg.anisotropy      = std::atoi(val);
            if (_stricmp(key, "TexFilter3D") == 0)     g_cfg.texFilter3D     = std::atoi(val);
            if (_stricmp(key, "LinearFilter") == 0 ||
                _stricmp(key, "LinearFilter2D") == 0)  g_cfg.linearFilter2D  = (std::atoi(val) != 0);
            if (_stricmp(key, "LinearFilterLfb") == 0) g_cfg.linearFilterLfb = (std::atoi(val) != 0);
            if (_stricmp(key, "DrawDistance") == 0)    g_cfg.drawDistMode    = std::atoi(val);
            if (_stricmp(key, "FogMode") == 0)         g_cfg.fogMode         = std::atoi(val);
            if (_stricmp(key, "Gamma") == 0)           g_cfg.gamma           = static_cast<float>(std::atof(val));
            if (_stricmp(key, "ForceGlideOnD3D") == 0) g_cfg.forceGlideOnD3D = (std::atoi(val) != 0);
            if (_stricmp(key, "SkipIntro") == 0)             g_cfg.skipIntro             = (std::atoi(val) != 0);
            if (_stricmp(key, "ShowLauncherOnStartup") == 0) g_cfg.showLauncherOnStartup = (std::atoi(val) != 0);
            if (_stricmp(key, "CheckForUpdates") == 0)       g_cfg.checkForUpdates       = (std::atoi(val) != 0);
            if (_stricmp(key, "EnableMods") == 0)            g_cfg.enableMods            = (std::atoi(val) != 0);
            if (_stricmp(key, "FixCampaignFrontBug") == 0)   g_cfg.fixCampaignFrontBug   = (std::atoi(val) != 0);
            if (_stricmp(key, "CampaignIntelHud") == 0)      g_cfg.campaignIntelHud      = (std::atoi(val) != 0);
            if (_stricmp(key, "GunnerMouseMode") == 0)       g_cfg.gunnerMouseMode       = std::atoi(val);
            if (_stricmp(key, "GunnerMouseSens") == 0)       g_cfg.gunnerMouseSens       = static_cast<float>(std::atof(val));
            if (_stricmp(key, "BallisticLeadMode") == 0)     g_cfg.ballisticLeadMode     = std::atoi(val);
            if (_stricmp(key, "DisableJoystick") == 0)       g_cfg.disableJoystick       = (std::atoi(val) != 0);
            if (_stricmp(key, "JoyDeviceName") == 0) {
                std::strncpy(g_cfg.joyDeviceName, val, sizeof(g_cfg.joyDeviceName) - 1);
                g_cfg.joyDeviceName[sizeof(g_cfg.joyDeviceName) - 1] = '\0';
            }
            if (_stricmp(key, "JoyAxisX") == 0)              g_cfg.joyAxisX              = std::atoi(val);
            if (_stricmp(key, "JoyAxisY") == 0)              g_cfg.joyAxisY              = std::atoi(val);
            if (_stricmp(key, "JoyAxisZ") == 0)              g_cfg.joyAxisZ              = std::atoi(val);
            if (_stricmp(key, "JoyAxisR") == 0)              g_cfg.joyAxisR              = std::atoi(val);
            if (_stricmp(key, "JoyInvertX") == 0)            g_cfg.joyInvertX            = (std::atoi(val) != 0);
            if (_stricmp(key, "JoyInvertY") == 0)            g_cfg.joyInvertY            = (std::atoi(val) != 0);
            if (_stricmp(key, "JoyInvertZ") == 0)            g_cfg.joyInvertZ            = (std::atoi(val) != 0);
            if (_stricmp(key, "JoyInvertR") == 0)            g_cfg.joyInvertR            = (std::atoi(val) != 0);
            if (_stricmp(key, "JoyDeadzone") == 0)           g_cfg.joyDeadzone           = static_cast<float>(std::atof(val));
            if (_stricmp(key, "JoyBtn1") == 0)               g_cfg.joyBtn1               = std::atoi(val);
            if (_stricmp(key, "JoyBtn2") == 0)               g_cfg.joyBtn2               = std::atoi(val);
            if (_stricmp(key, "JoyBtn3") == 0)               g_cfg.joyBtn3               = std::atoi(val);
            if (_stricmp(key, "JoyBtn4") == 0)               g_cfg.joyBtn4               = std::atoi(val);
            if (_stricmp(key, "AudioMute") == 0)             g_cfg.audioMute             = (std::atoi(val) != 0);
            if (_stricmp(key, "AudioMasterVol") == 0)        g_cfg.audioMasterVol        = static_cast<float>(std::atof(val));
            if (_stricmp(key, "AudioMusicVol") == 0)         g_cfg.audioMusicVol         = static_cast<float>(std::atof(val));
            if (_stricmp(key, "AudioSfxVol") == 0)           g_cfg.audioSfxVol           = static_cast<float>(std::atof(val));
            if (_stricmp(key, "AudioEngineVol") == 0)        g_cfg.audioEngineVol        = static_cast<float>(std::atof(val));
            if (_stricmp(key, "AudioVoiceVol") == 0)         g_cfg.audioVoiceVol         = static_cast<float>(std::atof(val));
            if (_stricmp(key, "ShowStatsHud") == 0)          g_cfg.showStatsHud          = (std::atoi(val) != 0);
        }
    }
    std::fclose(f);
    LogPrintf("[CFG] Loaded m1tp2_recomp.ini (%dx%d fs=%d menuAsp=%d 3dAsp=%d msaa=%dx af=%dx skipIntro=%d)\n",
              g_cfg.windowWidth, g_cfg.windowHeight, g_cfg.fullscreen, g_cfg.menuAspectMode,
              g_cfg.aspectMode, g_cfg.msaaSamples, g_cfg.anisotropy, g_cfg.skipIntro);
}

void SaveConfig() {
    FILE* f = std::fopen("m1tp2_recomp.ini", "w");
    if (!f) return;
    std::fprintf(f, "# M1 Tank Platoon II Native Recomp Settings (press F12 in-game to configure)\n");
    std::fprintf(f, "WindowWidth = %d\n", g_cfg.windowWidth);
    std::fprintf(f, "WindowHeight = %d\n", g_cfg.windowHeight);
    std::fprintf(f, "Fullscreen = %d\n", g_cfg.fullscreen ? 1 : 0);
    std::fprintf(f, "VSync = %d\n", g_cfg.vsync ? 1 : 0);
    std::fprintf(f, "Unlock100Fps = %d\n", g_cfg.unlock100Fps ? 1 : 0);
    std::fprintf(f, "# MenuAspectMode: 0=FillScreen, 1=4:3 (Default), 2=16:10, 3=CropZoom\n");
    std::fprintf(f, "MenuAspectMode = %d\n", g_cfg.menuAspectMode);
    std::fprintf(f, "# AspectMode (3D Combat): 0=SmartHor+ (Default), 1=All3DHor+, 2=4:3, 3=Stretch\n");
    std::fprintf(f, "AspectMode = %d\n", g_cfg.aspectMode);
    std::fprintf(f, "# MapAspectMode (Battleplanner / IVIS Map): 0=AutoNative4:3 (Default), 2=16:10, 3=16:9, 4=Stretch\n");
    std::fprintf(f, "MapAspectMode = %d\n", g_cfg.mapAspectMode);
    std::fprintf(f, "# MSAA: 0, 2, 4, 8\n");
    std::fprintf(f, "MSAA = %d\n", g_cfg.msaaSamples);
    std::fprintf(f, "# Anisotropy: 1, 2, 4, 8, 16\n");
    std::fprintf(f, "Anisotropy = %d\n", g_cfg.anisotropy);
    std::fprintf(f, "# TexFilter3D: 0=Trilinear, 1=Bilinear, 2=Nearest, 3=GameDefault\n");
    std::fprintf(f, "TexFilter3D = %d\n", g_cfg.texFilter3D);
    std::fprintf(f, "LinearFilter2D = %d\n", g_cfg.linearFilter2D ? 1 : 0);
    std::fprintf(f, "LinearFilterLfb = %d\n", g_cfg.linearFilterLfb ? 1 : 0);
    std::fprintf(f, "# DrawDistance: 0=2km(Default), 1=3km(Extended), 2=4km(Long), 3=6km(Max)\n");
    std::fprintf(f, "DrawDistance = %d\n", g_cfg.drawDistMode);
    std::fprintf(f, "# FogMode: 0=Auto, 1=2xExtended, 2=4xExtended, 3=Off\n");
    std::fprintf(f, "FogMode = %d\n", g_cfg.fogMode);
    std::fprintf(f, "Gamma = %.2f\n", g_cfg.gamma);
    std::fprintf(f, "ForceGlideOnD3D = %d\n", g_cfg.forceGlideOnD3D ? 1 : 0);
    std::fprintf(f, "SkipIntro = %d\n", g_cfg.skipIntro ? 1 : 0);
    std::fprintf(f, "ShowLauncherOnStartup = %d\n", g_cfg.showLauncherOnStartup ? 1 : 0);
    std::fprintf(f, "CheckForUpdates = %d\n", g_cfg.checkForUpdates ? 1 : 0);
    std::fprintf(f, "EnableMods = %d\n", g_cfg.enableMods ? 1 : 0);
    std::fprintf(f, "FixCampaignFrontBug = %d\n", g_cfg.fixCampaignFrontBug ? 1 : 0);
    std::fprintf(f, "CampaignIntelHud = %d\n", g_cfg.campaignIntelHud ? 1 : 0);
    std::fprintf(f, "# GunnerMouseMode: 0=PrecisionStick (Default), 1=DirectRawMouse, 2=Stock1998\n");
    std::fprintf(f, "GunnerMouseMode = %d\n", g_cfg.gunnerMouseMode);
    std::fprintf(f, "GunnerMouseSens = %.2f\n", g_cfg.gunnerMouseSens);
    std::fprintf(f, "# BallisticLeadMode: 0=SmoothFCS (Default), 1=ManualNoAutoLead, 2=Stock1998\n");
    std::fprintf(f, "BallisticLeadMode = %d\n", g_cfg.ballisticLeadMode);
    std::fprintf(f, "DisableJoystick = %d\n", g_cfg.disableJoystick ? 1 : 0);
    std::fprintf(f, "JoyDeviceName = %s\n", g_cfg.joyDeviceName[0] ? g_cfg.joyDeviceName : "Auto");
    std::fprintf(f, "JoyAxisX = %d\n", g_cfg.joyAxisX);
    std::fprintf(f, "JoyAxisY = %d\n", g_cfg.joyAxisY);
    std::fprintf(f, "JoyAxisZ = %d\n", g_cfg.joyAxisZ);
    std::fprintf(f, "JoyAxisR = %d\n", g_cfg.joyAxisR);
    std::fprintf(f, "JoyInvertX = %d\n", g_cfg.joyInvertX ? 1 : 0);
    std::fprintf(f, "JoyInvertY = %d\n", g_cfg.joyInvertY ? 1 : 0);
    std::fprintf(f, "JoyInvertZ = %d\n", g_cfg.joyInvertZ ? 1 : 0);
    std::fprintf(f, "JoyInvertR = %d\n", g_cfg.joyInvertR ? 1 : 0);
    std::fprintf(f, "JoyDeadzone = %.2f\n", g_cfg.joyDeadzone);
    std::fprintf(f, "JoyBtn1 = %d\n", g_cfg.joyBtn1);
    std::fprintf(f, "JoyBtn2 = %d\n", g_cfg.joyBtn2);
    std::fprintf(f, "JoyBtn3 = %d\n", g_cfg.joyBtn3);
    std::fprintf(f, "JoyBtn4 = %d\n", g_cfg.joyBtn4);
    std::fprintf(f, "AudioMute = %d\n", g_cfg.audioMute ? 1 : 0);
    std::fprintf(f, "AudioMasterVol = %.2f\n", g_cfg.audioMasterVol);
    std::fprintf(f, "AudioMusicVol = %.2f\n", g_cfg.audioMusicVol);
    std::fprintf(f, "AudioSfxVol = %.2f\n", g_cfg.audioSfxVol);
    std::fprintf(f, "AudioEngineVol = %.2f\n", g_cfg.audioEngineVol);
    std::fprintf(f, "AudioVoiceVol = %.2f\n", g_cfg.audioVoiceVol);
    std::fprintf(f, "ShowStatsHud = %d\n", g_cfg.showStatsHud ? 1 : 0);
    std::fclose(f);
}

} // namespace m1

extern "C" __declspec(dllexport) void __cdecl M1TP2_RecompInit() {
    m1::LogInit();
    m1::LoadConfig();
    AddVectoredExceptionHandler(1, m1::CrashHandler);
    m1::HeapGuardInit();
    m1::VfsModInit();
    m1::CampaignInstallHooks();
    m1::GunneryInstallHooks();
    m1::AudioMixerInstallHooks();
    m1::JoystickSDLInstallHooks();
    m1::VideoSDLInstallHooks();
    m1::GlideGLInstallHooks();
    m1::LogPrintf("[CORE] All native replacement hooks installed successfully.\n");
}

BOOL WINAPI DllMain(HINSTANCE /*hInst*/, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_DETACH) {
        m1::GlideGLShutdown();
        m1::JoystickSDLShutdown();
        m1::VfsModShutdown();
        m1::HeapGuardShutdown();
        m1::LogShutdown();
    }
    return TRUE;
}

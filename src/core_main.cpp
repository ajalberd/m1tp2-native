#include "common.h"
#include "heap_guard.h"
#include "vfs_mod.h"
#include "campaign_hud.h"
#include "gunnery.h"
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

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0xE06D7363 || code == DBG_PRINTEXCEPTION_C) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto* ctx = ep->ContextRecord;
    LogPrintf("\n!!! EXCEPTION 0x%08lX at EIP=0x%08lX !!!\n", code, ctx->Eip);
    LogPrintf("  EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX\n", ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx);
    LogPrintf("  ESI=0x%08lX EDI=0x%08lX EBP=0x%08lX ESP=0x%08lX\n", ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        LogPrintf("  AccessViolation: %s address 0x%08lX\n",
                  ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                  static_cast<unsigned long>(ep->ExceptionRecord->ExceptionInformation[1]));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void LogInit() {
    InitializeCriticalSection(&g_logLock);
    g_logInit = true;
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
        if (std::sscanf(line, " %127[^= \t] = %127s", key, val) == 2) {
            if (_stricmp(key, "WindowWidth") == 0)     g_cfg.windowWidth     = std::atoi(val);
            if (_stricmp(key, "WindowHeight") == 0)    g_cfg.windowHeight    = std::atoi(val);
            if (_stricmp(key, "Fullscreen") == 0)      g_cfg.fullscreen      = (std::atoi(val) != 0);
            if (_stricmp(key, "VSync") == 0)           g_cfg.vsync           = (std::atoi(val) != 0);
            if (_stricmp(key, "MenuAspectMode") == 0)  g_cfg.menuAspectMode  = std::atoi(val);
            if (_stricmp(key, "AspectMode") == 0)      g_cfg.aspectMode      = std::atoi(val);
            if (_stricmp(key, "MapAspectMode") == 0)   g_cfg.mapAspectMode   = std::atoi(val);
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
            if (_stricmp(key, "EnableMods") == 0)            g_cfg.enableMods            = (std::atoi(val) != 0);
            if (_stricmp(key, "FixCampaignFrontBug") == 0)   g_cfg.fixCampaignFrontBug   = (std::atoi(val) != 0);
            if (_stricmp(key, "CampaignIntelHud") == 0)      g_cfg.campaignIntelHud      = (std::atoi(val) != 0);
            if (_stricmp(key, "GunnerMouseMode") == 0)       g_cfg.gunnerMouseMode       = std::atoi(val);
            if (_stricmp(key, "GunnerMouseSens") == 0)       g_cfg.gunnerMouseSens       = static_cast<float>(std::atof(val));
            if (_stricmp(key, "BallisticLeadMode") == 0)     g_cfg.ballisticLeadMode     = std::atoi(val);
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
    std::fprintf(f, "# MenuAspectMode: 0=FillScreen, 1=4:3 (Default), 2=16:10, 3=CropZoom\n");
    std::fprintf(f, "MenuAspectMode = %d\n", g_cfg.menuAspectMode);
    std::fprintf(f, "# AspectMode (3D Combat): 0=SmartHor+ (Default), 1=All3DHor+, 2=4:3, 3=Stretch\n");
    std::fprintf(f, "AspectMode = %d\n", g_cfg.aspectMode);
    std::fprintf(f, "# MapAspectMode (Battleplanner / IVIS Map): 0=FillScreen, 1=4:3 (Default), 2=16:10\n");
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
    std::fprintf(f, "EnableMods = %d\n", g_cfg.enableMods ? 1 : 0);
    std::fprintf(f, "FixCampaignFrontBug = %d\n", g_cfg.fixCampaignFrontBug ? 1 : 0);
    std::fprintf(f, "CampaignIntelHud = %d\n", g_cfg.campaignIntelHud ? 1 : 0);
    std::fprintf(f, "# GunnerMouseMode: 0=PrecisionStick (Default), 1=DirectRawMouse, 2=Stock1998\n");
    std::fprintf(f, "GunnerMouseMode = %d\n", g_cfg.gunnerMouseMode);
    std::fprintf(f, "GunnerMouseSens = %.2f\n", g_cfg.gunnerMouseSens);
    std::fprintf(f, "# BallisticLeadMode: 0=SmoothFCS (Default), 1=ManualNoAutoLead, 2=Stock1998\n");
    std::fprintf(f, "BallisticLeadMode = %d\n", g_cfg.ballisticLeadMode);
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
    m1::VideoSDLInstallHooks();
    m1::GlideGLInstallHooks();
    m1::LogPrintf("[CORE] All native replacement hooks installed successfully.\n");
}

BOOL WINAPI DllMain(HINSTANCE /*hInst*/, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_DETACH) {
        m1::GlideGLShutdown();
        m1::VfsModShutdown();
        m1::HeapGuardShutdown();
        m1::LogShutdown();
    }
    return TRUE;
}

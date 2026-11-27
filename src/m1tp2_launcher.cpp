// M1 Tank Platoon II — Native GUI Launcher (SDL3 + OpenGL 3.3 + Dear ImGui)
// Feature parity with falcon30-native Trilogy Launcher:
//   - Launch Cards: M1 Tank Platoon II (M1TP2_bin.exe Win11 EmulateHeap bypass) & War College (DSG)
//   - Direct Manuals & Docs Links: Official PDF Manual, v1.2 Patch Notes, Original ReadMe, Mods Folder
//   - Built-in Quickstart Keyboard Layout Image Viewer (docs\M1TP2_Quickstart_Keys.png)
//   - Live 2-way m1tp2_recomp.ini Quick Launch Settings (Graphics, Widescreen, Gunnery Mouse, FCS Lead, Campaign)
//   - CLI flags: --launcher | --no-launcher / --play | --war-college / -w

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kLauncherVersion = "0.3.2";
constexpr const char* kGithubLatestUrl = "https://api.github.com/repos/ajalberd/m1tp2-native/releases/latest";

struct LauncherCfg {
    int   windowWidth           = 0;
    int   windowHeight          = 0;
    bool  fullscreen            = true;
    bool  vsync                 = true;
    bool  unlock100Fps          = true;
    int   menuAspectMode        = 1;    // 4:3
    int   aspectMode            = 0;    // Smart Hor+
    int   mapAspectMode         = 0;    // Auto Aspect-Native (4:3)
    int   msaaSamples           = 4;
    int   anisotropy            = 16;
    int   texFilter3D           = 0;
    bool  linearFilter2D        = true;
    bool  linearFilterLfb       = false;
    int   drawDistMode          = 1;    // 3.0 km
    int   fogMode               = 0;    // Auto
    float gamma                 = 1.0f;
    bool  forceGlideOnD3D       = true;
    bool  skipIntro             = true;
    bool  showLauncherOnStartup = true;
    bool  checkForUpdates       = true;
    bool  enableMods            = true;
    bool  fixCampaignFrontBug   = true;
    bool  campaignIntelHud      = true;
    int   gunnerMouseMode       = 0;    // Precision Stick
    float gunnerMouseSens       = 1.0f;
    int   ballisticLeadMode     = 0;    // Smooth FCS
    bool  disableJoystick       = true;
    std::string joyDeviceName   = "Auto";
    int   joyAxisX              = 0;
    int   joyAxisY              = 1;
    int   joyAxisZ              = 2;
    int   joyAxisR              = 3;
    bool  joyInvertX            = false;
    bool  joyInvertY            = false;
    bool  joyInvertZ            = false;
    bool  joyInvertR            = false;
    float joyDeadzone           = 0.08f;
    int   joyBtn1               = 0;
    int   joyBtn2               = 1;
    int   joyBtn3               = 2;
    int   joyBtn4               = 3;
    bool  audioMute             = false;
    float audioMasterVol        = 1.0f;
    float audioMusicVol         = 1.0f;
    float audioSfxVol           = 1.0f;
    float audioEngineVol        = 1.0f;
    float audioVoiceVol         = 1.0f;
    bool  showStatsHud          = false;
};

struct StockGameCfg {
    bool    loaded      = false;
    bool    dirty       = false;
    int32_t menuMusicOn = 0; // 0x872B80: menu/briefing music on/off (disabled by default)
    int32_t detailHigh  = 1;
    int32_t renderMode  = 1; // 0=SW, 1=Glide, 2=D3D
    int32_t alphaBlend  = 1;
    int32_t extraFlag   = 1;
    char    hostName[9]   = "HOST";
    char    playerName[9] = "PLAYER";
};

static bool LoadStockGameCfg(const std::string& path, StockGameCfg& g) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t buf[38] = {};
    size_t rd = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    if (rd < 20) return false;
    int32_t ints[5] = {};
    std::memcpy(ints, buf, sizeof(ints));
    g.menuMusicOn = ints[0] ? 1 : 0;
    g.detailHigh  = ints[1] ? 1 : 0;
    g.renderMode  = std::clamp(ints[2], 0, 2);
    g.alphaBlend  = ints[3] ? 1 : 0;
    g.extraFlag   = ints[4] ? 1 : 0;
    if (rd >= 38) {
        std::memcpy(g.hostName,   buf + 20, 8); g.hostName[8]   = '\0';
        std::memcpy(g.playerName, buf + 29, 8); g.playerName[8] = '\0';
    }
    g.loaded = true;
    return true;
}

static void SaveStockGameCfg(const std::string& path, const StockGameCfg& g) {
    if (!g.loaded || !g.dirty) return;
    uint8_t buf[38] = {};
    int32_t ints[5] = { g.menuMusicOn, g.detailHigh, g.renderMode, g.alphaBlend, g.extraFlag };
    std::memcpy(buf, ints, sizeof(ints));
    std::strncpy(reinterpret_cast<char*>(buf + 20), g.hostName, 8);
    std::strncpy(reinterpret_cast<char*>(buf + 29), g.playerName, 8);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(buf, 1, sizeof(buf), f);
    std::fclose(f);
}

static bool FileExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void LoadIni(const std::string& path, LauncherCfg& c) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == ';') continue;
        char k[128], v[128];
        if (std::sscanf(line, " %127[^= \t] = %127[^\r\n]", k, v) == 2) {
            size_t vlen = std::strlen(v);
            while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t')) v[--vlen] = '\0';
            if (_stricmp(k, "WindowWidth") == 0)           c.windowWidth           = std::atoi(v);
            if (_stricmp(k, "WindowHeight") == 0)          c.windowHeight          = std::atoi(v);
            if (_stricmp(k, "Fullscreen") == 0)            c.fullscreen            = (std::atoi(v) != 0);
            if (_stricmp(k, "VSync") == 0)                 c.vsync                 = (std::atoi(v) != 0);
            if (_stricmp(k, "Unlock100Fps") == 0)          c.unlock100Fps          = (std::atoi(v) != 0);
            if (_stricmp(k, "MenuAspectMode") == 0)        c.menuAspectMode        = std::atoi(v);
            if (_stricmp(k, "AspectMode") == 0)            c.aspectMode            = std::atoi(v);
            if (_stricmp(k, "MapAspectMode") == 0) {
                int mv = std::atoi(v);
                c.mapAspectMode = (mv == 1) ? 0 : mv;
            }
            if (_stricmp(k, "MSAA") == 0)                  c.msaaSamples           = std::atoi(v);
            if (_stricmp(k, "Anisotropy") == 0)            c.anisotropy            = std::atoi(v);
            if (_stricmp(k, "TexFilter3D") == 0)           c.texFilter3D           = std::atoi(v);
            if (_stricmp(k, "LinearFilter2D") == 0)        c.linearFilter2D        = (std::atoi(v) != 0);
            if (_stricmp(k, "LinearFilterLfb") == 0)       c.linearFilterLfb       = (std::atoi(v) != 0);
            if (_stricmp(k, "DrawDistance") == 0)          c.drawDistMode          = std::atoi(v);
            if (_stricmp(k, "FogMode") == 0)               c.fogMode               = std::atoi(v);
            if (_stricmp(k, "Gamma") == 0)                 c.gamma                 = static_cast<float>(std::atof(v));
            if (_stricmp(k, "ForceGlideOnD3D") == 0)       c.forceGlideOnD3D       = (std::atoi(v) != 0);
            if (_stricmp(k, "SkipIntro") == 0)             c.skipIntro             = (std::atoi(v) != 0);
            if (_stricmp(k, "ShowLauncherOnStartup") == 0) c.showLauncherOnStartup = (std::atoi(v) != 0);
            if (_stricmp(k, "CheckForUpdates") == 0)       c.checkForUpdates       = (std::atoi(v) != 0);
            if (_stricmp(k, "EnableMods") == 0)            c.enableMods            = (std::atoi(v) != 0);
            if (_stricmp(k, "FixCampaignFrontBug") == 0)   c.fixCampaignFrontBug   = (std::atoi(v) != 0);
            if (_stricmp(k, "CampaignIntelHud") == 0)      c.campaignIntelHud      = (std::atoi(v) != 0);
            if (_stricmp(k, "GunnerMouseMode") == 0)       c.gunnerMouseMode       = std::atoi(v);
            if (_stricmp(k, "GunnerMouseSens") == 0)       c.gunnerMouseSens       = static_cast<float>(std::atof(v));
            if (_stricmp(k, "BallisticLeadMode") == 0)     c.ballisticLeadMode     = std::atoi(v);
            if (_stricmp(k, "DisableJoystick") == 0)       c.disableJoystick       = (std::atoi(v) != 0);
            if (_stricmp(k, "JoyDeviceName") == 0)         c.joyDeviceName         = v;
            if (_stricmp(k, "JoyAxisX") == 0)              c.joyAxisX              = std::atoi(v);
            if (_stricmp(k, "JoyAxisY") == 0)              c.joyAxisY              = std::atoi(v);
            if (_stricmp(k, "JoyAxisZ") == 0)              c.joyAxisZ              = std::atoi(v);
            if (_stricmp(k, "JoyAxisR") == 0)              c.joyAxisR              = std::atoi(v);
            if (_stricmp(k, "JoyInvertX") == 0)            c.joyInvertX            = (std::atoi(v) != 0);
            if (_stricmp(k, "JoyInvertY") == 0)            c.joyInvertY            = (std::atoi(v) != 0);
            if (_stricmp(k, "JoyInvertZ") == 0)            c.joyInvertZ            = (std::atoi(v) != 0);
            if (_stricmp(k, "JoyInvertR") == 0)            c.joyInvertR            = (std::atoi(v) != 0);
            if (_stricmp(k, "JoyDeadzone") == 0)           c.joyDeadzone           = std::clamp(static_cast<float>(std::atof(v)), 0.0f, 0.40f);
            if (_stricmp(k, "JoyBtn1") == 0)               c.joyBtn1               = std::atoi(v);
            if (_stricmp(k, "JoyBtn2") == 0)               c.joyBtn2               = std::atoi(v);
            if (_stricmp(k, "JoyBtn3") == 0)               c.joyBtn3               = std::atoi(v);
            if (_stricmp(k, "JoyBtn4") == 0)               c.joyBtn4               = std::atoi(v);
            if (_stricmp(k, "AudioMute") == 0)             c.audioMute             = (std::atoi(v) != 0);
            if (_stricmp(k, "AudioMasterVol") == 0)        c.audioMasterVol        = std::clamp(static_cast<float>(std::atof(v)), 0.0f, 1.0f);
            if (_stricmp(k, "AudioMusicVol") == 0)         c.audioMusicVol         = std::clamp(static_cast<float>(std::atof(v)), 0.0f, 1.0f);
            if (_stricmp(k, "AudioSfxVol") == 0)           c.audioSfxVol           = std::clamp(static_cast<float>(std::atof(v)), 0.0f, 1.0f);
            if (_stricmp(k, "AudioEngineVol") == 0)        c.audioEngineVol        = std::clamp(static_cast<float>(std::atof(v)), 0.0f, 1.0f);
            if (_stricmp(k, "AudioVoiceVol") == 0)         c.audioVoiceVol         = std::clamp(static_cast<float>(std::atof(v)), 0.0f, 1.0f);
            if (_stricmp(k, "ShowStatsHud") == 0)          c.showStatsHud          = (std::atoi(v) != 0);
        }
    }
    std::fclose(f);
}

static void SaveIni(const std::string& path, const LauncherCfg& c) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "# M1 Tank Platoon II Native Recomp Settings (press F12 in-game to configure)\n");
    std::fprintf(f, "WindowWidth = %d\n", c.windowWidth);
    std::fprintf(f, "WindowHeight = %d\n", c.windowHeight);
    std::fprintf(f, "Fullscreen = %d\n", c.fullscreen ? 1 : 0);
    std::fprintf(f, "VSync = %d\n", c.vsync ? 1 : 0);
    std::fprintf(f, "Unlock100Fps = %d\n", c.unlock100Fps ? 1 : 0);
    std::fprintf(f, "# MenuAspectMode: 0=FillScreen, 1=4:3 (Default), 2=16:10, 3=CropZoom\n");
    std::fprintf(f, "MenuAspectMode = %d\n", c.menuAspectMode);
    std::fprintf(f, "# AspectMode (3D Combat): 0=SmartHor+ (Default), 1=All3DHor+, 2=4:3, 3=Stretch\n");
    std::fprintf(f, "AspectMode = %d\n", c.aspectMode);
    std::fprintf(f, "# MapAspectMode (Battleplanner / IVIS Map): 0=AutoNative4:3 (Default), 2=16:10, 3=16:9, 4=Stretch\n");
    std::fprintf(f, "MapAspectMode = %d\n", c.mapAspectMode);
    std::fprintf(f, "# MSAA: 0, 2, 4, 8\n");
    std::fprintf(f, "MSAA = %d\n", c.msaaSamples);
    std::fprintf(f, "# Anisotropy: 1, 2, 4, 8, 16\n");
    std::fprintf(f, "Anisotropy = %d\n", c.anisotropy);
    std::fprintf(f, "# TexFilter3D: 0=Trilinear, 1=Bilinear, 2=Nearest, 3=GameDefault\n");
    std::fprintf(f, "TexFilter3D = %d\n", c.texFilter3D);
    std::fprintf(f, "LinearFilter2D = %d\n", c.linearFilter2D ? 1 : 0);
    std::fprintf(f, "LinearFilterLfb = %d\n", c.linearFilterLfb ? 1 : 0);
    std::fprintf(f, "# DrawDistance: 0=2km(Default), 1=3km(Extended), 2=4km(Long), 3=6km(Max)\n");
    std::fprintf(f, "DrawDistance = %d\n", c.drawDistMode);
    std::fprintf(f, "# FogMode: 0=Auto, 1=2xExtended, 2=4xExtended, 3=Off\n");
    std::fprintf(f, "FogMode = %d\n", c.fogMode);
    std::fprintf(f, "Gamma = %.2f\n", c.gamma);
    std::fprintf(f, "ForceGlideOnD3D = %d\n", c.forceGlideOnD3D ? 1 : 0);
    std::fprintf(f, "SkipIntro = %d\n", c.skipIntro ? 1 : 0);
    std::fprintf(f, "ShowLauncherOnStartup = %d\n", c.showLauncherOnStartup ? 1 : 0);
    std::fprintf(f, "CheckForUpdates = %d\n", c.checkForUpdates ? 1 : 0);
    std::fprintf(f, "EnableMods = %d\n", c.enableMods ? 1 : 0);
    std::fprintf(f, "FixCampaignFrontBug = %d\n", c.fixCampaignFrontBug ? 1 : 0);
    std::fprintf(f, "CampaignIntelHud = %d\n", c.campaignIntelHud ? 1 : 0);
    std::fprintf(f, "# GunnerMouseMode: 0=PrecisionStick (Default), 1=DirectRawMouse, 2=Stock1998\n");
    std::fprintf(f, "GunnerMouseMode = %d\n", c.gunnerMouseMode);
    std::fprintf(f, "GunnerMouseSens = %.2f\n", c.gunnerMouseSens);
    std::fprintf(f, "# BallisticLeadMode: 0=SmoothFCS (Default), 1=ManualNoAutoLead, 2=Stock1998\n");
    std::fprintf(f, "BallisticLeadMode = %d\n", c.ballisticLeadMode);
    std::fprintf(f, "DisableJoystick = %d\n", c.disableJoystick ? 1 : 0);
    std::fprintf(f, "JoyDeviceName = %s\n", c.joyDeviceName.empty() ? "Auto" : c.joyDeviceName.c_str());
    std::fprintf(f, "JoyAxisX = %d\n", c.joyAxisX);
    std::fprintf(f, "JoyAxisY = %d\n", c.joyAxisY);
    std::fprintf(f, "JoyAxisZ = %d\n", c.joyAxisZ);
    std::fprintf(f, "JoyAxisR = %d\n", c.joyAxisR);
    std::fprintf(f, "JoyInvertX = %d\n", c.joyInvertX ? 1 : 0);
    std::fprintf(f, "JoyInvertY = %d\n", c.joyInvertY ? 1 : 0);
    std::fprintf(f, "JoyInvertZ = %d\n", c.joyInvertZ ? 1 : 0);
    std::fprintf(f, "JoyInvertR = %d\n", c.joyInvertR ? 1 : 0);
    std::fprintf(f, "JoyDeadzone = %.2f\n", c.joyDeadzone);
    std::fprintf(f, "JoyBtn1 = %d\n", c.joyBtn1);
    std::fprintf(f, "JoyBtn2 = %d\n", c.joyBtn2);
    std::fprintf(f, "JoyBtn3 = %d\n", c.joyBtn3);
    std::fprintf(f, "JoyBtn4 = %d\n", c.joyBtn4);
    std::fprintf(f, "AudioMute = %d\n", c.audioMute ? 1 : 0);
    std::fprintf(f, "AudioMasterVol = %.2f\n", c.audioMasterVol);
    std::fprintf(f, "AudioMusicVol = %.2f\n", c.audioMusicVol);
    std::fprintf(f, "AudioSfxVol = %.2f\n", c.audioSfxVol);
    std::fprintf(f, "AudioEngineVol = %.2f\n", c.audioEngineVol);
    std::fprintf(f, "AudioVoiceVol = %.2f\n", c.audioVoiceVol);
    std::fprintf(f, "ShowStatsHud = %d\n", c.showStatsHud ? 1 : 0);
    std::fclose(f);
}

static bool LaunchMainGame(const std::string& dir) {
    std::string origExe = dir + "\\M1TP2.exe";
    std::string binExe  = dir + "\\M1TP2_bin.exe";
    if (!FileExists(binExe)) {
        if (!CreateHardLinkA(binExe.c_str(), origExe.c_str(), nullptr)) {
            if (!CopyFileA(origExe.c_str(), binExe.c_str(), FALSE)) {
                MessageBoxA(nullptr, "Could not locate M1TP2.exe in the game directory.",
                            "M1TP2 Native Launcher", MB_ICONERROR);
                return false;
            }
        }
    }
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (!CreateProcessA(binExe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        MessageBoxA(nullptr, "Failed to launch M1TP2_bin.exe.", "M1TP2 Native Launcher", MB_ICONERROR);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static bool LaunchWarCollege(const std::string& dir) {
    std::string dsgDir  = dir + "\\Dsg";
    std::string omoExe  = dir + "\\Dsg\\OMOPLAY.EXE";
    std::string staFile = dir + "\\Dsg\\M1DSG.sta";

    if (!FileExists(omoExe)) {
        MessageBoxA(nullptr, "War College (Dsg\\OMOPLAY.EXE) was not found in this installation.",
                    "M1TP2 Native Launcher", MB_ICONERROR);
        return false;
    }

    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    // 1. Prefer GOG's bundled BoxedWine FIRST: GOG pre-installed 16-bit QuickTime for Windows (QTW)
    //    inside BoxedWine's Wine prefix, which OMOPLAY.EXE requires to play War College movies.
    std::string bwDir = dir + "\\Boxedwine";
    std::string bwExe = dir + "\\Boxedwine\\x64\\BoxedWine.exe";
    if (!FileExists(bwExe)) bwExe = dir + "\\Boxedwine\\x86\\BoxedWine.exe";
    if (FileExists(bwExe)) {
        char cmd[MAX_PATH * 3];
        std::snprintf(cmd, sizeof(cmd),
            "\"%s\" -title \"M1 Tank Platoon II - War College\" -root \"./FileSystems/root\" "
            "-resolution 1024x768 -env BOXED_DD_AUTOREFRESH=1 -zip ./FileSystems/Wine-6.0b.zip "
            "../Dsg/OMOPLAY.EXE M1DSG.sta",
            bwExe.c_str());
        if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, bwDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
    }

    // 2. Fallback to otvdmw.exe (for Retail CD installs without BoxedWine; requires Dsg\QT16.EXE installed)
    char otvdm[MAX_PATH] = {};
    std::string localOtvdm = dir + "\\otvdmw.exe";
    if (FileExists(localOtvdm)) {
        lstrcpynA(otvdm, localOtvdm.c_str(), MAX_PATH);
    } else if (SearchPathA(nullptr, "otvdmw.exe", nullptr, MAX_PATH, otvdm, nullptr) == 0) {
        otvdm[0] = '\0';
    }
    if (otvdm[0]) {
        char cmd[MAX_PATH * 3];
        std::snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\"", otvdm, omoExe.c_str(), staFile.c_str());
        if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, dsgDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
    }

    MessageBoxA(nullptr,
        "War College (Dsg\\OMOPLAY.EXE) is a 16-bit Windows 3.1 application.\n"
        "Install otvdm (winevdm) or keep the GOG Boxedwine folder to launch it on 64-bit Windows.",
        "M1TP2 Native Launcher", MB_ICONINFORMATION);
    return false;
}

static GLuint LoadPngTexture(const std::string& path, int& outW, int& outH) {
    outW = outH = 0;
    if (!FileExists(path)) return 0;
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return 0;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    using GenMipFn = void (APIENTRY *)(GLenum);
    auto genMip = reinterpret_cast<GenMipFn>(SDL_GL_GetProcAddress("glGenerateMipmap"));
    if (genMip) {
        genMip(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
    outW = w;
    outH = h;
    return tex;
}

static void ApplyLauncherTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding   = 0.0f;
    st.ChildRounding    = 8.0f;
    st.FrameRounding    = 5.0f;
    st.GrabRounding     = 4.0f;
    st.FramePadding     = ImVec2(8.0f, 5.0f);
    st.ItemSpacing      = ImVec2(10.0f, 7.0f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.052f, 0.075f, 0.105f, 1.00f);
    c[ImGuiCol_ChildBg]          = ImVec4(0.075f, 0.105f, 0.145f, 1.00f);
    c[ImGuiCol_Border]           = ImVec4(0.22f, 0.30f, 0.38f, 0.90f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.11f, 0.16f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.15f, 0.24f, 0.32f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.18f, 0.32f, 0.26f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.30f, 0.95f, 0.48f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.26f, 0.78f, 0.44f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.96f, 0.54f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.14f, 0.24f, 0.34f, 1.00f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.20f, 0.34f, 0.46f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.16f, 0.42f, 0.30f, 1.00f);
}

// ==================== Native WinHTTP + BCrypt SHA-256 GitHub Auto-Updater ====================

enum class UpdatePhase : int {
    Idle = 0,
    Checking,
    UpToDate,
    Available,
    Downloading,
    Verifying,
    ReadyToInstall,
    Error,
};

struct UpdaterState {
    std::atomic<int>      phase{static_cast<int>(UpdatePhase::Idle)};
    std::atomic<bool>     cancel{false};
    std::atomic<uint64_t> dlBytes{0};
    std::atomic<uint64_t> dlTotal{0};

    std::mutex  mtx;
    std::string latestTag;
    std::string releaseUrl;
    std::string setupUrl;
    std::string setupName;
    std::string sumsUrl;
    std::string downloadedExe;
    std::string errorMsg;
};

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static bool ParseSemVer(const std::string& tag, int& maj, int& min, int& pat) {
    maj = min = pat = 0;
    const char* p = tag.c_str();
    while (*p && (*p == 'v' || *p == 'V' || *p == ' ')) ++p;
    return std::sscanf(p, "%d.%d.%d", &maj, &min, &pat) >= 2;
}

static bool IsNewerSemVer(const std::string& remoteTag, const std::string& localVer) {
    int rMaj, rMin, rPat, lMaj, lMin, lPat;
    if (!ParseSemVer(remoteTag, rMaj, rMin, rPat)) return false;
    if (!ParseSemVer(localVer,  lMaj, lMin, lPat)) return false;
    if (rMaj != lMaj) return rMaj > lMaj;
    if (rMin != lMin) return rMin > lMin;
    return rPat > lPat;
}

static bool WinHttpFetch(const std::string& url,
                         std::string* outStr,
                         const std::string* outFilePath,
                         const std::shared_ptr<UpdaterState>& st,
                         std::string& errOut) {
    std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[2048] = {};
    uc.lpszHostName     = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath      = pathBuf;
    uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        errOut = "Invalid URL";
        return false;
    }

    std::wstring ua = L"m1tp2-native/" + Utf8ToWide(kLauncherVersion);
    HINTERNET hSess = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) {
        hSess = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!hSess) { errOut = "WinHttpOpen failed"; return false; }

    WinHttpSetTimeouts(hSess, 5000, 5000, 10000, 20000);
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSess, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    HINTERNET hConn = WinHttpConnect(hSess, hostBuf, uc.nPort, 0);
    if (!hConn) {
        WinHttpCloseHandle(hSess);
        errOut = "Connection failed";
        return false;
    }

    DWORD reqFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", pathBuf, nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hReq) {
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        errOut = "Request creation failed";
        return false;
    }

    const wchar_t* hdrs = L"Accept: application/vnd.github+json\r\n";
    BOOL ok = WinHttpSendRequest(hReq, hdrs, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
              && WinHttpReceiveResponse(hReq, nullptr);
    if (!ok) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        errOut = "Network unreachable";
        return false;
    }

    DWORD status = 0, statusSz = sizeof(status);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSz, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        errOut = "HTTP " + std::to_string(status);
        return false;
    }

    if (outFilePath && st) {
        wchar_t lenBuf[64] = {};
        DWORD lenSz = sizeof(lenBuf);
        if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSz, WINHTTP_NO_HEADER_INDEX)) {
            st->dlTotal.store(static_cast<uint64_t>(_wcstoui64(lenBuf, nullptr, 10)));
        }
        st->dlBytes.store(0);
    }

    FILE* fp = nullptr;
    if (outFilePath) {
        fp = std::fopen(outFilePath->c_str(), "wb");
        if (!fp) {
            WinHttpCloseHandle(hReq);
            WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess);
            errOut = "Failed to open temp file";
            return false;
        }
    }

    std::vector<char> buf(65536);
    bool success = true;
    while (true) {
        if (st && st->cancel.load()) { success = false; errOut = "Cancelled"; break; }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) { success = false; errOut = "Read error"; break; }
        if (avail == 0) break;
        DWORD toRead = std::min<DWORD>(avail, static_cast<DWORD>(buf.size()));
        DWORD got = 0;
        if (!WinHttpReadData(hReq, buf.data(), toRead, &got)) { success = false; errOut = "Download interrupted"; break; }
        if (got == 0) break;
        if (outStr) outStr->append(buf.data(), got);
        if (fp) {
            if (std::fwrite(buf.data(), 1, got, fp) != got) { success = false; errOut = "Disk write failed"; break; }
            if (st) st->dlBytes.fetch_add(got);
        }
    }

    if (fp) std::fclose(fp);
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    if (!success && outFilePath) DeleteFileA(outFilePath->c_str());
    return success;
}

static bool ExtractJsonStringField(const std::string& json, const std::string& key, size_t startPos,
                                   std::string& outVal, size_t* outNextPos = nullptr) {
    std::string pat = "\"" + key + "\"";
    size_t pos = json.find(pat, startPos);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + pat.size());
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    ++pos;
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
        }
        val.push_back(json[pos++]);
    }
    outVal = val;
    if (outNextPos) *outNextPos = (pos < json.size()) ? (pos + 1) : pos;
    return true;
}

static bool ComputeFileSha256Hex(const std::string& path, std::string& outHex) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0) {
            std::vector<uint8_t> buf(65536);
            bool ioOk = true;
            while (size_t rd = std::fread(buf.data(), 1, buf.size(), f)) {
                if (BCryptHashData(hHash, buf.data(), static_cast<ULONG>(rd), 0) != 0) {
                    ioOk = false;
                    break;
                }
            }
            uint8_t digest[32] = {};
            if (ioOk && BCryptFinishHash(hHash, digest, sizeof(digest), 0) == 0) {
                char hex[65];
                for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
                outHex = hex;
                ok = true;
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    std::fclose(f);
    return ok;
}

static void StartUpdateCheck(const std::shared_ptr<UpdaterState>& st) {
    int cur = st->phase.load();
    if (cur == static_cast<int>(UpdatePhase::Checking) ||
        cur == static_cast<int>(UpdatePhase::Downloading) ||
        cur == static_cast<int>(UpdatePhase::Verifying)) {
        return;
    }
    st->phase.store(static_cast<int>(UpdatePhase::Checking));
    std::thread([st]() {
        std::string body, err;
        if (!WinHttpFetch(kGithubLatestUrl, &body, nullptr, st, err)) {
            if (!st->cancel.load()) {
                std::lock_guard<std::mutex> lk(st->mtx);
                st->errorMsg = err;
                st->phase.store(static_cast<int>(UpdatePhase::Error));
            }
            return;
        }
        std::string tag, htmlUrl;
        if (!ExtractJsonStringField(body, "tag_name", 0, tag)) {
            std::lock_guard<std::mutex> lk(st->mtx);
            st->errorMsg = "Could not parse release tag";
            st->phase.store(static_cast<int>(UpdatePhase::Error));
            return;
        }
        ExtractJsonStringField(body, "html_url", 0, htmlUrl);

        std::string setupUrl, setupName, sumsUrl;
        size_t scanPos = body.find("\"assets\"");
        if (scanPos == std::string::npos) scanPos = 0;
        std::string dlUrl;
        while (ExtractJsonStringField(body, "browser_download_url", scanPos, dlUrl, &scanPos)) {
            size_t sl = dlUrl.find_last_of('/');
            std::string fname = (sl != std::string::npos) ? dlUrl.substr(sl + 1) : dlUrl;
            if (fname.find("Setup") != std::string::npos && fname.size() > 4 &&
                _stricmp(fname.c_str() + fname.size() - 4, ".exe") == 0) {
                setupUrl  = dlUrl;
                setupName = fname;
            } else if (_stricmp(fname.c_str(), "SHA256SUMS.txt") == 0) {
                sumsUrl = dlUrl;
            }
        }

        std::lock_guard<std::mutex> lk(st->mtx);
        st->latestTag  = tag;
        st->releaseUrl = htmlUrl;
        st->setupUrl   = setupUrl;
        st->setupName  = setupName;
        st->sumsUrl    = sumsUrl;
        if (IsNewerSemVer(tag, kLauncherVersion) && !setupUrl.empty()) {
            st->phase.store(static_cast<int>(UpdatePhase::Available));
        } else {
            st->phase.store(static_cast<int>(UpdatePhase::UpToDate));
        }
    }).detach();
}

static void StartUpdateDownload(const std::shared_ptr<UpdaterState>& st) {
    std::string setupUrl, setupName, sumsUrl;
    {
        std::lock_guard<std::mutex> lk(st->mtx);
        setupUrl  = st->setupUrl;
        setupName = st->setupName;
        sumsUrl   = st->sumsUrl;
    }
    if (setupUrl.empty() || setupName.empty()) return;

    st->phase.store(static_cast<int>(UpdatePhase::Downloading));
    std::thread([st, setupUrl, setupName, sumsUrl]() {
        char tmpBuf[MAX_PATH] = {};
        if (GetTempPathA(MAX_PATH, tmpBuf) == 0) {
            std::lock_guard<std::mutex> lk(st->mtx);
            st->errorMsg = "Temp path unavailable";
            st->phase.store(static_cast<int>(UpdatePhase::Error));
            return;
        }
        std::string updDir = std::string(tmpBuf) + "m1tp2_update";
        CreateDirectoryA(updDir.c_str(), nullptr);
        std::string destPath = updDir + "\\" + setupName;

        std::string err;
        if (!WinHttpFetch(setupUrl, nullptr, &destPath, st, err)) {
            if (!st->cancel.load()) {
                std::lock_guard<std::mutex> lk(st->mtx);
                st->errorMsg = "Download failed: " + err;
                st->phase.store(static_cast<int>(UpdatePhase::Error));
            }
            return;
        }

        st->phase.store(static_cast<int>(UpdatePhase::Verifying));
        if (!sumsUrl.empty()) {
            std::string sumsBody, sumsErr;
            if (WinHttpFetch(sumsUrl, &sumsBody, nullptr, st, sumsErr)) {
                std::string expectedHash;
                size_t p = 0;
                while (p < sumsBody.size()) {
                    size_t nl = sumsBody.find('\n', p);
                    std::string line = (nl == std::string::npos) ? sumsBody.substr(p) : sumsBody.substr(p, nl - p);
                    p = (nl == std::string::npos) ? sumsBody.size() : (nl + 1);
                    if (line.find(setupName) != std::string::npos && line.size() >= 64) {
                        expectedHash = line.substr(0, 64);
                        break;
                    }
                }
                if (!expectedHash.empty()) {
                    std::string actualHash;
                    if (!ComputeFileSha256Hex(destPath, actualHash) ||
                        _stricmp(actualHash.c_str(), expectedHash.c_str()) != 0) {
                        DeleteFileA(destPath.c_str());
                        std::lock_guard<std::mutex> lk(st->mtx);
                        st->errorMsg = "SHA-256 verification failed!";
                        st->phase.store(static_cast<int>(UpdatePhase::Error));
                        return;
                    }
                }
            }
        }

        if (!st->cancel.load()) {
            std::lock_guard<std::mutex> lk(st->mtx);
            st->downloadedExe = destPath;
            st->phase.store(static_cast<int>(UpdatePhase::ReadyToInstall));
        }
    }).detach();
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmd, int) {
    char dirBuf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, dirBuf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    std::string dir(dirBuf);
    size_t sl = dir.find_last_of("\\/");
    if (sl != std::string::npos) dir.resize(sl);
    SetCurrentDirectoryA(dir.c_str());

    bool forceLauncher = false;
    bool skipLauncher  = false;
    bool warCollegeCli = false;
    if (lpCmd) {
        if (std::strstr(lpCmd, "--war-college") || std::strstr(lpCmd, "-w")) warCollegeCli = true;
        if (std::strstr(lpCmd, "--launcher"))                                 forceLauncher = true;
        if (std::strstr(lpCmd, "--no-launcher") || std::strstr(lpCmd, "--play")) skipLauncher = true;
    }

    if (warCollegeCli) {
        return LaunchWarCollege(dir) ? 0 : 1;
    }

    std::string iniPath = dir + "\\m1tp2_recomp.ini";
    LauncherCfg cfg;
    LoadIni(iniPath, cfg);

    std::string stockCfgPath = dir + "\\game.cfg";
    StockGameCfg stockCfg;
    LoadStockGameCfg(stockCfgPath, stockCfg);

    bool hasGame       = FileExists(dir + "\\M1TP2.exe");
    bool hasWarCollege = FileExists(dir + "\\Dsg\\OMOPLAY.EXE");
    bool hasManualPdf  = FileExists(dir + "\\M1_Tank_Platoon_II_Manual.pdf");
    bool hasPatchTxt   = FileExists(dir + "\\M1TP2Readme.txt");
    bool hasReadmeTxt  = FileExists(dir + "\\ReadMe.txt");

    std::string keysPngPath = dir + "\\docs\\M1TP2_Quickstart_Keys.png";
    if (!FileExists(keysPngPath)) keysPngPath = dir + "\\recomp\\docs\\M1TP2_Quickstart_Keys.png";
    if (!FileExists(keysPngPath)) keysPngPath = dir + "\\M1TP2_Quickstart_Keys.png";
    bool hasKeysPng = FileExists(keysPngPath);

    if (!forceLauncher && hasGame && (skipLauncher || !cfg.showLauncherOnStartup)) {
        return LaunchMainGame(dir) ? 0 : 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK)) {
        // Fallback: if SDL video fails for any reason, launch directly
        return hasGame ? (LaunchMainGame(dir) ? 0 : 1) : 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* win = SDL_CreateWindow(
        "M1 Tank Platoon II — Native Launcher",
        1120, 860,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) {
        SDL_Quit();
        return hasGame ? (LaunchMainGame(dir) ? 0 : 1) : 1;
    }
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GLContext glCtx = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, glCtx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // Load crisp TrueType font baked at high res (28px) so maximizing on 1440p/21:9/4K stays razor-sharp
    float baseFontScale = 1.0f;
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf"
    };
    for (const char* fp : fontCandidates) {
        if (FileExists(fp)) {
            if (io.Fonts->AddFontFromFileTTF(fp, 28.0f)) {
                baseFontScale = 15.5f / 28.0f;
                break;
            }
        }
    }

    ApplyLauncherTheme();
    const ImGuiStyle baseStyle = ImGui::GetStyle();
    ImGui_ImplSDL3_InitForOpenGL(win, glCtx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    int keysW = 0, keysH = 0;
    GLuint keysTex = hasKeysPng ? LoadPngTexture(keysPngPath, keysW, keysH) : 0;

    int   selCard       = 0; // 0 = M1TP2, 1 = War College
    bool  showKeysModal = false;
    float keysZoom      = 0.0f; // 0.0 = Fit to Window, >0 = manual scale factor (1.0 = native 1920x1080)
    int   launchChoice  = -1; // -1=none, 0=M1TP2, 1=WarCollege, 2=Quit
    std::string statusNote;

    auto updater = std::make_shared<UpdaterState>();
    if (cfg.checkForUpdates) {
        StartUpdateCheck(updater);
    }

    // Live SDL3 Joystick / HOTAS tester & binder state
    struct JoyEntry {
        SDL_JoystickID id = 0;
        std::string    name;
    };
    std::vector<JoyEntry> joyDevices;
    SDL_Joystick*         activeJoy      = nullptr;
    SDL_JoystickID        activeJoyId    = 0;
    int                   bindingBtnSlot = -1; // -1=none, 0..3 = waiting for button press for M1TP2 Btn 1..4
    bool                  prevBtnState[32] = {};

    auto RefreshJoyList = [&]() {
        joyDevices.clear();
        int cnt = 0;
        if (SDL_JoystickID* ids = SDL_GetJoysticks(&cnt)) {
            for (int i = 0; i < cnt; ++i) {
                const char* nm = SDL_GetJoystickNameForID(ids[i]);
                joyDevices.push_back({ ids[i], nm ? nm : ("Joystick #" + std::to_string(i + 1)) });
            }
            SDL_free(ids);
        }
        // Pick matching joystick to open for live testing
        SDL_JoystickID wantId = 0;
        if (!joyDevices.empty()) {
            bool isAuto = (cfg.joyDeviceName.empty() || _stricmp(cfg.joyDeviceName.c_str(), "Auto") == 0);
            if (!isAuto) {
                for (const auto& d : joyDevices) {
                    if (_stricmp(d.name.c_str(), cfg.joyDeviceName.c_str()) == 0) {
                        wantId = d.id;
                        break;
                    }
                }
            }
            if (wantId == 0) wantId = joyDevices[0].id;
        }
        if (wantId != activeJoyId) {
            if (activeJoy) { SDL_CloseJoystick(activeJoy); activeJoy = nullptr; }
            activeJoyId = 0;
            if (wantId != 0) {
                activeJoy = SDL_OpenJoystick(wantId);
                if (activeJoy) activeJoyId = wantId;
            }
            std::memset(prevBtnState, 0, sizeof(prevBtnState));
        }
    };
    RefreshJoyList();

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) {
                launchChoice = 2;
                running = false;
            }
            if (ev.type == SDL_EVENT_JOYSTICK_ADDED || ev.type == SDL_EVENT_JOYSTICK_REMOVED) {
                RefreshJoyList();
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE) {
                    if (bindingBtnSlot >= 0) {
                        bindingBtnSlot = -1;
                    } else if (showKeysModal) {
                        showKeysModal = false;
                    } else {
                        launchChoice = 2;
                        running = false;
                    }
                } else if (!showKeysModal && ev.key.scancode == SDL_SCANCODE_LEFT) {
                    selCard = 0;
                } else if (!showKeysModal && ev.key.scancode == SDL_SCANCODE_RIGHT) {
                    selCard = 1;
                } else if (!showKeysModal &&
                           (ev.key.scancode == SDL_SCANCODE_RETURN || ev.key.scancode == SDL_SCANCODE_KP_ENTER)) {
                    if ((selCard == 0 && hasGame) || (selCard == 1 && hasWarCollege)) {
                        launchChoice = selCard;
                        running = false;
                    }
                }
            }
        }

        int winW = 1080, winH = 790;
        SDL_GetWindowSizeInPixels(win, &winW, &winH);
        glViewport(0, 0, winW, winH);
        glClearColor(0.045f, 0.065f, 0.095f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();

        // Dynamically scale the entire launcher proportionally with window size
        const float dispW = std::max(320.0f, io.DisplaySize.x);
        const float dispH = std::max(240.0f, io.DisplaySize.y);
        const float s = std::clamp(std::min(dispW / 1120.0f, dispH / 860.0f), 0.78f, 2.50f);
        auto S = [s](float v) { return v * s; };

        ImGuiStyle& curStyle = ImGui::GetStyle();
        curStyle = baseStyle;
        curStyle.ScaleAllSizes(s);
        io.FontGlobalScale = baseFontScale * s;

        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(dispW, dispH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(24.0f), S(18.0f)));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##M1TP2NativeLauncher", nullptr, flags)) {
            const float availW        = ImGui::GetContentRegionAvail().x;
            // Full-width layout for Quickstart Keyboard viewer; proportionally scaled 1020*s layout for main launcher
            const float contentW      = showKeysModal ? availW : std::min(availW, S(1060.0f));
            const float leftPad       = std::max(0.0f, (availW - contentW) * 0.5f);
            if (leftPad > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftPad);
            const float contentRightX = ImGui::GetCursorPosX() + contentW;

            ImGui::BeginGroup();

            // Header Banner
            ImGui::SetWindowFontScale(1.50f);
            ImGui::TextColored(ImVec4(0.30f, 0.95f, 0.50f, 1.0f), "M1 TANK PLATOON II");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.90f, 0.94f, 0.98f, 1.0f), "NATIVE LAUNCHER");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SameLine(0.0f, S(10.0f));
            ImGui::TextColored(ImVec4(0.55f, 0.66f, 0.78f, 1.0f), "v%s", kLauncherVersion);

            // Non-intrusive GitHub Release Auto-Update Banner (top-right of header)
            {
                auto uPhase = static_cast<UpdatePhase>(updater->phase.load());
                if (uPhase == UpdatePhase::ReadyToInstall) {
                    std::string exePath;
                    {
                        std::lock_guard<std::mutex> lk(updater->mtx);
                        exePath = updater->downloadedExe;
                    }
                    SaveIni(iniPath, cfg);
                    SaveStockGameCfg(stockCfgPath, stockCfg);
                    char cmdBuf[MAX_PATH * 4];
                    std::snprintf(cmdBuf, sizeof(cmdBuf), "\"%s\" /S /W=%lu /R /D=\"%s\"",
                                  exePath.c_str(), static_cast<unsigned long>(GetCurrentProcessId()), dir.c_str());
                    STARTUPINFOA si{};
                    PROCESS_INFORMATION pi{};
                    si.cb = sizeof(si);
                    if (CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                        CloseHandle(pi.hThread);
                        CloseHandle(pi.hProcess);
                        launchChoice = 2;
                        running = false;
                    } else {
                        std::lock_guard<std::mutex> lk(updater->mtx);
                        updater->errorMsg = "Failed to launch updater executable";
                        updater->phase.store(static_cast<int>(UpdatePhase::Error));
                    }
                } else if (uPhase == UpdatePhase::Available) {
                    std::string tag, relUrl;
                    {
                        std::lock_guard<std::mutex> lk(updater->mtx);
                        tag = updater->latestTag;
                        relUrl = updater->releaseUrl;
                    }
                    float updBtnW   = S(165.0f);
                    float notesBtnW = S(84.0f);
                    float rightW    = updBtnW + notesBtnW + S(8.0f);
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + S(10.0f), contentRightX - rightW));
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.56f, 0.28f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.68f, 0.35f, 1.0f));
                    char lbl[64];
                    std::snprintf(lbl, sizeof(lbl), "Update to %s", tag.c_str());
                    if (ImGui::Button(lbl, ImVec2(updBtnW, S(28.0f)))) {
                        StartUpdateDownload(updater);
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::SameLine(0.0f, S(8.0f));
                    if (ImGui::Button("Notes", ImVec2(notesBtnW, S(28.0f))) && !relUrl.empty()) {
                        ShellExecuteA(nullptr, "open", relUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                } else if (uPhase == UpdatePhase::Downloading || uPhase == UpdatePhase::Verifying) {
                    uint64_t got = updater->dlBytes.load();
                    uint64_t tot = updater->dlTotal.load();
                    float frac = (uPhase == UpdatePhase::Verifying) ? 1.0f
                               : ((tot > 0) ? static_cast<float>(static_cast<double>(got) / static_cast<double>(tot)) : 0.0f);
                    char ovl[64];
                    if (uPhase == UpdatePhase::Verifying) {
                        std::snprintf(ovl, sizeof(ovl), "Verifying SHA-256...");
                    } else if (tot > 0) {
                        std::snprintf(ovl, sizeof(ovl), "Downloading %.1f / %.1f MB",
                                      got / (1024.0 * 1024.0), tot / (1024.0 * 1024.0));
                    } else {
                        std::snprintf(ovl, sizeof(ovl), "Downloading %.1f MB...", got / (1024.0 * 1024.0));
                    }
                    float barW = S(245.0f);
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + S(10.0f), contentRightX - barW));
                    ImGui::ProgressBar(frac, ImVec2(barW, S(26.0f)), ovl);
                }
            }

            ImGui::TextColored(ImVec4(0.64f, 0.72f, 0.82f, 1.0f),
                "SDL3 + OpenGL 3.3 HD Glide  |  True Hor+ Widescreen  |  Precision Gunnery & FCS  |  F12 In-Game Overlay");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Show Quickstart Keyboard Layout Full Viewer OR Main Launcher Body
            if (showKeysModal) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "QUICKSTART KEYBOARD & GUNNERY LAYOUT");

                ImGui::SameLine(0.0f, S(18.0f));
                if (ImGui::Button("Fit",  ImVec2(S(52.0f), S(28.0f)))) keysZoom = 0.0f;
                ImGui::SameLine(0.0f, S(6.0f));
                if (ImGui::Button("100%", ImVec2(S(58.0f), S(28.0f)))) keysZoom = 1.0f;
                ImGui::SameLine(0.0f, S(6.0f));
                if (ImGui::Button("125%", ImVec2(S(58.0f), S(28.0f)))) keysZoom = 1.25f;
                ImGui::SameLine(0.0f, S(6.0f));
                if (ImGui::Button("150%", ImVec2(S(58.0f), S(28.0f)))) keysZoom = 1.5f;

                float btnPngW  = S(175.0f);
                float btnBackW = S(155.0f);
                float rightBtnsW = hasKeysPng ? (btnPngW + btnBackW + S(10.0f)) : btnBackW;
                float targetX = std::max(ImGui::GetCursorPosX() + S(12.0f), contentRightX - rightBtnsW);
                ImGui::SameLine();
                ImGui::SetCursorPosX(targetX);
                if (hasKeysPng && ImGui::Button("Open PNG Externally", ImVec2(btnPngW, S(28.0f)))) {
                    ShellExecuteA(nullptr, "open", keysPngPath.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                }
                if (hasKeysPng) ImGui::SameLine(0.0f, S(10.0f));
                if (ImGui::Button("Back to Launcher (Esc)", ImVec2(btnBackW, S(28.0f)))) {
                    showKeysModal = false;
                }
                ImGui::Spacing();

                ImVec2 viewAvail = ImGui::GetContentRegionAvail();
                float viewerW = std::max(S(320.0f), viewAvail.x);
                float viewerH = std::max(S(220.0f), viewAvail.y);
                if (keysTex && keysW > 0 && keysH > 0) {
                    ImGuiWindowFlags childFlags = (keysZoom > 0.0f) ? ImGuiWindowFlags_HorizontalScrollbar : 0;
                    ImGui::BeginChild("##KeysImageScroll", ImVec2(viewerW, viewerH), ImGuiChildFlags_None, childFlags);
                    ImVec2 inner = ImGui::GetContentRegionAvail();
                    float aspect = static_cast<float>(keysW) / static_cast<float>(keysH);
                    float drawW, drawH;
                    if (keysZoom <= 0.0f) {
                        drawW = inner.x;
                        drawH = drawW / aspect;
                        if (drawH > inner.y) {
                            drawH = inner.y;
                            drawW = drawH * aspect;
                        }
                    } else {
                        drawW = static_cast<float>(keysW) * keysZoom;
                        drawH = static_cast<float>(keysH) * keysZoom;
                    }
                    float padX = std::max(0.0f, (inner.x - drawW) * 0.5f);
                    float padY = std::max(0.0f, (inner.y - drawH) * 0.5f);
                    if (padX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
                    if (padY > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);
                    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(keysTex)), ImVec2(drawW, drawH));
                    ImGui::EndChild();
                } else {
                    ImGui::TextColored(ImVec4(0.98f, 0.76f, 0.30f, 1.0f),
                        "docs\\M1TP2_Quickstart_Keys.png not found. Essential Keys:");
                    ImGui::Spacing();
                    ImGui::Text("VIEWS:   F1=TC Pit | F2=Gnr Pit | F3=IVIS Map | F4=GPS Sight | F5=Aux | F6=VisBlk | F7=TC Hatch | F8=Chase");
                    ImGui::Text("TANKS:   Shift+F1..F4 = Jump to Tank #1..#4 | F9/F10 = Prev/Next Vehicle");
                    ImGui::Text("GUNNERY: 1=Sabot 2=HEAT 3=MPAT 4=STAFF 5=Coax | Space=Lase | Enter/LMB=Fire | T=Thermal | Z=10x X=3x");
                    ImGui::Text("DRIVE:   Arrow Keys=Drive | A=Auto-Drive | S=Stop | C/V/B=1/3, 2/3, Max Speed | Shift+G=Hull to Gun");
                    ImGui::Text("RECOMP:  F12=In-Game Overlay | F11=2D Filter | Alt+Enter=Fullscreen");
                }
            } else {
                // 2 Side-by-Side Launch Cards (matching Falcon 3.0 Trilogy Launcher)
                struct CardDef {
                    const char* badge;
                    const char* title;
                    const char* sub;
                    const char* desc;
                    const char* bullets;
                    bool        ready;
                    ImVec4      accent;
                };
                CardDef cards[2] = {
                    {
                        "US ARMY  |  M1A2 ABRAMS",
                        "M1 TANK PLATOON II",
                        "M1TP2_bin.exe  (Native SDL3 + OpenGL 3.3 Runtime)",
                        "Full Platoon Combat, Dynamic Campaigns & Battleplanner",
                        "- HD OpenGL 3.3 Glide (MSAA, 16x AF, up to 6km LOD)\n"
                        "- Smart True Hor+ Widescreen (4:3 Sights & Cockpits)\n"
                        "- Precision Gunner Mouse & Smooth FCS Broadside Lead Fix\n"
                        "- 1998 Campaign AI-Front Fix, Live Intel HUD & mods/ VFS",
                        hasGame,
                        ImVec4(0.28f, 0.92f, 0.48f, 1.0f)
                    },
                    {
                        "ARMOR SCHOOL  |  ENCYCLOPEDIA",
                        "WAR COLLEGE (DSG)",
                        "Dsg\\OMOPLAY.EXE  (Auto otvdm / GOG BoxedWine)",
                        "Official U.S. Army, USMC & OPFOR Combat Doctrine & VRG",
                        "- Full Vehicle Recognition Guides (NATO & OPFOR)\n"
                        "- Armor Platoon Tactics, Formations & Fire Support\n"
                        "- Theater Intelligence Briefings for Every Campaign\n"
                        "- Launches cleanly on 64-bit Windows with zero console flash",
                        hasWarCollege,
                        ImVec4(0.36f, 0.72f, 1.00f, 1.0f)
                    }
                };

                const float cardGap = S(16.0f);
                const float cardW   = (contentW - cardGap) * 0.5f;
                const float cardH   = S(194.0f);

                for (int i = 0; i < 2; ++i) {
                    if (i > 0) ImGui::SameLine(0.0f, cardGap);
                    const auto& cd = cards[i];
                    bool isSel = (selCard == i);

                    ImVec4 bg     = isSel ? ImVec4(0.10f, 0.16f, 0.22f, 1.0f) : ImVec4(0.072f, 0.102f, 0.145f, 1.0f);
                    ImVec4 border = isSel ? cd.accent : ImVec4(0.22f, 0.28f, 0.36f, 0.85f);
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
                    ImGui::PushStyleColor(ImGuiCol_Border,  border);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSel ? S(2.5f) : S(1.0f));

                    char cid[32];
                    std::snprintf(cid, sizeof(cid), "##LaunchCard_%d", i);
                    ImGui::BeginChild(cid, ImVec2(cardW, cardH), ImGuiChildFlags_Borders);

                    ImGui::SetCursorPos(ImVec2(S(14.0f), S(12.0f)));
                    ImGui::TextColored(cd.accent, "[%s]", cd.badge);
                    const char* stTxt = cd.ready ? "READY" : "NOT FOUND";
                    float stW = ImGui::CalcTextSize(stTxt).x;
                    ImGui::SameLine(cardW - stW - S(14.0f));
                    ImGui::TextColored(cd.ready ? ImVec4(0.30f, 0.95f, 0.48f, 1.0f) : ImVec4(0.90f, 0.35f, 0.32f, 1.0f),
                                       "%s", stTxt);

                    ImGui::SetCursorPosX(S(14.0f));
                    ImGui::SetWindowFontScale(1.24f);
                    ImGui::TextColored(cd.ready ? ImVec4(0.96f, 0.98f, 1.0f, 1.0f) : ImVec4(0.55f, 0.58f, 0.62f, 1.0f),
                                       "%s", cd.title);
                    ImGui::SetWindowFontScale(1.0f);

                    ImGui::SetCursorPosX(S(14.0f));
                    ImGui::TextColored(ImVec4(0.90f, 0.80f, 0.42f, 1.0f), "%s", cd.sub);
                    ImGui::Spacing();
                    ImGui::SetCursorPosX(S(14.0f));
                    ImGui::TextColored(ImVec4(0.78f, 0.85f, 0.92f, 1.0f), "%s", cd.desc);
                    ImGui::Spacing();
                    ImGui::SetCursorPosX(S(14.0f));
                    ImGui::TextColored(ImVec4(0.62f, 0.70f, 0.80f, 1.0f), "%s", cd.bullets);

                    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
                    char bid[32];
                    std::snprintf(bid, sizeof(bid), "##CardHit_%d", i);
                    if (ImGui::InvisibleButton(bid, ImVec2(cardW, cardH))) selCard = i;
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && cd.ready) {
                        selCard = i;
                        launchChoice = i;
                        running = false;
                    }

                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                }

                ImGui::Spacing();

                // Manuals & Quickstart Reference Panel
                ImGui::BeginChild("##DocsPanel", ImVec2(contentW, S(74.0f)), ImGuiChildFlags_Borders);
                ImGui::SetCursorPos(ImVec2(S(14.0f), S(7.0f)));
                ImGui::TextColored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "MANUALS & QUICKSTART REFERENCE");

                ImGui::SetCursorPos(ImVec2(S(14.0f), S(32.0f)));
                const float btnH = S(30.0f);
                const float docInnerW = contentW - S(28.0f) - S(40.0f); // 4 gaps of S(10)
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.36f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.38f, 1.0f));
                if (ImGui::Button("Quickstart Keyboard Layout", ImVec2(docInnerW * 0.245f, btnH))) {
                    showKeysModal = true;
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine(0.0f, S(10.0f));
                if (!hasManualPdf) ImGui::BeginDisabled();
                if (ImGui::Button("Official Game Manual (PDF)", ImVec2(docInnerW * 0.235f, btnH))) {
                    std::string p = dir + "\\M1_Tank_Platoon_II_Manual.pdf";
                    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened M1_Tank_Platoon_II_Manual.pdf";
                }
                if (!hasManualPdf) ImGui::EndDisabled();

                ImGui::SameLine(0.0f, S(10.0f));
                if (!hasPatchTxt) ImGui::BeginDisabled();
                if (ImGui::Button("v1.2 Patch Notes", ImVec2(docInnerW * 0.165f, btnH))) {
                    std::string p = dir + "\\M1TP2Readme.txt";
                    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened M1TP2Readme.txt";
                }
                if (!hasPatchTxt) ImGui::EndDisabled();

                ImGui::SameLine(0.0f, S(10.0f));
                if (!hasReadmeTxt) ImGui::BeginDisabled();
                if (ImGui::Button("Original ReadMe", ImVec2(docInnerW * 0.165f, btnH))) {
                    std::string p = dir + "\\ReadMe.txt";
                    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened ReadMe.txt";
                }
                if (!hasReadmeTxt) ImGui::EndDisabled();

                ImGui::SameLine(0.0f, S(10.0f));
                if (ImGui::Button("Open Mods Folder", ImVec2(docInnerW * 0.190f, btnH))) {
                    std::string mp = dir + "\\mods";
                    CreateDirectoryA(mp.c_str(), nullptr);
                    ShellExecuteA(nullptr, "open", mp.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened mods\\ folder";
                }
                ImGui::EndChild();

                ImGui::Spacing();

                // Comprehensive Pre-Launch Settings Panel (synced 2-way with m1tp2_recomp.ini & game.cfg)
                ImGui::BeginChild("##QuickSettingsPanel", ImVec2(contentW, S(366.0f)), ImGuiChildFlags_Borders);

                auto pctSlider = [](const char* label, float& v, float w) {
                    int pct = std::clamp(static_cast<int>(std::lround(v * 100.0f)), 0, 100);
                    ImGui::SetNextItemWidth(w);
                    if (ImGui::SliderInt(label, &pct, 0, 100, "%d%%")) {
                        v = static_cast<float>(pct) / 100.0f;
                    }
                };

                if (ImGui::BeginTabBar("##LauncherSettingsTabs")) {
                    if (ImGui::BeginTabItem("  Quick Launch, Audio & Gunnery  ")) {
                        ImGui::Spacing();
                        const float col1W = contentW * 0.195f;
                        const float col2W = contentW * 0.190f;
                        const float baseY = ImGui::GetCursorPosY();

                        // Column 1: Graphics & Widescreen
                        ImGui::SetCursorPos(ImVec2(S(14.0f), baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.88f, 0.82f, 0.45f, 1.0f), "Graphics & Widescreen");
                        const char* asp3d[] = { "Smart Hor+ (Auto)", "All 3D Hor+", "4:3 Original", "Stretch Fill" };
                        ImGui::SetNextItemWidth(col1W);
                        ImGui::Combo("3D Aspect", &cfg.aspectMode, asp3d, 4);

                        const int mapVals[] = { 0, 2, 3, 4 };
                        const char* mapAsp[] = { "Auto Native (4:3)", "16:10 Wide", "16:9 Wide", "Stretch Fill" };
                        int mapIdx = 0;
                        for (int i = 0; i < 4; ++i) if (mapVals[i] == cfg.mapAspectMode) mapIdx = i;
                        ImGui::SetNextItemWidth(col1W);
                        if (ImGui::Combo("Map Aspect", &mapIdx, mapAsp, 4)) cfg.mapAspectMode = mapVals[mapIdx];

                        const char* drawOpts[] = { "2.0 km (Stock)", "3.0 km (2x LOD)", "4.0 km (3x LOD)", "6.0 km (4x LOD)" };
                        ImGui::SetNextItemWidth(col1W);
                        ImGui::Combo("Draw Dist", &cfg.drawDistMode, drawOpts, 4);

                        const int msaaVals[] = { 0, 2, 4, 8 };
                        const char* msaaLbls[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
                        int msaaIdx = 0;
                        for (int i = 0; i < 4; ++i) if (msaaVals[i] == cfg.msaaSamples) msaaIdx = i;
                        ImGui::SetNextItemWidth(col1W);
                        if (ImGui::Combo("MSAA", &msaaIdx, msaaLbls, 4)) cfg.msaaSamples = msaaVals[msaaIdx];

                        const int afVals[] = { 1, 2, 4, 8, 16 };
                        const char* afLbls[] = { "Off (1x)", "2x AF", "4x AF", "8x AF", "16x AF" };
                        int afIdx = 0;
                        for (int i = 0; i < 5; ++i) if (afVals[i] == cfg.anisotropy) afIdx = i;
                        ImGui::SetNextItemWidth(col1W);
                        if (ImGui::Combo("Anisotropy", &afIdx, afLbls, 5)) cfg.anisotropy = afVals[afIdx];

                        const char* fogOpts[] = { "Auto (Match Draw)", "Extended 2x", "Extended 4x", "Off (Clear)" };
                        ImGui::SetNextItemWidth(col1W);
                        ImGui::Combo("3D Fog", &cfg.fogMode, fogOpts, 4);

                        ImGui::SetNextItemWidth(col1W);
                        ImGui::SliderFloat("3D Gamma", &cfg.gamma, 0.70f, 1.60f, "%.2f");
                        ImGui::EndGroup();

                        // Column 2: Audio Mixer & Gunnery FCS
                        ImGui::SetCursorPos(ImVec2(contentW * 0.340f, baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.45f, 0.86f, 1.00f, 1.0f), "Audio Mixer");
                        ImGui::SameLine(0.0f, S(14.0f));
                        ImGui::Checkbox("Mute All", &cfg.audioMute);
                        if (cfg.audioMute) ImGui::BeginDisabled();
                        pctSlider("Master",      cfg.audioMasterVol, col2W);
                        pctSlider("Music",       cfg.audioMusicVol,  col2W);
                        pctSlider("Weapons/SFX", cfg.audioSfxVol,    col2W);
                        pctSlider("Engines",     cfg.audioEngineVol, col2W);
                        pctSlider("Crew Voice",  cfg.audioVoiceVol,  col2W);
                        if (cfg.audioMute) ImGui::EndDisabled();

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.98f, 0.65f, 0.36f, 1.0f), "Gunnery & Ballistic FCS");
                        const char* mouseModes[] = { "Precision Stick", "Direct Raw", "Stock 1998" };
                        ImGui::SetNextItemWidth(col2W);
                        ImGui::Combo("Gunner Aim", &cfg.gunnerMouseMode, mouseModes, 3);
                        ImGui::SetNextItemWidth(col2W);
                        ImGui::SliderFloat("Mouse Sens", &cfg.gunnerMouseSens, 0.25f, 3.00f, "%.2fx");
                        const char* leadModes[] = { "Smooth FCS", "Manual Lead", "Stock 1998" };
                        ImGui::SetNextItemWidth(col2W);
                        ImGui::Combo("Broadside FCS", &cfg.ballisticLeadMode, leadModes, 3);
                        ImGui::EndGroup();

                        // Column 3: Runtime, Input & Campaign Toggles
                        ImGui::SetCursorPos(ImVec2(contentW * 0.680f, baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.45f, 0.94f, 0.62f, 1.0f), "Runtime & Campaign");
                        ImGui::Checkbox("Borderless Fullscreen", &cfg.fullscreen);
                        ImGui::Checkbox("VSync", &cfg.vsync);
                        ImGui::SameLine();
                        ImGui::Checkbox("100 FPS Mode", &cfg.unlock100Fps);
                        ImGui::Checkbox("Fast-Boot (Skip Intros)", &cfg.skipIntro);
                        ImGui::Checkbox("Show Launcher on Startup", &cfg.showLauncherOnStartup);
                        ImGui::Checkbox("Fix 1998 Campaign AI Bug", &cfg.fixCampaignFrontBug);
                        ImGui::Checkbox("Campaign Intel Sidebar", &cfg.campaignIntelHud);
                        ImGui::Checkbox("Enable mods/ VFS Loader", &cfg.enableMods);
                        ImGui::Checkbox("Disable Joystick / Gamepad", &cfg.disableJoystick);
                        ImGui::Checkbox("Show FPS / Stats HUD", &cfg.showStatsHud);
                        if (stockCfg.loaded) {
                            bool mus = (stockCfg.menuMusicOn != 0);
                            if (ImGui::Checkbox("Menu Music (game.cfg)", &mus)) {
                                stockCfg.menuMusicOn = mus ? 1 : 0;
                                stockCfg.dirty = true;
                            }
                        }
                        ImGui::EndGroup();

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("  Advanced Graphics, Filters & game.cfg  ")) {
                        ImGui::Spacing();
                        const float colW = contentW * 0.195f;
                        const float baseY = ImGui::GetCursorPosY();

                        // Column 1: Advanced 2D/3D Filtering & Window
                        ImGui::SetCursorPos(ImVec2(S(14.0f), baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.88f, 0.82f, 0.45f, 1.0f), "Advanced Filters & Display");
                        const char* menuAsp[] = { "Fill Screen", "4:3 Original (Rec.)", "16:10 Wide", "Crop Zoom" };
                        ImGui::SetNextItemWidth(colW);
                        ImGui::Combo("2D Menu Aspect", &cfg.menuAspectMode, menuAsp, 4);

                        const char* texFilt[] = { "Trilinear (Mipmaps)", "Bilinear", "Nearest (Retro)", "Game Default" };
                        ImGui::SetNextItemWidth(colW);
                        ImGui::Combo("3D Tex Filter", &cfg.texFilter3D, texFilt, 4);

                        ImGui::Checkbox("2D Menu Bilinear Filter (F11)", &cfg.linearFilter2D);
                        ImGui::Checkbox("3D Cockpit / HUD Bilinear Filter", &cfg.linearFilterLfb);
                        ImGui::Checkbox("Force Native Glide on D3D", &cfg.forceGlideOnD3D);

                        ImGui::Spacing();
                        ImGui::TextDisabled("Custom Window Size (0 = Native)");
                        ImGui::SetNextItemWidth(colW * 0.6f);
                        ImGui::InputInt("Width", &cfg.windowWidth, 0, 0);
                        cfg.windowWidth = std::clamp(cfg.windowWidth, 0, 7680);
                        ImGui::SetNextItemWidth(colW * 0.6f);
                        ImGui::InputInt("Height", &cfg.windowHeight, 0, 0);
                        cfg.windowHeight = std::clamp(cfg.windowHeight, 0, 4320);
                        ImGui::EndGroup();

                        // Column 2: Original M1TP2 game.cfg
                        ImGui::SetCursorPos(ImVec2(contentW * 0.365f, baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.45f, 0.86f, 1.00f, 1.0f), "Original Game Config (game.cfg)");
                        if (stockCfg.loaded) {
                            bool mus = (stockCfg.menuMusicOn != 0);
                            if (ImGui::Checkbox("Menu Music (game.cfg)", &mus)) {
                                stockCfg.menuMusicOn = mus ? 1 : 0; stockCfg.dirty = true;
                            }
                            bool hiDet = (stockCfg.detailHigh != 0);
                            if (ImGui::Checkbox("High 3D Detail Level", &hiDet)) {
                                stockCfg.detailHigh = hiDet ? 1 : 0; stockCfg.dirty = true;
                            }
                            bool alpha = (stockCfg.alphaBlend != 0);
                            if (ImGui::Checkbox("Alpha Blending (Smoke/FX)", &alpha)) {
                                stockCfg.alphaBlend = alpha ? 1 : 0; stockCfg.dirty = true;
                            }
                            const char* rends[] = { "Software (2D)", "Glide HD (Recommended)", "Direct3D (Auto->Glide)" };
                            ImGui::SetNextItemWidth(colW);
                            if (ImGui::Combo("Renderer", &stockCfg.renderMode, rends, 3)) {
                                stockCfg.dirty = true;
                            }
                            ImGui::SetNextItemWidth(colW * 0.85f);
                            if (ImGui::InputText("Player Name", stockCfg.playerName, sizeof(stockCfg.playerName))) {
                                stockCfg.dirty = true;
                            }
                            ImGui::SetNextItemWidth(colW * 0.85f);
                            if (ImGui::InputText("Host Name", stockCfg.hostName, sizeof(stockCfg.hostName))) {
                                stockCfg.dirty = true;
                            }
                        } else {
                            ImGui::TextDisabled("game.cfg not found yet (created on first run).");
                        }
                        ImGui::EndGroup();

                        // Column 3: Updates, Maintenance & Logs
                        ImGui::SetCursorPos(ImVec2(contentW * 0.710f, baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.45f, 0.94f, 0.62f, 1.0f), "Updates, Maintenance & Logs");
                        const float mBtnW = contentW * 0.255f;
                        const float mBtnH = S(28.0f);
                        ImGui::Checkbox("Check for Updates on Startup", &cfg.checkForUpdates);
                        {
                            auto up = static_cast<UpdatePhase>(updater->phase.load());
                            bool busy = (up == UpdatePhase::Checking || up == UpdatePhase::Downloading || up == UpdatePhase::Verifying);
                            if (busy) ImGui::BeginDisabled();
                            if (ImGui::Button("Check for Updates Now", ImVec2(mBtnW, mBtnH))) {
                                StartUpdateCheck(updater);
                            }
                            if (busy) ImGui::EndDisabled();

                            std::lock_guard<std::mutex> lk(updater->mtx);
                            if (up == UpdatePhase::Checking) {
                                ImGui::TextColored(ImVec4(0.65f, 0.82f, 0.98f, 1.0f), "Checking GitHub Releases...");
                            } else if (up == UpdatePhase::UpToDate) {
                                ImGui::TextColored(ImVec4(0.42f, 0.95f, 0.55f, 1.0f), "Up to date (v%s)", kLauncherVersion);
                            } else if (up == UpdatePhase::Available) {
                                ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.32f, 1.0f), "Update %s ready (click top-right)", updater->latestTag.c_str());
                            } else if (up == UpdatePhase::Error) {
                                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "Update check: %s", updater->errorMsg.c_str());
                            }
                        }
                        ImGui::Spacing();
                        if (ImGui::Button("Reset Audio Mixer to 100%", ImVec2(mBtnW, mBtnH))) {
                            cfg.audioMute = false;
                            cfg.audioMasterVol = cfg.audioMusicVol = cfg.audioSfxVol = 1.0f;
                            cfg.audioEngineVol = cfg.audioVoiceVol = 1.0f;
                            statusNote = "Audio Mixer reset to 100%";
                        }
                        if (ImGui::Button("Reset All Recomp Defaults", ImVec2(mBtnW, mBtnH))) {
                            cfg = LauncherCfg{};
                            statusNote = "Restored default m1tp2_recomp.ini settings";
                        }
                        std::string logPath   = dir + "\\m1tp2_recomp.log";
                        std::string crashPath = dir + "\\m1tp2_crash.log";
                        bool hasLog   = FileExists(logPath);
                        bool hasCrash = FileExists(crashPath);
                        if (!hasLog) ImGui::BeginDisabled();
                        if (ImGui::Button("Open Runtime Log", ImVec2(mBtnW, mBtnH))) {
                            ShellExecuteA(nullptr, "open", logPath.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                            statusNote = "Opened m1tp2_recomp.log";
                        }
                        if (!hasLog) ImGui::EndDisabled();
                        if (!hasCrash) ImGui::BeginDisabled();
                        if (ImGui::Button("Open Crash Log", ImVec2(mBtnW, mBtnH))) {
                            ShellExecuteA(nullptr, "open", crashPath.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                            statusNote = "Opened m1tp2_crash.log";
                        }
                        if (!hasCrash) ImGui::EndDisabled();
                        ImGui::EndGroup();

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("  Joystick & HOTAS Tester / Binder  ")) {
                        ImGui::Spacing();
                        SDL_UpdateJoysticks();
                        if (activeJoy && !SDL_JoystickConnected(activeJoy)) {
                            RefreshJoyList();
                        }

                        const float baseY = ImGui::GetCursorPosY();

                        // Helper to read normalized axis [-1, +1] with deadzone & invert
                        auto readNormAxis = [&](int axisIdx, bool inv, bool applyDz) -> float {
                            if (!activeJoy || axisIdx < 0 || axisIdx >= SDL_GetNumJoystickAxes(activeJoy)) return 0.0f;
                            int16_t raw = SDL_GetJoystickAxis(activeJoy, axisIdx);
                            float v = (raw >= 0) ? (raw / 32767.0f) : (raw / 32768.0f);
                            if (inv) v = -v;
                            if (applyDz) {
                                float dz = std::clamp(cfg.joyDeadzone, 0.0f, 0.40f);
                                float a = std::fabs(v);
                                if (a <= dz) return 0.0f;
                                if (dz > 0.001f) v = (v >= 0.0f ? 1.0f : -1.0f) * ((a - dz) / (1.0f - dz));
                            }
                            return std::clamp(v, -1.0f, 1.0f);
                        };

                        // Capture next physical button press if user clicked "Bind..."
                        if (bindingBtnSlot >= 0 && activeJoy) {
                            int nb = std::min(SDL_GetNumJoystickButtons(activeJoy), 32);
                            for (int b = 0; b < nb; ++b) {
                                bool down = SDL_GetJoystickButton(activeJoy, b);
                                if (down && !prevBtnState[b]) {
                                    int* targets[4] = { &cfg.joyBtn1, &cfg.joyBtn2, &cfg.joyBtn3, &cfg.joyBtn4 };
                                    *targets[bindingBtnSlot] = b;
                                    statusNote = "Bound M1TP2 Button " + std::to_string(bindingBtnSlot + 1) +
                                                 " -> Stick Btn " + std::to_string(b + 1);
                                    bindingBtnSlot = -1;
                                    break;
                                }
                            }
                        }
                        if (activeJoy) {
                            int nb = std::min(SDL_GetNumJoystickButtons(activeJoy), 32);
                            for (int b = 0; b < nb; ++b) prevBtnState[b] = SDL_GetJoystickButton(activeJoy, b);
                        }

                        // Column 1: Device Picker & Live 2D Visual Crosshair Tester
                        ImGui::SetCursorPos(ImVec2(S(14.0f), baseY));
                        ImGui::BeginGroup();
                        bool joyEn = !cfg.disableJoystick;
                        if (ImGui::Checkbox("Enable Joystick / HOTAS (SDL3)", &joyEn)) {
                            cfg.disableJoystick = !joyEn;
                        }
                        ImGui::SameLine(0.0f, S(8.0f));
                        if (ImGui::Button("Refresh", ImVec2(S(72.0f), 0.0f))) {
                            RefreshJoyList();
                            statusNote = joyDevices.empty() ? "No joysticks detected" : ("Detected " + std::to_string(joyDevices.size()) + " device(s)");
                        }

                        // Device dropdown
                        std::string preview = (cfg.joyDeviceName.empty() || _stricmp(cfg.joyDeviceName.c_str(), "Auto") == 0)
                            ? (joyDevices.empty() ? "No Stick Detected" : ("Auto (" + joyDevices[0].name + ")"))
                            : cfg.joyDeviceName;
                        ImGui::SetNextItemWidth(contentW * 0.31f);
                        if (ImGui::BeginCombo("##JoyDevCombo", preview.c_str())) {
                            bool isAuto = (cfg.joyDeviceName.empty() || _stricmp(cfg.joyDeviceName.c_str(), "Auto") == 0);
                            if (ImGui::Selectable("Auto (First Detected)", isAuto)) {
                                cfg.joyDeviceName = "Auto";
                                RefreshJoyList();
                            }
                            for (const auto& d : joyDevices) {
                                bool sel = (!isAuto && _stricmp(cfg.joyDeviceName.c_str(), d.name.c_str()) == 0);
                                if (ImGui::Selectable(d.name.c_str(), sel)) {
                                    cfg.joyDeviceName = d.name;
                                    RefreshJoyList();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::Spacing();
                        // Live 2D X/Y Stick Crosshair Box + Deadzone Ring
                        const float boxSz = S(150.0f);
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        ImVec2 p1 = ImVec2(p0.x + boxSz, p0.y + boxSz);
                        ImVec2 ctr = ImVec2(p0.x + boxSz * 0.5f, p0.y + boxSz * 0.5f);
                        float half = boxSz * 0.46f;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddRectFilled(p0, p1, IM_COL32(14, 22, 30, 255), S(4.0f));
                        dl->AddRect(p0, p1, IM_COL32(60, 120, 95, 220), S(4.0f), 0, S(1.5f));
                        dl->AddLine(ImVec2(ctr.x, p0.y + S(4.0f)), ImVec2(ctr.x, p1.y - S(4.0f)), IM_COL32(45, 75, 65, 180));
                        dl->AddLine(ImVec2(p0.x + S(4.0f), ctr.y), ImVec2(p1.x - S(4.0f), ctr.y), IM_COL32(45, 75, 65, 180));
                        if (cfg.joyDeadzone > 0.005f) {
                            float dzR = half * std::clamp(cfg.joyDeadzone, 0.0f, 0.40f);
                            dl->AddCircleFilled(ctr, dzR, IM_COL32(220, 160, 40, 38), 24);
                            dl->AddCircle(ctr, dzR, IM_COL32(230, 175, 55, 150), 24, S(1.2f));
                        }
                        float rawX = readNormAxis(cfg.joyAxisX, cfg.joyInvertX, false);
                        float rawY = readNormAxis(cfg.joyAxisY, cfg.joyInvertY, false);
                        float outX = readNormAxis(cfg.joyAxisX, cfg.joyInvertX, true);
                        float outY = readNormAxis(cfg.joyAxisY, cfg.joyInvertY, true);
                        // Dim raw dot + bright green deadzone-filtered output dot
                        dl->AddCircleFilled(ImVec2(ctr.x + rawX * half, ctr.y + rawY * half), S(3.5f), IM_COL32(150, 170, 190, 160));
                        dl->AddCircleFilled(ImVec2(ctr.x + outX * half, ctr.y + outY * half), S(5.5f),
                                            activeJoy ? IM_COL32(75, 245, 130, 255) : IM_COL32(100, 110, 120, 180));
                        ImGui::Dummy(ImVec2(boxSz, boxSz));

                        // Live Z / R / POV readout next to crosshair
                        ImGui::SameLine(0.0f, S(12.0f));
                        ImGui::BeginGroup();
                        int nAxes = activeJoy ? SDL_GetNumJoystickAxes(activeJoy) : 0;
                        int nBtns = activeJoy ? SDL_GetNumJoystickButtons(activeJoy) : 0;
                        int nHats = activeJoy ? SDL_GetNumJoystickHats(activeJoy) : 0;
                        ImGui::TextColored(activeJoy ? ImVec4(0.35f, 0.95f, 0.55f, 1.0f) : ImVec4(0.95f, 0.55f, 0.40f, 1.0f),
                                           activeJoy ? "CONNECTED" : "NO DEVICE");
                        ImGui::TextDisabled("%d axes | %d btns | %d hat", nAxes, nBtns, nHats);
                        ImGui::Spacing();
                        float barW = S(115.0f);
                        int showAxes = std::min(nAxes, 4);
                        for (int a = 0; a < showAxes; ++a) {
                            float av = readNormAxis(a, false, false);
                            char albl[24];
                            std::snprintf(albl, sizeof(albl), "A%d: %+.2f", a, av);
                            ImGui::ProgressBar(av * 0.5f + 0.5f, ImVec2(barW, S(17.0f)), albl);
                        }
                        if (nHats > 0) {
                            uint8_t h = SDL_GetJoystickHat(activeJoy, 0);
                            const char* hStr = (h == SDL_HAT_UP) ? "N" : (h == SDL_HAT_RIGHT) ? "E" :
                                               (h == SDL_HAT_DOWN) ? "S" : (h == SDL_HAT_LEFT) ? "W" :
                                               (h == SDL_HAT_RIGHTUP) ? "NE" : (h == SDL_HAT_RIGHTDOWN) ? "SE" :
                                               (h == SDL_HAT_LEFTDOWN) ? "SW" : (h == SDL_HAT_LEFTUP) ? "NW" : "CTR";
                            ImGui::Text("POV Hat: %s", hStr);
                        }
                        ImGui::EndGroup();

                        // Live Button Grid (first 16 buttons)
                        ImGui::Spacing();
                        ImGui::TextDisabled("Live Buttons:");
                        int showBtns = std::clamp(nBtns, 8, 16);
                        for (int b = 0; b < showBtns; ++b) {
                            if (b > 0 && b != 8) ImGui::SameLine(0.0f, S(4.0f));
                            bool down = (activeJoy && b < nBtns && SDL_GetJoystickButton(activeJoy, b));
                            ImGui::PushStyleColor(ImGuiCol_Button, down ? ImVec4(0.20f, 0.78f, 0.36f, 1.0f) : ImVec4(0.11f, 0.16f, 0.22f, 1.0f));
                            char btxt[12];
                            std::snprintf(btxt, sizeof(btxt), "%d", b + 1);
                            ImGui::Button(btxt, ImVec2(S(26.0f), S(20.0f)));
                            ImGui::PopStyleColor();
                        }
                        ImGui::EndGroup();

                        // Column 2: Axis Mapping & Deadzone
                        ImGui::SetCursorPos(ImVec2(contentW * 0.355f, baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.88f, 0.82f, 0.45f, 1.0f), "Axis Mapping & Deadzone");
                        const char* axisOpts[] = { "None", "Axis 0", "Axis 1", "Axis 2", "Axis 3", "Axis 4", "Axis 5", "Axis 6", "Axis 7" };
                        auto axisRow = [&](const char* label, const char* invId, int& axisVal, bool& invVal) {
                            int idx = std::clamp(axisVal + 1, 0, 8);
                            ImGui::SetNextItemWidth(contentW * 0.135f);
                            if (ImGui::Combo(label, &idx, axisOpts, 9)) axisVal = idx - 1;
                            ImGui::SameLine(0.0f, S(8.0f));
                            ImGui::Checkbox(invId, &invVal);
                        };
                        axisRow("X (Yaw/Turn)",   "Inv##X", cfg.joyAxisX, cfg.joyInvertX);
                        axisRow("Y (Pitch/Move)", "Inv##Y", cfg.joyAxisY, cfg.joyInvertY);
                        axisRow("Z (Throttle)",   "Inv##Z", cfg.joyAxisZ, cfg.joyInvertZ);
                        axisRow("R (Rudder)",     "Inv##R", cfg.joyAxisR, cfg.joyInvertR);

                        ImGui::Spacing();
                        int dzPct = std::clamp(static_cast<int>(std::lround(cfg.joyDeadzone * 100.0f)), 0, 35);
                        ImGui::SetNextItemWidth(contentW * 0.195f);
                        if (ImGui::SliderInt("Deadzone", &dzPct, 0, 35, "%d%%")) {
                            cfg.joyDeadzone = static_cast<float>(dzPct) / 100.0f;
                        }
                        ImGui::Spacing();
                        if (ImGui::Button("Reset Default Stick Bindings", ImVec2(contentW * 0.275f, S(28.0f)))) {
                            cfg.joyAxisX = 0; cfg.joyAxisY = 1; cfg.joyAxisZ = 2; cfg.joyAxisR = 3;
                            cfg.joyInvertX = cfg.joyInvertY = cfg.joyInvertZ = cfg.joyInvertR = false;
                            cfg.joyDeadzone = 0.08f;
                            cfg.joyBtn1 = 0; cfg.joyBtn2 = 1; cfg.joyBtn3 = 2; cfg.joyBtn4 = 3;
                            bindingBtnSlot = -1;
                            statusNote = "Reset joystick axis & button bindings to defaults";
                        }
                        ImGui::Spacing();
                        ImGui::TextWrapped("Bypasses 1998 WinMM slot-0 limit: any USB stick, HOTAS, pedals, or gamepad works.");
                        ImGui::EndGroup();

                        // Column 3: 1-Click Button Binder
                        ImGui::SetCursorPos(ImVec2(contentW * 0.685f, baseY));
                        ImGui::BeginGroup();
                        ImGui::TextColored(ImVec4(0.45f, 0.94f, 0.62f, 1.0f), "Button Binder (Click Bind, Press Stick)");
                        const char* btnNames[4] = { "Btn 1 (Fire Gun)", "Btn 2 (Lase/Coax)", "Btn 3 (Aux 1)", "Btn 4 (Aux 2)" };
                        int* btnPtrs[4] = { &cfg.joyBtn1, &cfg.joyBtn2, &cfg.joyBtn3, &cfg.joyBtn4 };
                        for (int i = 0; i < 4; ++i) {
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("%-17s", btnNames[i]);
                            ImGui::SameLine();
                            bool waiting = (bindingBtnSlot == i);
                            char bLbl[48];
                            if (waiting) {
                                std::snprintf(bLbl, sizeof(bLbl), "Press stick...##b%d", i);
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.45f, 0.12f, 1.0f));
                            } else if (*btnPtrs[i] >= 0) {
                                std::snprintf(bLbl, sizeof(bLbl), "Stick #%d##b%d", *btnPtrs[i] + 1, i);
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.30f, 0.42f, 1.0f));
                            } else {
                                std::snprintf(bLbl, sizeof(bLbl), "Unbound##b%d", i);
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 1.0f));
                            }
                            if (ImGui::Button(bLbl, ImVec2(S(102.0f), S(26.0f)))) {
                                bindingBtnSlot = waiting ? -1 : i;
                            }
                            ImGui::PopStyleColor();
                            ImGui::SameLine(0.0f, S(5.0f));
                            char cLbl[16];
                            std::snprintf(cLbl, sizeof(cLbl), "X##clr%d", i);
                            if (ImGui::Button(cLbl, ImVec2(S(26.0f), S(26.0f)))) {
                                *btnPtrs[i] = -1;
                                if (bindingBtnSlot == i) bindingBtnSlot = -1;
                            }
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled("Tip: Uncheck 'Disable Joystick' or check\n'Enable Joystick / HOTAS' above to use\nyour stick in-game. Press Esc to cancel\nbutton capture.");
                        ImGui::EndGroup();

                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

                ImGui::EndChild();

                ImGui::Spacing();
                ImGui::Spacing();

                // Bottom Action Bar
                bool canLaunch = (selCard == 0) ? hasGame : hasWarCollege;
                if (!canLaunch) ImGui::BeginDisabled();
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.56f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.68f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.12f, 0.48f, 0.24f, 1.0f));
                const char* btnLabel = (selCard == 0) ? "LAUNCH  M1 TANK PLATOON II" : "LAUNCH  WAR COLLEGE (DSG)";
                if (ImGui::Button(btnLabel, ImVec2(S(360.0f), S(46.0f)))) {
                    launchChoice = selCard;
                    running = false;
                }
                ImGui::PopStyleColor(3);
                if (!canLaunch) ImGui::EndDisabled();

                ImGui::SameLine();
                if (!statusNote.empty()) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(ImVec4(0.48f, 0.94f, 0.62f, 1.0f), "  %s", statusNote.c_str());
                } else {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("  Tip: Press F12 anytime in-game for settings & Quick Keys");
                }

                ImGui::SameLine();
                ImGui::SetCursorPosX(contentRightX - S(120.0f));
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.36f, 0.14f, 0.14f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.18f, 0.18f, 1.0f));
                if (ImGui::Button("Quit", ImVec2(S(120.0f), S(46.0f)))) {
                    launchChoice = 2;
                    running = false;
                }
                ImGui::PopStyleColor(2);
            }

            ImGui::EndGroup();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    updater->cancel.store(true);

    // Always save updated INI & stock game.cfg settings on exit or launch
    SaveIni(iniPath, cfg);
    SaveStockGameCfg(stockCfgPath, stockCfg);

    if (activeJoy) SDL_CloseJoystick(activeJoy);
    if (keysTex) glDeleteTextures(1, &keysTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(glCtx);
    SDL_DestroyWindow(win);
    SDL_Quit();

    if (launchChoice == 0) return LaunchMainGame(dir) ? 0 : 1;
    if (launchChoice == 1) return LaunchWarCollege(dir) ? 0 : 1;
    return 0;
}

// M1 Tank Platoon II — Native GUI Launcher (SDL3 + OpenGL 3.3 + Dear ImGui)
// Feature parity with falcon30-native Trilogy Launcher:
//   - Launch Cards: M1 Tank Platoon II (M1TP2_bin.exe Win11 EmulateHeap bypass) & War College (DSG)
//   - Direct Manuals & Docs Links: Official PDF Manual, v1.2 Patch Notes, Original ReadMe, Mods Folder
//   - Built-in Quickstart Keyboard Layout Image Viewer (docs\M1TP2_Quickstart_Keys.png)
//   - Live 2-way m1tp2_recomp.ini Quick Launch Settings (Graphics, Widescreen, Gunnery Mouse, FCS Lead, Campaign)
//   - CLI flags: --launcher | --no-launcher / --play | --war-college / -w

#include <windows.h>
#include <shellapi.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct LauncherCfg {
    int   windowWidth           = 0;
    int   windowHeight          = 0;
    bool  fullscreen            = true;
    bool  vsync                 = true;
    int   menuAspectMode        = 1;    // 4:3
    int   aspectMode            = 0;    // Smart Hor+
    int   mapAspectMode         = 1;    // 4:3
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
    bool  enableMods            = true;
    bool  fixCampaignFrontBug   = true;
    bool  campaignIntelHud      = true;
    int   gunnerMouseMode       = 0;    // Precision Stick
    float gunnerMouseSens       = 1.0f;
    int   ballisticLeadMode     = 0;    // Smooth FCS
    bool  showStatsHud          = false;
};

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
        if (std::sscanf(line, " %127[^= \t] = %127s", k, v) == 2) {
            if (_stricmp(k, "WindowWidth") == 0)           c.windowWidth           = std::atoi(v);
            if (_stricmp(k, "WindowHeight") == 0)          c.windowHeight          = std::atoi(v);
            if (_stricmp(k, "Fullscreen") == 0)            c.fullscreen            = (std::atoi(v) != 0);
            if (_stricmp(k, "VSync") == 0)                 c.vsync                 = (std::atoi(v) != 0);
            if (_stricmp(k, "MenuAspectMode") == 0)        c.menuAspectMode        = std::atoi(v);
            if (_stricmp(k, "AspectMode") == 0)            c.aspectMode            = std::atoi(v);
            if (_stricmp(k, "MapAspectMode") == 0)         c.mapAspectMode         = std::atoi(v);
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
            if (_stricmp(k, "EnableMods") == 0)            c.enableMods            = (std::atoi(v) != 0);
            if (_stricmp(k, "FixCampaignFrontBug") == 0)   c.fixCampaignFrontBug   = (std::atoi(v) != 0);
            if (_stricmp(k, "CampaignIntelHud") == 0)      c.campaignIntelHud      = (std::atoi(v) != 0);
            if (_stricmp(k, "GunnerMouseMode") == 0)       c.gunnerMouseMode       = std::atoi(v);
            if (_stricmp(k, "GunnerMouseSens") == 0)       c.gunnerMouseSens       = static_cast<float>(std::atof(v));
            if (_stricmp(k, "BallisticLeadMode") == 0)     c.ballisticLeadMode     = std::atoi(v);
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
    std::fprintf(f, "# MenuAspectMode: 0=FillScreen, 1=4:3 (Default), 2=16:10, 3=CropZoom\n");
    std::fprintf(f, "MenuAspectMode = %d\n", c.menuAspectMode);
    std::fprintf(f, "# AspectMode (3D Combat): 0=SmartHor+ (Default), 1=All3DHor+, 2=4:3, 3=Stretch\n");
    std::fprintf(f, "AspectMode = %d\n", c.aspectMode);
    std::fprintf(f, "# MapAspectMode (Battleplanner / IVIS Map): 0=FillScreen, 1=4:3 (Default), 2=16:10\n");
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
    std::fprintf(f, "EnableMods = %d\n", c.enableMods ? 1 : 0);
    std::fprintf(f, "FixCampaignFrontBug = %d\n", c.fixCampaignFrontBug ? 1 : 0);
    std::fprintf(f, "CampaignIntelHud = %d\n", c.campaignIntelHud ? 1 : 0);
    std::fprintf(f, "# GunnerMouseMode: 0=PrecisionStick (Default), 1=DirectRawMouse, 2=Stock1998\n");
    std::fprintf(f, "GunnerMouseMode = %d\n", c.gunnerMouseMode);
    std::fprintf(f, "GunnerMouseSens = %.2f\n", c.gunnerMouseSens);
    std::fprintf(f, "# BallisticLeadMode: 0=SmoothFCS (Default), 1=ManualNoAutoLead, 2=Stock1998\n");
    std::fprintf(f, "BallisticLeadMode = %d\n", c.ballisticLeadMode);
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

    // 1. Prefer otvdmw.exe if installed
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

    // 2. Fallback to GOG's bundled BoxedWine (no console flash)
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        // Fallback: if SDL video fails for any reason, launch directly
        return hasGame ? (LaunchMainGame(dir) ? 0 : 1) : 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* win = SDL_CreateWindow(
        "M1 Tank Platoon II — Native Launcher",
        1080, 790,
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
    ApplyLauncherTheme();
    ImGui_ImplSDL3_InitForOpenGL(win, glCtx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    int keysW = 0, keysH = 0;
    GLuint keysTex = hasKeysPng ? LoadPngTexture(keysPngPath, keysW, keysH) : 0;

    int  selCard       = 0; // 0 = M1TP2, 1 = War College
    bool showKeysModal = false;
    int  launchChoice  = -1; // -1=none, 0=M1TP2, 1=WarCollege, 2=Quit
    std::string statusNote;

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) {
                launchChoice = 2;
                running = false;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                if (ev.key.scancode == SDL_SCANCODE_ESCAPE) {
                    if (showKeysModal) {
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
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(winW), static_cast<float>(winH)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 18.0f));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("##M1TP2NativeLauncher", nullptr, flags)) {
            const float availW   = ImGui::GetContentRegionAvail().x;
            const float contentW = std::min(availW, 1020.0f);
            const float leftPad  = std::max(0.0f, (availW - contentW) * 0.5f);
            if (leftPad > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftPad);

            ImGui::BeginGroup();

            // Header Banner
            ImGui::SetWindowFontScale(1.55f);
            ImGui::TextColored(ImVec4(0.30f, 0.95f, 0.50f, 1.0f), "M1 TANK PLATOON II");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.90f, 0.94f, 0.98f, 1.0f), "NATIVE LAUNCHER");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored(ImVec4(0.64f, 0.72f, 0.82f, 1.0f),
                "SDL3 + OpenGL 3.3 HD Glide  |  True Hor+ Widescreen  |  Precision Gunnery & FCS  |  F12 In-Game Overlay");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Show Quickstart Keyboard Layout Full Viewer OR Main Launcher Body
            if (showKeysModal) {
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "QUICKSTART KEYBOARD & GUNNERY LAYOUT");
                ImGui::SameLine(contentW - 315.0f);
                if (hasKeysPng && ImGui::Button("Open PNG Externally", ImVec2(170.0f, 28.0f))) {
                    ShellExecuteA(nullptr, "open", keysPngPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                ImGui::SameLine();
                if (ImGui::Button("Back to Launcher (Esc)", ImVec2(135.0f, 28.0f))) {
                    showKeysModal = false;
                }
                ImGui::Spacing();

                float viewerH = std::max(320.0f, static_cast<float>(winH) - 180.0f);
                if (keysTex && keysW > 0 && keysH > 0) {
                    float aspect = static_cast<float>(keysW) / static_cast<float>(keysH);
                    float drawW = contentW;
                    float drawH = drawW / aspect;
                    if (drawH > viewerH) {
                        drawH = viewerH;
                        drawW = drawH * aspect;
                    }
                    float pad = std::max(0.0f, (contentW - drawW) * 0.5f);
                    if (pad > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                    ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(keysTex)), ImVec2(drawW, drawH));
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
                        "- Smart True Hor+ 16:9 Widescreen (4:3 Sights & Cockpits)\n"
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

                const float cardGap = 16.0f;
                const float cardW   = (contentW - cardGap) * 0.5f;
                const float cardH   = 205.0f;

                for (int i = 0; i < 2; ++i) {
                    if (i > 0) ImGui::SameLine(0.0f, cardGap);
                    const auto& cd = cards[i];
                    bool isSel = (selCard == i);

                    ImVec4 bg     = isSel ? ImVec4(0.10f, 0.16f, 0.22f, 1.0f) : ImVec4(0.072f, 0.102f, 0.145f, 1.0f);
                    ImVec4 border = isSel ? cd.accent : ImVec4(0.22f, 0.28f, 0.36f, 0.85f);
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
                    ImGui::PushStyleColor(ImGuiCol_Border,  border);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSel ? 2.5f : 1.0f);

                    char cid[32];
                    std::snprintf(cid, sizeof(cid), "##LaunchCard_%d", i);
                    ImGui::BeginChild(cid, ImVec2(cardW, cardH), ImGuiChildFlags_Borders);

                    ImGui::SetCursorPos(ImVec2(14.0f, 12.0f));
                    ImGui::TextColored(cd.accent, "[%s]", cd.badge);
                    const char* stTxt = cd.ready ? "READY" : "NOT FOUND";
                    float stW = ImGui::CalcTextSize(stTxt).x;
                    ImGui::SameLine(cardW - stW - 14.0f);
                    ImGui::TextColored(cd.ready ? ImVec4(0.30f, 0.95f, 0.48f, 1.0f) : ImVec4(0.90f, 0.35f, 0.32f, 1.0f),
                                       "%s", stTxt);

                    ImGui::SetCursorPosX(14.0f);
                    ImGui::SetWindowFontScale(1.26f);
                    ImGui::TextColored(cd.ready ? ImVec4(0.96f, 0.98f, 1.0f, 1.0f) : ImVec4(0.55f, 0.58f, 0.62f, 1.0f),
                                       "%s", cd.title);
                    ImGui::SetWindowFontScale(1.0f);

                    ImGui::SetCursorPosX(14.0f);
                    ImGui::TextColored(ImVec4(0.90f, 0.80f, 0.42f, 1.0f), "%s", cd.sub);
                    ImGui::Spacing();
                    ImGui::SetCursorPosX(14.0f);
                    ImGui::TextColored(ImVec4(0.78f, 0.85f, 0.92f, 1.0f), "%s", cd.desc);
                    ImGui::Spacing();
                    ImGui::SetCursorPosX(14.0f);
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
                ImGui::BeginChild("##DocsPanel", ImVec2(contentW, 78.0f), ImGuiChildFlags_Borders);
                ImGui::SetCursorPos(ImVec2(14.0f, 8.0f));
                ImGui::TextColored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "MANUALS & QUICKSTART REFERENCE");

                ImGui::SetCursorPos(ImVec2(14.0f, 34.0f));
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.36f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.38f, 1.0f));
                if (ImGui::Button("Quickstart Keyboard Layout", ImVec2(225.0f, 32.0f))) {
                    showKeysModal = true;
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine();
                if (!hasManualPdf) ImGui::BeginDisabled();
                if (ImGui::Button("Official Game Manual (PDF)", ImVec2(215.0f, 32.0f))) {
                    std::string p = dir + "\\M1_Tank_Platoon_II_Manual.pdf";
                    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened M1_Tank_Platoon_II_Manual.pdf";
                }
                if (!hasManualPdf) ImGui::EndDisabled();

                ImGui::SameLine();
                if (!hasPatchTxt) ImGui::BeginDisabled();
                if (ImGui::Button("v1.2 Patch Notes", ImVec2(150.0f, 32.0f))) {
                    std::string p = dir + "\\M1TP2Readme.txt";
                    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened M1TP2Readme.txt";
                }
                if (!hasPatchTxt) ImGui::EndDisabled();

                ImGui::SameLine();
                if (!hasReadmeTxt) ImGui::BeginDisabled();
                if (ImGui::Button("Original ReadMe", ImVec2(145.0f, 32.0f))) {
                    std::string p = dir + "\\ReadMe.txt";
                    ShellExecuteA(nullptr, "open", p.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened ReadMe.txt";
                }
                if (!hasReadmeTxt) ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("Open Mods Folder", ImVec2(165.0f, 32.0f))) {
                    std::string mp = dir + "\\mods";
                    CreateDirectoryA(mp.c_str(), nullptr);
                    ShellExecuteA(nullptr, "open", mp.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
                    statusNote = "Opened mods\\ folder";
                }
                ImGui::EndChild();

                ImGui::Spacing();

                // Quick Launch Settings Panel (3 Columns, synced directly with m1tp2_recomp.ini)
                ImGui::BeginChild("##QuickSettingsPanel", ImVec2(contentW, 275.0f), ImGuiChildFlags_Borders);
                ImGui::SetCursorPos(ImVec2(14.0f, 10.0f));
                ImGui::TextColored(ImVec4(0.55f, 0.84f, 1.0f, 1.0f), "QUICK LAUNCH SETTINGS  (m1tp2_recomp.ini)");

                // Column 1: Graphics & Widescreen
                ImGui::SetCursorPos(ImVec2(14.0f, 38.0f));
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.88f, 0.82f, 0.45f, 1.0f), "Graphics & Widescreen");
                const char* asp3d[] = { "Smart Hor+ 16:9", "All 3D Hor+", "4:3 Original", "Stretch Fill" };
                ImGui::SetNextItemWidth(210.0f);
                ImGui::Combo("3D Aspect", &cfg.aspectMode, asp3d, 4);

                const char* mapAsp[] = { "16:9 Fill", "4:3 Original", "16:10 Wide" };
                ImGui::SetNextItemWidth(210.0f);
                ImGui::Combo("Map Aspect", &cfg.mapAspectMode, mapAsp, 3);

                const char* drawOpts[] = { "2.0 km (Stock)", "3.0 km (2x LOD)", "4.0 km (3x LOD)", "6.0 km (4x LOD)" };
                ImGui::SetNextItemWidth(210.0f);
                ImGui::Combo("Draw Dist", &cfg.drawDistMode, drawOpts, 4);

                const int msaaVals[] = { 0, 2, 4, 8 };
                const char* msaaLbls[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
                int msaaIdx = 0;
                for (int i = 0; i < 4; ++i) if (msaaVals[i] == cfg.msaaSamples) msaaIdx = i;
                ImGui::SetNextItemWidth(210.0f);
                if (ImGui::Combo("MSAA", &msaaIdx, msaaLbls, 4)) cfg.msaaSamples = msaaVals[msaaIdx];

                const int afVals[] = { 1, 2, 4, 8, 16 };
                const char* afLbls[] = { "Off (1x)", "2x AF", "4x AF", "8x AF", "16x AF" };
                int afIdx = 0;
                for (int i = 0; i < 5; ++i) if (afVals[i] == cfg.anisotropy) afIdx = i;
                ImGui::SetNextItemWidth(210.0f);
                if (ImGui::Combo("Anisotropy", &afIdx, afLbls, 5)) cfg.anisotropy = afVals[afIdx];
                ImGui::EndGroup();

                // Column 2: Gunnery, Mouse & Ballistics
                ImGui::SameLine(340.0f);
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.98f, 0.65f, 0.36f, 1.0f), "Gunnery & Ballistic FCS");
                const char* mouseModes[] = { "Precision Stick (Rec.)", "Direct Raw Mouse", "Stock 1998" };
                ImGui::SetNextItemWidth(220.0f);
                ImGui::Combo("Gunner Mouse", &cfg.gunnerMouseMode, mouseModes, 3);

                ImGui::SetNextItemWidth(220.0f);
                ImGui::SliderFloat("Mouse Sens", &cfg.gunnerMouseSens, 0.25f, 3.00f, "%.2fx");

                const char* leadModes[] = { "Smooth FCS (Rec.)", "Manual (No Auto-Lead)", "Stock 1998" };
                ImGui::SetNextItemWidth(220.0f);
                ImGui::Combo("Broadside FCS", &cfg.ballisticLeadMode, leadModes, 3);

                const char* fogOpts[] = { "Auto (Match Draw)", "Extended 2x", "Extended 4x", "Off (Clear)" };
                ImGui::SetNextItemWidth(220.0f);
                ImGui::Combo("3D Fog", &cfg.fogMode, fogOpts, 4);

                ImGui::SetNextItemWidth(220.0f);
                ImGui::SliderFloat("3D Gamma", &cfg.gamma, 0.70f, 1.60f, "%.2f");
                ImGui::EndGroup();

                // Column 3: Runtime & Campaign Toggles
                ImGui::SameLine(695.0f);
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.45f, 0.94f, 0.62f, 1.0f), "Runtime & Campaign");
                ImGui::Checkbox("Borderless Fullscreen", &cfg.fullscreen);
                ImGui::Checkbox("VSync", &cfg.vsync);
                ImGui::Checkbox("Fast-Boot (Skip Intros)", &cfg.skipIntro);
                ImGui::Checkbox("Show Launcher on Startup", &cfg.showLauncherOnStartup);
                ImGui::Checkbox("Fix 1998 Campaign AI Bug", &cfg.fixCampaignFrontBug);
                ImGui::Checkbox("Campaign Intel Sidebar", &cfg.campaignIntelHud);
                ImGui::Checkbox("Enable mods/ VFS Loader", &cfg.enableMods);
                ImGui::Checkbox("Show FPS / Stats HUD", &cfg.showStatsHud);
                ImGui::EndGroup();

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
                if (ImGui::Button(btnLabel, ImVec2(360.0f, 46.0f))) {
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

                ImGui::SameLine(contentW - 120.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.36f, 0.14f, 0.14f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.18f, 0.18f, 1.0f));
                if (ImGui::Button("Quit", ImVec2(120.0f, 46.0f))) {
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

    // Always save updated INI settings on exit or launch
    SaveIni(iniPath, cfg);

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

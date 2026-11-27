#include "overlay_ui.h"
#include "video_sdl.h"
#include "glide_gl.h"
#include "vfs_mod.h"
#include "campaign_hud.h"
#include "gunnery.h"
#include "audio_mixer.h"
#include "joystick_sdl.h"
#include "gl_loader.h"
#include "common.h"

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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace m1 {

namespace {

bool g_open   = false;
bool g_inited = false;

bool g_showDisplay  = true;
bool g_showAudio    = false;
bool g_showGunnery  = false;
bool g_showCampaign = false;
bool g_showKeys     = false;
bool g_showDebug    = false;

std::string g_statusBanner;
uint64_t    g_statusUntilMs = 0;

// Quickstart Keyboard Layout texture (loaded lazily when Quick Keys window opens)
GLuint g_keysTex     = 0;
int    g_keysW       = 0;
int    g_keysH       = 0;
bool   g_keysTried   = false;

static void SetStatusMsg(const char* msg) {
    g_statusBanner = msg ? msg : "";
    g_statusUntilMs = SDL_GetTicks() + 3000;
}

static bool FileExistsA(const char* p) {
    DWORD a = GetFileAttributesA(p);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void OpenExternalFile(const char* relPath, const char* label) {
    if (FileExistsA(relPath)) {
        ShellExecuteA(nullptr, "open", relPath, nullptr, nullptr, SW_SHOWNORMAL);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Opened %s", label);
        SetStatusMsg(buf);
    } else {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Not found: %s", relPath);
        SetStatusMsg(buf);
    }
}

static void EnsureKeysTextureLoaded() {
    if (g_keysTried) return;
    g_keysTried = true;

    const char* candidates[] = {
        "docs\\M1TP2_Quickstart_Keys.png",
        "recomp\\docs\\M1TP2_Quickstart_Keys.png",
        "M1TP2_Quickstart_Keys.png"
    };
    const char* found = nullptr;
    for (const char* c : candidates) {
        if (FileExistsA(c)) { found = c; break; }
    }
    if (!found) return;

    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load(found, &w, &h, &ch, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return;
    }

    glGenTextures(1, &g_keysTex);
    glBindTexture(GL_TEXTURE_2D, g_keysTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    gl::GenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);

    g_keysW = w;
    g_keysH = h;
    LogPrintf("[UI] Loaded Quickstart Keyboard Layout texture (%dx%d) from %s\n", w, h, found);
}

static void ApplyMilitaryTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding    = 6.0f;
    st.ChildRounding     = 5.0f;
    st.FrameRounding     = 4.0f;
    st.PopupRounding     = 4.0f;
    st.GrabRounding      = 4.0f;
    st.WindowBorderSize  = 1.5f;
    st.FramePadding      = ImVec2(8.0f, 4.0f);
    st.ItemSpacing       = ImVec2(8.0f, 6.0f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.055f, 0.082f, 0.095f, 0.94f);
    c[ImGuiCol_ChildBg]          = ImVec4(0.070f, 0.102f, 0.120f, 0.90f);
    c[ImGuiCol_PopupBg]          = ImVec4(0.060f, 0.090f, 0.105f, 0.96f);
    c[ImGuiCol_Border]           = ImVec4(0.24f, 0.56f, 0.36f, 0.85f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.07f, 0.15f, 0.11f, 0.96f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.10f, 0.24f, 0.16f, 0.98f);
    c[ImGuiCol_MenuBarBg]        = ImVec4(0.06f, 0.11f, 0.12f, 0.96f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.10f, 0.16f, 0.19f, 0.90f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.15f, 0.25f, 0.28f, 0.95f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.18f, 0.34f, 0.26f, 1.00f);
    c[ImGuiCol_CheckMark]        = ImVec4(0.32f, 0.95f, 0.52f, 1.00f);
    c[ImGuiCol_SliderGrab]       = ImVec4(0.26f, 0.78f, 0.44f, 0.95f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.96f, 0.54f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.12f, 0.28f, 0.20f, 0.92f);
    c[ImGuiCol_ButtonHovered]    = ImVec4(0.18f, 0.42f, 0.28f, 1.00f);
    c[ImGuiCol_ButtonActive]     = ImVec4(0.22f, 0.52f, 0.34f, 1.00f);
    c[ImGuiCol_Header]           = ImVec4(0.14f, 0.32f, 0.22f, 0.85f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.18f, 0.44f, 0.30f, 0.95f);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.22f, 0.54f, 0.36f, 1.00f);
    c[ImGuiCol_Separator]        = ImVec4(0.24f, 0.45f, 0.34f, 0.65f);
}

static void DrawCampaignIntelSidebar(int winW, int winH, const CampaignSnapshot& cs) {
    int vx = 0, vy = 0, vw = winW, vh = winH;
    VideoGetViewport(false, vx, vy, vw, vh);
    int rightMargin = winW - (vx + vw);

    float px, py, pw;
    if (rightMargin >= 210) {
        pw = std::min(static_cast<float>(rightMargin - 16), 310.0f);
        px = static_cast<float>(vx + vw + 8);
        py = g_open ? 38.0f : std::max(16.0f, (winH - 470.0f) * 0.5f);
    } else {
        pw = 260.0f;
        px = static_cast<float>(winW) - pw - 12.0f;
        py = g_open ? 38.0f : 14.0f;
    }

    ImGui::SetNextWindowPos(ImVec2(px, py), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw, 0.0f), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.072f, 0.062f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.28f, 0.72f, 0.42f, 0.92f));
    if (ImGui::Begin("##CampaignIntelHUD", nullptr, flags)) {
        ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.68f, 1.0f), "CAMPAIGN INTEL");
        ImGui::Separator();

        // Verdict banner
        const char* verdict = "OPENING PHASE";
        ImVec4 vcol(0.40f, 0.86f, 0.98f, 1.0f);
        if (cs.totalBattles > 0) {
            if (cs.winPct >= cs.winThreshold) {
                verdict = "STATUS: WINNING";
                vcol = ImVec4(0.28f, 0.98f, 0.44f, 1.0f);
            } else if (cs.winPct <= cs.loseThreshold) {
                verdict = "STATUS: LOSING";
                vcol = ImVec4(1.00f, 0.34f, 0.30f, 1.0f);
            } else {
                verdict = "STATUS: CONTESTED";
                vcol = ImVec4(1.00f, 0.80f, 0.25f, 1.0f);
            }
        }
        ImGui::TextColored(vcol, "%s", verdict);
        ImGui::Text("Record: %dW - %dL  (%d%%)", cs.wins, cs.losses, cs.winPct);

        // Win-Rate Gauge vs Hidden Checkpoints
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float gw = ImGui::GetContentRegionAvail().x;
        float gh = 14.0f;
        float loseX = gw * (cs.loseThreshold / 100.0f);
        float winX  = gw * (cs.winThreshold  / 100.0f);
        dl->AddRectFilled(p0, ImVec2(p0.x + gw,    p0.y + gh), IM_COL32(150, 112, 24, 235), 2.0f);
        dl->AddRectFilled(p0, ImVec2(p0.x + loseX, p0.y + gh), IM_COL32(175, 42,  42, 245), 2.0f);
        dl->AddRectFilled(ImVec2(p0.x + winX, p0.y), ImVec2(p0.x + gw, p0.y + gh), IM_COL32(36, 155, 65, 245), 2.0f);
        if (cs.totalBattles > 0) {
            float nx = p0.x + gw * (std::clamp(cs.winPct, 0, 100) / 100.0f);
            dl->AddRectFilled(ImVec2(nx - 2.0f, p0.y - 2.0f), ImVec2(nx + 2.0f, p0.y + gh + 2.0f),
                              IM_COL32(255, 255, 255, 255));
        }
        ImGui::Dummy(ImVec2(gw, gh + 2.0f));
        ImGui::TextDisabled("Defeat <= %d%%  |  Win >= %d%%", cs.loseThreshold, cs.winThreshold);

        ImGui::Separator();
        if (cs.nextCpIdx < 3) {
            ImGui::TextColored(ImVec4(0.65f, 0.94f, 0.74f, 1.0f), "Next Checkpoint #%d", cs.nextCpIdx + 1);
        } else {
            ImGui::TextColored(ImVec4(0.65f, 0.94f, 0.74f, 1.0f), "Final Campaign Cap");
        }
        ImGui::Text("Battles: %d / %d", cs.totalBattles, cs.nextCpBattles);
        ImGui::Text("Clock:   %d / %d hr (Day %d)", cs.elapsedHours, cs.nextCpHours, cs.elapsedDays + 1);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.65f, 0.94f, 0.74f, 1.0f), "Front Lines (US -> Enemy)");
        static const char* kFrontNames[4] = { "RED", "GRN", "YEL", "BLU" };
        for (int i = 0; i < 4; ++i) {
            const auto& fr = cs.fronts[i];
            bool isYou = (i == cs.playerFront);
            const char* trend = (fr.lastDelta < 0) ? "ADV" : (fr.lastDelta > 0 ? "RET" : "HLD");
            ImVec4 tcol = isYou ? ImVec4(1.0f, 0.95f, 0.45f, 1.0f)
                                : (fr.active ? ImVec4(0.84f, 0.88f, 0.86f, 1.0f) : ImVec4(0.45f, 0.50f, 0.48f, 1.0f));
            ImGui::TextColored(tcol, "%s%s  %3d%%  %s",
                               isYou ? "> " : "  ", kFrontNames[i],
                               fr.progressPct, fr.active ? trend : "OFF");

            ImVec2 fp = ImGui::GetCursorScreenPos();
            float fw = ImGui::GetContentRegionAvail().x;
            float fh = 8.0f;
            if (fr.active) {
                dl->AddRectFilled(fp, ImVec2(fp.x + fw, fp.y + fh), IM_COL32(148, 42, 42, 230), 2.0f);
                dl->AddRectFilled(fp, ImVec2(fp.x + fw * (fr.progressPct / 100.0f), fp.y + fh),
                                  isYou ? IM_COL32(82, 226, 250, 245) : IM_COL32(58, 162, 220, 240), 2.0f);
            } else {
                dl->AddRectFilled(fp, ImVec2(fp.x + fw, fp.y + fh), IM_COL32(48, 54, 52, 190), 2.0f);
            }
            ImGui::Dummy(ImVec2(fw, fh + 3.0f));
        }

        ImGui::Separator();
        ImGui::TextColored(g_cfg.fixCampaignFrontBug ? ImVec4(0.42f, 0.95f, 0.55f, 1.0f)
                                                     : ImVec4(0.98f, 0.72f, 0.26f, 1.0f),
                           g_cfg.fixCampaignFrontBug ? "AI Fronts: Fixed (/10)" : "AI Fronts: 1998 Stock Bug");
        ImGui::TextDisabled("Press F12 for Native Settings");
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

static void DrawStatsPill() {
    float topY = g_open ? 32.0f : 10.0f;
    ImGui::SetNextWindowPos(ImVec2(10.0f, topY), ImGuiCond_Always);
    ImGuiWindowFlags pf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.07f, 0.06f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.28f, 0.82f, 0.44f, 0.88f));
    if (ImGui::Begin("##M1StatsPill", nullptr, pf)) {
        if (g_stats.inGlide3D) {
            ImGui::TextColored(ImVec4(0.40f, 0.98f, 0.55f, 1.0f),
                "GLIDE HD %dx%d [%s %s] | %.0f FPS | MSAA %dX | AF %dX | TRIS %d",
                g_stats.renderW, g_stats.renderH,
                GlideGetViewName(g_stats.currentView),
                g_stats.horPlusActive ? "HOR+" : "4:3",
                g_stats.fps, g_stats.activeMsaa, g_cfg.anisotropy, g_stats.triangles);
        } else {
            int winW = 0, winH = 0;
            VideoGetWindowSize(winW, winH);
            ImGui::TextColored(ImVec4(0.40f, 0.98f, 0.55f, 1.0f),
                "2D/SW MODE %dx%d | %.0f FPS | Press F12 for Native Menu",
                winW, winH, g_stats.fps);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

static void DrawDisplayWindow() {
    ImGui::SetNextWindowPos(ImVec2(24.0f, 44.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 485.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("3D / Display Settings", &g_showDisplay)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    if (ImGui::Checkbox("Borderless Fullscreen (Alt+Enter)", &g_cfg.fullscreen)) {
        VideoApplyDisplaySettings();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("VSync", &g_cfg.vsync)) {
        VideoApplyDisplaySettings();
        changed = true;
    }
    if (ImGui::Checkbox("100 FPS Mode (High-Precision Timer)", &g_cfg.unlock100Fps)) {
        VideoApplyFrameRateCap();
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Replaces M1TP2's 1998 15.6ms GetTickCount + 50 FPS cap with a microsecond QPC timer\n"
                          "and 100 FPS cap (72 FPS with 144Hz VSync, 100 FPS with VSync off / G-Sync).\n"
                          "Simulation speed remains 100%% authentic 1x real-time.");
    }

    const char* aspect3dNames[] = {
        "Smart Hor+ Widescreen (Recommended)",
        "All 3D Hor+ (Inc. GPS Sight)",
        "4:3 Original",
        "Fill Screen (Stretch)"
    };
    if (ImGui::Combo("3D Combat Aspect", &g_cfg.aspectMode, aspect3dNames, 4)) {
        GlideApplySettings();
        changed = true;
    }

    const int mapVals[] = { MAP_ASPECT_AUTO_NATIVE, MAP_ASPECT_16_10, MAP_ASPECT_16_9, MAP_ASPECT_STRETCH };
    const char* mapNames[] = { "Auto Aspect-Native (4:3)", "16:10 Wide (Capped)", "16:9 Wide (Capped)", "Stretch Fill" };
    int mapIdx = 0;
    for (int i = 0; i < 4; ++i) if (mapVals[i] == g_cfg.mapAspectMode) mapIdx = i;
    if (ImGui::Combo("Tactical Map / Planner", &mapIdx, mapNames, 4)) {
        g_cfg.mapAspectMode = mapVals[mapIdx];
        GlideApplySettings();
        changed = true;
    }

    const char* menuNames[] = { "Fill Screen", "4:3 Original", "16:10 Wide", "Crop Zoom" };
    if (ImGui::Combo("2D Menu Aspect", &g_cfg.menuAspectMode, menuNames, 4)) {
        changed = true;
    }

    ImGui::Separator();

    const char* drawNames[] = {
        "2.0 km (Stock 1x LOD)",
        "3.0 km (Extended 2x LOD)",
        "4.0 km (Long 3x LOD)",
        "6.0 km (Max 4x LOD)"
    };
    if (ImGui::Combo("3D Draw Distance / LOD", &g_cfg.drawDistMode, drawNames, 4)) {
        GlideApplySettings();
        changed = true;
    }

    const char* fogNames[] = {
        "Auto (Match Draw Distance)",
        "Extended 2x",
        "Extended 4x",
        "Off (Clear Horizon)"
    };
    if (ImGui::Combo("3D Fog Distance", &g_cfg.fogMode, fogNames, 4)) {
        changed = true;
    }

    const int msaaVals[] = { 0, 2, 4, 8 };
    const char* msaaLabels[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
    int msaaIdx = 0;
    for (int i = 0; i < 4; ++i) if (msaaVals[i] == g_cfg.msaaSamples) msaaIdx = i;
    if (ImGui::Combo("3D MSAA Anti-Aliasing", &msaaIdx, msaaLabels, 4)) {
        g_cfg.msaaSamples = std::min(msaaVals[msaaIdx], g_stats.maxMsaa);
        GlideApplySettings();
        changed = true;
    }

    const int afVals[] = { 1, 2, 4, 8, 16 };
    const char* afLabels[] = { "Off (1x)", "2x AF", "4x AF", "8x AF", "16x AF" };
    int afIdx = 0;
    for (int i = 0; i < 5; ++i) if (afVals[i] == g_cfg.anisotropy) afIdx = i;
    if (ImGui::Combo("Anisotropic Filtering", &afIdx, afLabels, 5)) {
        g_cfg.anisotropy = std::min(afVals[afIdx], g_stats.maxAniso);
        GlideApplySettings();
        changed = true;
    }

    const char* texFiltNames[] = { "Trilinear (Mipmaps)", "Bilinear", "Nearest (Retro)", "Game Default" };
    if (ImGui::Combo("3D Texture Filter", &g_cfg.texFilter3D, texFiltNames, 4)) {
        GlideApplySettings();
        changed = true;
    }

    if (ImGui::Checkbox("2D Menu Bilinear Filter (F11)", &g_cfg.linearFilter2D)) {
        VideoApplyDisplaySettings();
        changed = true;
    }
    if (ImGui::Checkbox("3D Cockpit / HUD Bilinear Filter", &g_cfg.linearFilterLfb)) {
        GlideApplySettings();
        changed = true;
    }
    if (ImGui::SliderFloat("3D Gamma", &g_cfg.gamma, 0.70f, 1.60f, "%.2f")) {
        changed = true;
    }
    if (ImGui::Checkbox("Force Native Glide HD when Direct3D Selected", &g_cfg.forceGlideOnD3D)) {
        changed = true;
    }

    ImGui::Separator();
    if (g_stats.inGlide3D) {
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.58f, 1.0f),
            "Active: %s [%s %dx%d] | %.0f FPS",
            GlideGetViewName(g_stats.currentView),
            g_stats.horPlusActive ? "True Hor+" : "4:3",
            g_stats.renderW, g_stats.renderH, g_stats.fps);
    } else {
        ImGui::TextDisabled("Active: 2D Menu Mode | %.0f FPS", g_stats.fps);
    }

    if (changed) SaveConfig();
    ImGui::End();
}

static void DrawAudioWindow() {
    ImGui::SetNextWindowPos(ImVec2(470.0f, 44.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 255.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audio Mixer", &g_showAudio)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    if (ImGui::Checkbox("Mute All Audio", &g_cfg.audioMute)) {
        changed = true;
    }
    ImGui::Separator();

    auto pctSlider = [&](const char* label, float& v) {
        int pct = std::clamp(static_cast<int>(std::lround(v * 100.0f)), 0, 100);
        if (ImGui::SliderInt(label, &pct, 0, 100, "%d%%")) {
            v = static_cast<float>(pct) / 100.0f;
            changed = true;
        }
    };

    if (g_cfg.audioMute) ImGui::BeginDisabled();
    pctSlider("Master Volume",     g_cfg.audioMasterVol);
    pctSlider("Music (Menu/Map)",  g_cfg.audioMusicVol);
    pctSlider("Weapons & SFX",     g_cfg.audioSfxVol);
    pctSlider("Engines & Tracks",  g_cfg.audioEngineVol);
    pctSlider("Crew Radio / Voice",g_cfg.audioVoiceVol);
    if (g_cfg.audioMute) ImGui::EndDisabled();

    ImGui::Spacing();
    if (ImGui::Button("Reset All to 100%")) {
        g_cfg.audioMute      = false;
        g_cfg.audioMasterVol = 1.0f;
        g_cfg.audioMusicVol  = 1.0f;
        g_cfg.audioSfxVol    = 1.0f;
        g_cfg.audioEngineVol = 1.0f;
        g_cfg.audioVoiceVol  = 1.0f;
        changed = true;
    }

    if (changed) {
        AudioApplySettings();
        SaveConfig();
    }
    ImGui::End();
}

static void DrawGunneryWindow() {
    ImGui::SetNextWindowPos(ImVec2(470.0f, 44.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 360.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Gunnery & Fire Control (FCS)", &g_showGunnery)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.34f, 1.0f), "Gunner Sight Mouse Precision (GPS / GAS)");
    const char* mouseModes[] = {
        "Precision Stick (No 10x Deadzone - Recommended)",
        "Direct Raw Mouse (1:1 Aim, M1A2 Slew Cap)",
        "Stock 1998 Virtual Stick"
    };
    if (ImGui::Combo("Gunner Mouse Mode", &g_cfg.gunnerMouseMode, mouseModes, 3)) {
        changed = true;
    }
    if (g_cfg.gunnerMouseMode != GUNNER_STOCK_1998) {
        if (ImGui::SliderFloat("Mouse Sensitivity", &g_cfg.gunnerMouseSens, 0.25f, 3.00f, "%.2fx")) {
            changed = true;
        }
    }
    if (g_cfg.gunnerMouseMode == GUNNER_PRECISION_STICK) {
        ImGui::TextWrapped("Replaces M1TP2's 640x480 quantization and integer /8 deadzone with sub-pixel smooth tracking for crisp long-range aiming in 10x zoom.");
    } else if (g_cfg.gunnerMouseMode == GUNNER_DIRECT_RAW) {
        ImGui::TextWrapped("Uses raw SDL mouse deltas for direct 1:1 aiming when left-clicked into GPS/GAS sight (right-click releases). Capped to authentic M1A2 turret slew speed.");
    }

    if (ImGui::Checkbox("Disable Joystick / Gamepad Input", &g_cfg.disableJoystick)) {
        JoystickSDLOnConfigChanged();
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When unchecked, uses the SDL3 Joystick/HOTAS backend configured in the Launcher\n(works with any USB stick/HOTAS/gamepad regardless of WinMM slot).");
    }
    if (!g_cfg.disableJoystick) {
        ImGui::TextColored(ImVec4(0.45f, 0.88f, 1.0f, 1.0f), "Active Stick: %s", JoystickSDLGetActiveName());
        int dzPct = std::clamp(static_cast<int>(std::lround(g_cfg.joyDeadzone * 100.0f)), 0, 35);
        if (ImGui::SliderInt("Stick Deadzone", &dzPct, 0, 35, "%d%%")) {
            g_cfg.joyDeadzone = static_cast<float>(dzPct) / 100.0f;
            changed = true;
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.34f, 1.0f), "Ballistic Dynamic Lead Computer (90-deg Broadside Fix)");
    const char* leadModes[] = {
        "Smooth FCS (0.45s Avg + 35 m/s Clamp - Recommended)",
        "Manual Aim (Disable Hidden Auto-Lead)",
        "Stock 1998 (Unclamped Single-Frame Lead)"
    };
    if (ImGui::Combo("Ballistic Lead FCS", &g_cfg.ballisticLeadMode, leadModes, 3)) {
        changed = true;
    }
    ImGui::TextWrapped("Fixes M1TP2's single-frame turret-rate lead spike that throws rounds 40-60m forward when tracking sideways while driving.");

    ImGui::Separator();
    GunneryTelemetry gt;
    GunneryGetTelemetry(gt);
    ImGui::TextColored(ImVec4(0.50f, 0.92f, 0.65f, 1.0f), "Live FCS Telemetry");
    ImGui::Text("Sight Control: %s  |  Zoom: %s",
                gt.sightActive ? "ACTIVE (Captured)" : "Idle",
                gt.zoom10x ? "10x" : "3x");
    ImGui::Text("Turret Slew:   %+.2f deg/s  (Smooth FCS: %+.2f deg/s)",
                gt.instOmegaDegS, gt.smoothOmegaDegS);
    ImGui::Text("Tank Speed:    %.1f m/s (%.0f km/h)", gt.tankSpeedMps, gt.tankSpeedMps * 3.6f);
    ImGui::Text("Last Shot:     Range %.0fm | FCS Lead %+.1f m/s (Stock would be %+.1f m/s)",
                gt.lastLasedRangeM, gt.lastLeadMps, gt.lastStockLeadMps);

    if (changed) SaveConfig();
    ImGui::End();
}

static void DrawCampaignModsWindow() {
    ImGui::SetNextWindowPos(ImVec2(470.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 250.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Campaign & Mods VFS", &g_showCampaign)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    ImGui::TextColored(ImVec4(0.55f, 0.94f, 0.68f, 1.0f), "Dynamic Campaign Engine");
    if (ImGui::Checkbox("Fix 1998 AI-Front /10 Bug (0x42A395)", &g_cfg.fixCampaignFrontBug)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Original 1998 bug compared 0..1000 permille against 0..100 roll,\ncausing allied AI fronts to win 99%% of battles automatically.");
    }
    if (ImGui::Checkbox("Show Campaign Intel HUD (Map & Briefing Sidebar)", &g_cfg.campaignIntelHud)) {
        changed = true;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.94f, 0.68f, 1.0f), "Native Modding VFS (mods/ folder)");
    if (ImGui::Checkbox("Enable mods/ VFS Override Loader", &g_cfg.enableMods)) {
        VfsModReload();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Mods")) {
        VfsModReload();
        SetStatusMsg("Reloaded mods/ VFS");
    }
    ImGui::Text("Active Mods: %d  |  Override Files: %d  |  Unit JSON Overrides: %d",
                g_stats.activeMods, g_stats.modFiles, g_stats.unitOverrides);
    ImGui::TextDisabled("Live VFS Hits This Session: %d", g_stats.vfsHits);
    if (ImGui::Button("Open mods/ Folder in Explorer")) {
        CreateDirectoryA("mods", nullptr);
        ShellExecuteA(nullptr, "open", "mods", nullptr, nullptr, SW_SHOWNORMAL);
    }

    if (changed) SaveConfig();
    ImGui::End();
}

static float g_keysZoom = 0.0f; // 0.0 = Fit to window, >0 = manual scale

static void DrawQuickKeysWindow(int winW, int winH) {
    EnsureKeysTextureLoaded();
    float maxH = std::max(420.0f, static_cast<float>(winH) - 80.0f);
    float maxW = std::max(640.0f, static_cast<float>(winW) - 80.0f);
    float aspect = (g_keysW > 0 && g_keysH > 0) ? (static_cast<float>(g_keysW) / static_cast<float>(g_keysH)) : (16.0f / 9.0f);
    float initH = std::min(maxH, 1140.0f);
    float initW = std::min(maxW, initH * aspect + 24.0f);
    initH = std::min(maxH, (initW - 24.0f) / aspect + 68.0f);
    ImGui::SetNextWindowPos(ImVec2((winW - initW) * 0.5f, std::max(30.0f, (winH - initH) * 0.5f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(initW, initH), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("M1TP2 Quickstart Keyboard Layout", &g_showKeys)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Fit")) g_keysZoom = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("100%")) g_keysZoom = 1.0f;
    ImGui::SameLine();
    if (ImGui::Button("125%")) g_keysZoom = 1.25f;
    ImGui::SameLine();
    if (ImGui::Button("Open Manual PDF")) OpenExternalFile("M1_Tank_Platoon_II_Manual.pdf", "Manual PDF");
    ImGui::SameLine();
    if (ImGui::Button("Open Keyboard Image Externally")) OpenExternalFile("docs\\M1TP2_Quickstart_Keys.png", "Quickstart PNG");
    ImGui::Separator();

    if (g_keysTex && g_keysW > 0 && g_keysH > 0) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float imgAreaW = std::max(200.0f, avail.x);
        float imgAreaH = std::max(140.0f, avail.y);
        ImGuiWindowFlags cf = (g_keysZoom > 0.0f) ? ImGuiWindowFlags_HorizontalScrollbar : 0;
        ImGui::BeginChild("##OverlayKeysScroll", ImVec2(imgAreaW, imgAreaH), ImGuiChildFlags_None, cf);
        ImVec2 inner = ImGui::GetContentRegionAvail();
        float drawW, drawH;
        if (g_keysZoom <= 0.0f) {
            drawW = inner.x;
            drawH = drawW / aspect;
            if (drawH > inner.y) {
                drawH = inner.y;
                drawW = drawH * aspect;
            }
        } else {
            drawW = static_cast<float>(g_keysW) * g_keysZoom;
            drawH = static_cast<float>(g_keysH) * g_keysZoom;
        }
        float padX = std::max(0.0f, (inner.x - drawW) * 0.5f);
        float padY = std::max(0.0f, (inner.y - drawH) * 0.5f);
        if (padX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
        if (padY > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padY);
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(g_keysTex)), ImVec2(drawW, drawH));
        ImGui::EndChild();
    } else {
        ImGui::TextColored(ImVec4(0.98f, 0.78f, 0.34f, 1.0f), "Quickstart Controls Summary (docs\\M1TP2_Quickstart_Keys.png not found)");
        ImGui::Separator();
        ImGui::Text("VIEWS:   F1=TC Pit | F2=Gnr Pit | F3=IVIS Map | F4=GPS Sight | F5=Aux | F6=VisBlk | F7=TC Hatch | F8=Chase");
        ImGui::Text("TANKS:   Shift+F1..F4 = Jump to Tank #1..#4 | F9/F10 = Prev/Next Vehicle (Shift = Platoon)");
        ImGui::Text("GUNNERY: 1=Sabot 2=HEAT 3=MPAT 4=STAFF 5=Coax | Space=Lase | Enter/LMB=Fire | T=Thermal | Z=10x X=3x");
        ImGui::Text("DRIVE:   Arrows=Drive | A=AutoDrive | S=Stop | C/V/B=Speed | G=Gun->Hull | Shift+G=Hull->Gun");
        ImGui::Text("PLATOON: F=Fire at Will | H=Hold Fire | Shift+W/L/C/V=Wedge/Line/Col/Vee | Bksp=Smoke Gren | /=Smoke Gen");
    }

    ImGui::End();
}

static void DrawDebugWindow() {
    ImGui::SetNextWindowPos(ImVec2(24.0f, 515.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 195.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Stats & Diagnostics", &g_showDebug)) {
        ImGui::End();
        return;
    }

    if (ImGui::Checkbox("Show Top-Left FPS / Renderer Stats Pill", &g_cfg.showStatsHud)) {
        SaveConfig();
    }
    ImGui::Separator();
    int winW = 0, winH = 0;
    VideoGetWindowSize(winW, winH);
    ImGui::Text("Window: %dx%d  |  FPS: %.1f", winW, winH, g_stats.fps);
    ImGui::Text("Glide 3D: %s  |  View %d (%s)  |  Hor+: %s",
                g_stats.inGlide3D ? "YES" : "NO",
                g_stats.currentView, GlideGetViewName(g_stats.currentView),
                g_stats.horPlusActive ? "ACTIVE" : "4:3");
    ImGui::Text("Draw Calls: %d  |  Triangles: %d  |  Cached Textures: %d",
                g_stats.drawCalls, g_stats.triangles, g_stats.cachedTex);
    ImGui::Text("Game Cfg Render Mode: %s  |  VFS Hits: %d",
                mem<uint32_t>(addr::g_GameCfgRenderMode) == 1 ? "GLIDE" :
                (mem<uint32_t>(addr::g_GameCfgRenderMode) == 2 ? "D3D->GLIDE" : "SOFTWARE"),
                g_stats.vfsHits);
    ImGui::End();
}

} // namespace

void OverlayInit(SDL_Window* window, SDL_GLContext glCtx) {
    if (g_inited || !window || !glCtx) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Keep clean, don't litter imgui.ini in game root
    ApplyMilitaryTheme();

    ImGui_ImplSDL3_InitForOpenGL(window, glCtx);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    g_inited = true;
    LogPrintf("[UI] Dear ImGui F12 Overlay initialized (OpenGL 3.3 Core)\n");
}

void OverlayShutdown() {
    if (!g_inited) return;
    if (g_keysTex) {
        glDeleteTextures(1, &g_keysTex);
        g_keysTex = 0;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_inited = false;
}

bool OverlayIsOpen() { return g_open; }

void OverlayToggle() {
    g_open = !g_open;
    if (g_inited) {
        // Draw ImGui's own software cursor while the overlay is open so M1TP2's Win32
        // ShowCursor(FALSE) counter can never hide the cursor in fullscreen OpenGL.
        ImGui::GetIO().MouseDrawCursor = g_open;
    }
    SDL_HideCursor();
    if (!g_open) {
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

    if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.scancode == SDL_SCANCODE_ESCAPE) {
        OverlayToggle();
        return true;
    }

    if (g_inited) {
        ImGui_ImplSDL3_ProcessEvent(&ev);
    }

    // Swallow all keyboard & mouse input while F12 overlay is open
    switch (ev.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return true;
        default:
            return false;
    }
}

void OverlayRender(int winW, int winH) {
    if (!g_inited || winW <= 0 || winH <= 0) return;

    CampaignSnapshot cs;
    bool showCampHud = false;
    if (g_cfg.campaignIntelHud && !g_stats.inGlide3D) {
        CampaignGetSnapshot(cs);
        showCampHud = (cs.valid && cs.inCampaignUi);
    }

    if (!g_open && !g_cfg.showStatsHud && !showCampHud) {
        return;
    }

    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, winW, winH);

    ImGui::GetIO().MouseDrawCursor = g_open;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (showCampHud) {
        DrawCampaignIntelSidebar(winW, winH, cs);
    }
    if (g_cfg.showStatsHud) {
        DrawStatsPill();
    }

    if (g_open) {
        if (ImGui::BeginMainMenuBar()) {
            ImGui::TextColored(ImVec4(0.32f, 0.96f, 0.50f, 1.0f), "M1TP2 NATIVE");
            ImGui::Separator();

            if (ImGui::BeginMenu("Game")) {
                if (ImGui::MenuItem("Launch War College (DSG)")) {
                    ShellExecuteA(nullptr, "open", "m1tp2_native.exe", "--war-college", nullptr, SW_SHOWNORMAL);
                    SetStatusMsg("Launching War College...");
                }
                if (ImGui::MenuItem("Open Official Manual (PDF)")) {
                    OpenExternalFile("M1_Tank_Platoon_II_Manual.pdf", "Official Manual PDF");
                }
                if (ImGui::MenuItem("Open v1.2 Patch Notes")) {
                    OpenExternalFile("M1TP2Readme.txt", "v1.2 Patch Notes");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Show Launcher on Startup", nullptr, &g_cfg.showLauncherOnStartup)) {
                    SaveConfig();
                }
                if (ImGui::MenuItem("Skip Intro Videos on Launch", nullptr, &g_cfg.skipIntro)) {
                    SaveConfig();
                }
                if (ImGui::MenuItem("Save Settings Now (m1tp2_recomp.ini)")) {
                    SaveConfig();
                    SetStatusMsg("Saved m1tp2_recomp.ini!");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit M1 Tank Platoon II")) {
                    SaveConfig();
                    using GameShutdownFn = int(__cdecl*)();
                    using CrtExitFn      = void(__cdecl*)(int);
                    reinterpret_cast<GameShutdownFn>(addr::GameShutdown)();
                    reinterpret_cast<CrtExitFn>(addr::CrtExit)(0);
                }
                ImGui::EndMenu();
            }

            ImGui::MenuItem("3D / Display",    nullptr, &g_showDisplay);
            ImGui::MenuItem("Audio Mixer",     nullptr, &g_showAudio);
            ImGui::MenuItem("Gunnery & FCS",   nullptr, &g_showGunnery);
            ImGui::MenuItem("Campaign & Mods", nullptr, &g_showCampaign);
            ImGui::MenuItem("Quick Keys",      nullptr, &g_showKeys);
            ImGui::MenuItem("Stats / Debug",   nullptr, &g_showDebug);

            ImGui::Separator();
            if (!g_statusBanner.empty() && SDL_GetTicks() < g_statusUntilMs) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", g_statusBanner.c_str());
            } else {
                ImGui::TextDisabled("[F11] 2D Filter | [Alt+Enter] Fullscreen | [F12 / Esc] Close");
            }
            ImGui::EndMainMenuBar();
        }

        if (g_showDisplay)  DrawDisplayWindow();
        if (g_showAudio)    DrawAudioWindow();
        if (g_showGunnery)  DrawGunneryWindow();
        if (g_showCampaign) DrawCampaignModsWindow();
        if (g_showKeys)     DrawQuickKeysWindow(winW, winH);
        if (g_showDebug)    DrawDebugWindow();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace m1

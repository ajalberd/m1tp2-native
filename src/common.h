#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>

namespace m1 {

namespace addr {
    // WinMain & Window / Input
    constexpr uint32_t WinMain              = 0x4116A0;
    constexpr uint32_t LoadGlide2x          = 0x411720;
    constexpr uint32_t CreateGameWindow     = 0x412390;
    constexpr uint32_t WndProc              = 0x412430;
    constexpr uint32_t SysCharHandler       = 0x412790;
    constexpr uint32_t SetLastKey           = 0x4127C0;
    constexpr uint32_t MsgPump              = 0x412840;
    constexpr uint32_t PushChar             = 0x411620;
    constexpr uint32_t PlaySmacker          = 0x4196C0;
    constexpr uint32_t MouseHandler         = 0x41AD50;
    constexpr uint32_t GameShutdown         = 0x434490;
    constexpr uint32_t IntroCallMps         = 0x4344FB; // video\mps.smk
    constexpr uint32_t IntroCallMain        = 0x43450E; // video\intro.smk
    constexpr uint32_t IntroCallCredits     = 0x434521; // video\credits.smk
    constexpr uint32_t GlideUpdatePalette   = 0x4239B0;
    constexpr uint32_t GlideInitEntry       = 0x423100;
    constexpr uint32_t D3DInitCallSite      = 0x41DE37;

    // DirectDraw 2D layer
    constexpr uint32_t ddRestore            = 0x410050;
    constexpr uint32_t ddLock               = 0x410060;
    constexpr uint32_t ddUnlock             = 0x4100D0;
    constexpr uint32_t ddGetSurfacePtr      = 0x410120;
    constexpr uint32_t ddFlipToGDISurface   = 0x410180;
    constexpr uint32_t ddFlip               = 0x410190;
    constexpr uint32_t ddBlit               = 0x4101E0;
    constexpr uint32_t ddBlast              = 0x410280;
    constexpr uint32_t ddWaitBltStatus      = 0x410360;
    constexpr uint32_t ddWaitVBlank         = 0x410380;
    constexpr uint32_t ddCreate             = 0x410510;
    constexpr uint32_t ddSetMode            = 0x410590;
    constexpr uint32_t ddGetPalette         = 0x410880;
    constexpr uint32_t ddSetPalette         = 0x410920;
    constexpr uint32_t ddSetPaletteRange    = 0x410A30;
    constexpr uint32_t ddShutdownAll        = 0x410AE0;
    constexpr uint32_t ddReleaseSurfaces    = 0x410B60;
    constexpr uint32_t ddReleaseAll         = 0x410BB0;

    // Smacker helper
    constexpr uint32_t SmackerGetPixelFmt   = 0x419920;

    // Tracked allocator & CRT free
    constexpr uint32_t TrackedAlloc         = 0x411500;
    constexpr uint32_t TrackedFree          = 0x411550;
    constexpr uint32_t TrackedFreeGen       = 0x411590;
    constexpr uint32_t TrackedIncGen        = 0x4115D0;
    constexpr uint32_t TrackedGetGen        = 0x4115E0;
    constexpr uint32_t TrackedFreeCurGen    = 0x4115F0;
    constexpr uint32_t GlideInitBadFreeCall = 0x423263;
    constexpr uint32_t CrtFree              = 0x45C730;
    constexpr uint32_t CrtExit              = 0x45CD50;

    // Catalog / VFS & CRT File I/O
    constexpr uint32_t CatalogFindEntry      = 0x4270D0;
    constexpr uint32_t CatalogLookup         = 0x427120;
    constexpr uint32_t CrtFsopen             = 0x45C530;
    constexpr uint32_t CrtFopen              = 0x45C560;
    constexpr uint32_t g_CatalogSlotBase     = 0x875A60;

    // 3D Projection & Frustum Culling
    constexpr uint32_t ClassifyFrustumVertex = 0x4599ED;
    constexpr uint32_t Project3DVertex       = 0x459A5D;

    // Globals
    constexpr uint32_t g_RenderMode         = 0x46EE18; // 0=2D/SW, 1=Glide, 2=D3D
    constexpr uint32_t g_BltSyncFlag        = 0x46EE30;
    constexpr uint32_t g_GlideTableBase     = 0x46F1A8;
    constexpr uint32_t g_KeyScanCode        = 0x46F390;
    constexpr uint32_t g_KeyVkCode          = 0x46F394;
    constexpr uint32_t g_KeyConsumed        = 0x46F398;
    constexpr uint32_t g_AllocGen           = 0x46F39C;
    constexpr uint32_t g_MouseResetFlag     = 0x4773B8;
    constexpr uint32_t g_MouseLockFlag      = 0x4773BC;
    constexpr uint32_t g_MouseLockFlag2     = 0x4773C0;
    constexpr uint32_t g_NearZ              = 0x477E88;
    constexpr uint32_t g_NoGlideFlag        = 0x4810FC;
    constexpr uint32_t g_GlideInited        = 0x48467C;
    constexpr uint32_t g_CurrentView        = 0x486150; // 0=TC,1=Gnr,2=Map,3=GPS,4=GAS,5=VisBlk,6=Hatch,7=Chase,8=Ext
    constexpr uint32_t g_ClipLeft           = 0x4F7EE4;
    constexpr uint32_t g_ClipTop            = 0x4F7EE8;
    constexpr uint32_t g_ClipRight          = 0x4F7EEC;
    constexpr uint32_t g_ClipBottom         = 0x4F7EF0;
    constexpr uint32_t g_CenterX            = 0x4F7EF4;
    constexpr uint32_t g_CenterY            = 0x4F7EF8;
    constexpr uint32_t g_ViewX              = 0x4F7EFC;
    constexpr uint32_t g_ViewY              = 0x4F7F00;
    constexpr uint32_t g_Palette768         = 0x518150;
    constexpr uint32_t g_SurfacePitch       = 0x518450;
    constexpr uint32_t g_ClientRect         = 0x518458;
    constexpr uint32_t g_Bpp                = 0x518468;
    constexpr uint32_t g_GlideLut565        = 0x5184F8;
    constexpr uint32_t g_SavedPalette768    = 0x518700;
    constexpr uint32_t g_ScreenWidth        = 0x518A00;
    constexpr uint32_t g_ScreenHeight       = 0x518A04;
    constexpr uint32_t g_MouseLatchedBtn    = 0x51A7A4;
    constexpr uint32_t g_GlidePalRGBA       = 0x51E9B8;
    constexpr uint32_t g_GameCfgRenderMode  = 0x872B88; // 0=SW, 1=Glide, 2=D3D
    constexpr uint32_t g_MouseY             = 0x87A3B0;
    constexpr uint32_t g_MouseX             = 0x87A3B4;
    constexpr uint32_t g_MouseBtn           = 0x87A3B8;
    constexpr uint32_t g_hWnd               = 0x87A560;
    constexpr uint32_t g_KeyModFlags        = 0x87A564;
    constexpr uint32_t g_MouseSavedY        = 0x87A568;
    constexpr uint32_t g_MouseSavedX        = 0x87A56C;
    constexpr uint32_t g_AppActive          = 0x87A570;
    constexpr uint32_t g_PrimarySurfObj     = 0x88D1F8;
    constexpr uint32_t g_BackSurfObj        = 0x88D1FC;
}

struct SurfaceObj {
    uint8_t* pixels;
    uint32_t kind;
    uint32_t pitch;
    uint32_t height;
    uint32_t bytesPerPx;
    uint32_t clipX0;
    uint32_t clipY0;
    uint32_t width;
    uint32_t clipY1;
    uint32_t reserved[3];
};

template <typename T>
inline T& mem(uint32_t address) {
    return *reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

enum AspectMode : int {
    ASPECT_FILL   = 0, // Fill entire screen
    ASPECT_4_3    = 1, // Original 4:3 with pillarbox
    ASPECT_16_10  = 2, // 16:10 middle ground
    ASPECT_CROP   = 3, // Zoom/crop 4:3 to fill 16:9 without stretch
};

enum Aspect3DMode : int {
    ASPECT3D_SMART_HORPLUS = 0, // True Hor+ 16:9 in Chase/Hatch/Ext; crisp 4:3 in Cockpits/Sights/Map
    ASPECT3D_ALL_HORPLUS   = 1, // True Hor+ 16:9 also in GPS Sight & Vision Blocks (Cockpits/GAS/Map stay 4:3)
    ASPECT3D_4_3           = 2, // Classic 4:3 everywhere
    ASPECT3D_STRETCH       = 3, // Legacy full-screen stretch
};

enum TexFilterMode : int {
    TEXFILT_TRILINEAR = 0,
    TEXFILT_BILINEAR  = 1,
    TEXFILT_NEAREST   = 2,
    TEXFILT_GAME      = 3,
};

enum FogModeOpt : int {
    FOG_DEFAULT = 0,
    FOG_EXT_2X  = 1,
    FOG_EXT_4X  = 2,
    FOG_OFF     = 3,
};

struct Config {
    int  windowWidth      = 0;     // 0 = native monitor resolution
    int  windowHeight     = 0;
    bool fullscreen       = true;  // Borderless fullscreen at native monitor resolution
    bool vsync            = true;
    int  menuAspectMode   = ASPECT_4_3;             // Keep 2D menus / briefings / maps clean 4:3
    int  aspectMode       = ASPECT3D_SMART_HORPLUS; // True Hor+ widescreen in wide views, 4:3 in cockpits/sights
    int  msaaSamples      = 4;     // 0, 2, 4, 8
    int  anisotropy       = 16;    // 1, 2, 4, 8, 16
    int  texFilter3D      = TEXFILT_TRILINEAR;
    bool linearFilter2D   = true;
    bool linearFilterLfb  = false; // Crisp cockpit/HUD text by default
    int  fogMode          = FOG_DEFAULT;
    float gamma           = 1.0f;
    bool forceGlideOnD3D  = true;
    bool skipIntro        = true;  // Skip startup logo/intro Smacker videos
    bool enableMods       = true;  // Enable native mods/ VFS override loader
    bool showStatsHud     = false;
};

struct FrameStats {
    float fps          = 0.0f;
    int   drawCalls    = 0;
    int   triangles    = 0;
    int   cachedTex    = 0;
    int   renderW      = 0;
    int   renderH      = 0;
    int   activeMsaa   = 0;
    int   maxMsaa      = 8;
    int   maxAniso     = 16;
    bool  inGlide3D      = false;
    bool  horPlusActive  = false;
    int   currentView    = 0;
    int   activeMods     = 0;
    int   modFiles       = 0;
    int   vfsHits        = 0;
};

extern Config     g_cfg;
extern FrameStats g_stats;

void LogInit();
void LogShutdown();
void LogPrintf(const char* fmt, ...);
void LoadConfig();
void SaveConfig();

} // namespace m1

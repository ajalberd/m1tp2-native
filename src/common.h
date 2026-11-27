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
    constexpr uint32_t CrtSbhFindBlock      = 0x45FBD0;
    constexpr uint32_t CrtSbhFreeBlock      = 0x45FC30;
    constexpr uint32_t g_CrtHeapHandle      = 0x88E354;
    constexpr uint32_t CrtExit              = 0x45CD50;

    // WinMM Joystick IAT & state
    constexpr uint32_t IatJoyGetPosEx       = 0x88F430;
    constexpr uint32_t IatJoyGetDevCapsA    = 0x88F434;
    constexpr uint32_t g_JoyInitState       = 0x4D95C4;

    // Frame Timing & 100 FPS High-Precision Timer
    constexpr uint32_t GetTicks10ms         = 0x438CB0; // stock: GetTickCount() / 10 (15.6ms Win32 quantum)
    constexpr uint32_t PatchFrameCapImm     = 0x41E386; // imm8 in `83 F8 01 7E E9` (cmp eax, 1; jle) -> 0x00 = 100 FPS

    // MPSSNDR.DLL Audio Wrapper & Function Table
    constexpr uint32_t SndLoad              = 0x449C10; // calls [0x571478] LoadSnd(slot, path, params)
    constexpr uint32_t SndPlay              = 0x449C60; // calls [0x571484] PlaySnd(slot, params)
    constexpr uint32_t SndSetVol            = 0x449CF0; // calls [0x5714A0] SetVol(slot, vol 0..400)
    constexpr uint32_t g_SndInitState       = 0x4D9580; // 0=uninit, 1=active, 2=paused
    constexpr uint32_t g_SndFnLoad          = 0x571478;
    constexpr uint32_t g_SndFnPlay          = 0x571484;
    constexpr uint32_t g_SndFnSetVol        = 0x5714A0;

    // Catalog / VFS & CRT File I/O
    constexpr uint32_t CatalogFindEntry      = 0x4270D0;
    constexpr uint32_t CatalogLookup         = 0x427120;
    constexpr uint32_t CrtFsopen             = 0x45C530;
    constexpr uint32_t CrtFopen              = 0x45C560;
    constexpr uint32_t g_CatalogSlotBase     = 0x875A60;

    // Draw Distance, LOD, 3DZ Widescreen Cull & Unit Data Table
    constexpr uint32_t PatchDrawRadiusImm    = 0x4447D6; // mov eax, imm32 (stock 0x7D000 + 0x177000 = 2.0 km)
    constexpr uint32_t Patch3dzCullTail      = 0x44DF1D; // tail of 0x44DEC0 after camX/Y/Z computed (replaces 32-bit shl-9 overflow cull)
    constexpr uint32_t g_3dzCullCtxPtr       = 0x565A1C; // active 3DZ render context pointer
    constexpr uint32_t g_3dzPosShift         = 0x4D9BBC; // right-shift applied to 3DZ world delta
    constexpr uint32_t g_TerrainLodStep      = 0x4AFF2C; // stock 179200 (0x2BC00)
    constexpr uint32_t g_UnitTypeTable       = 0x479A80; // 94 UnitType structs, stride 0x80
    constexpr int      kUnitTypeCount        = 94;
    constexpr size_t   kUnitTypeStride       = 0x80;

    // Dynamic Campaign Engine
    constexpr uint32_t ShowCampaignMap       = 0x4010E0;
    constexpr uint32_t CampaignMainLoop      = 0x429260;
    constexpr uint32_t CampaignPreBattle     = 0x429360;
    constexpr uint32_t CampaignGetWinPermille= 0x429F60;
    constexpr uint32_t CallAiFrontWinPct     = 0x42A395; // call 0x429F60 missing / 10 in SimulateOtherFronts!
    constexpr uint32_t CallShowCampMap1      = 0x4292E6;
    constexpr uint32_t CallShowCampMap2      = 0x429400;
    constexpr uint32_t CallCampPreBattle1    = 0x422F3F;
    constexpr uint32_t CallCampPreBattle2    = 0x42928C;
    constexpr uint32_t CallCampPreBattle3    = 0x452A2D;
    constexpr uint32_t CallCampMainLoop1     = 0x41E84B;
    constexpr uint32_t CallCampMainLoop2     = 0x41E86D;
    constexpr uint32_t CallCampMainLoop3     = 0x44A479;
    constexpr uint32_t g_FrontDataPtrs       = 0x873830; // 4 pointers to RED/GREEN/YELLOW/BLUE.DAT

    // 3D Projection & Frustum Culling
    constexpr uint32_t ClassifyFrustumVertex = 0x4599ED;
    constexpr uint32_t Project3DVertex       = 0x459A5D;

    // Gunnery Mouse Aim & Ballistic Dynamic Lead Hooks
    constexpr uint32_t MouseToStick          = 0x434980;
    constexpr uint32_t CallGunnerMouseStick  = 0x43594F; // call 0x434980 inside GPS/GAS sight input
    constexpr uint32_t CallStabYawMulDiv     = 0x41705D; // call 0x45B58B stabilized turret yaw delta
    constexpr uint32_t CallStabPitchMulDiv   = 0x4170A1; // call 0x45B58B stabilized gun pitch delta
    constexpr uint32_t CallDirectYawMulDiv   = 0x41728C; // call 0x45B58B direct turret yaw delta
    constexpr uint32_t CallDirectPitchMulDiv = 0x4172CA; // call 0x45B58B direct gun pitch delta
    constexpr uint32_t CallShellLeadSinMul   = 0x406B74; // call 0x458C51 leadSpeed = SinMul(lasedRange, omega)
    constexpr uint32_t SinMulFn              = 0x458C51; // int __cdecl SinMul(int val, int angle32)
    constexpr uint32_t MulDivFn              = 0x45B58B; // int __cdecl MulDiv(int a, int b, int c)
    constexpr uint32_t g_FrameDelta          = 0x4810CC; // frame dt in 1/100th sec ticks
    constexpr uint32_t g_GunnerMouseActive   = 0x493558; // 1 when left-clicked into sight for turret control

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
    ASPECT3D_SMART_HORPLUS = 0, // True Hor+ widescreen in Chase/Hatch/Ext; crisp 4:3 in Cockpits/Sights/Map
    ASPECT3D_ALL_HORPLUS   = 1, // True Hor+ widescreen also in GPS Sight & Vision Blocks (Cockpits/GAS/Map stay 4:3)
    ASPECT3D_4_3           = 2, // Classic 4:3 everywhere
    ASPECT3D_STRETCH       = 3, // Legacy full-screen stretch
};

enum MapAspectOpt : int {
    MAP_ASPECT_AUTO_NATIVE = 0, // Automatic aspect-native 4:3 (zero stretch on 16:9 / 21:9 / 32:9)
    MAP_ASPECT_4_3_LEGACY  = 1, // Legacy v0.2.0 value (normalized to MAP_ASPECT_AUTO_NATIVE)
    MAP_ASPECT_16_10       = 2, // Capped 16:10 wide (pillarboxed on 16:9 / 21:9)
    MAP_ASPECT_16_9        = 3, // Capped 16:9 wide (pillarboxed on 21:9)
    MAP_ASPECT_STRETCH     = 4, // Stretch to full monitor width
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

enum DrawDistOpt : int {
    DRAWDIST_DEFAULT  = 0, // 2.0 km stock radius, 1x terrain LOD
    DRAWDIST_EXTENDED = 1, // 3.0 km radius, 2x terrain LOD
    DRAWDIST_LONG     = 2, // 4.0 km radius, 3x terrain LOD
    DRAWDIST_MAX      = 3, // 6.0 km max radius, 4x terrain LOD
};

enum GunnerMouseOpt : int {
    GUNNER_PRECISION_STICK = 0, // Sub-pixel smooth virtual stick (fixes 10x deadzone & quantization)
    GUNNER_DIRECT_RAW      = 1, // Direct 1:1 raw SDL mouse aiming (capped to authentic M1A2 slew rate)
    GUNNER_STOCK_1998      = 2, // Unmodified 1998 virtual joystick
};

enum BallisticLeadOpt : int {
    LEAD_SMOOTH_FCS = 0, // 0.45s EMA-smoothed turret tracking rate + 35 m/s cross-speed clamp
    LEAD_MANUAL     = 1, // Disable hidden mouse-slew auto-lead (manual aim/lead only)
    LEAD_STOCK_1998 = 2, // Unmodified 1998 single-frame lead
};

struct Config {
    int  windowWidth      = 0;     // 0 = native monitor resolution
    int  windowHeight     = 0;
    bool fullscreen       = true;  // Borderless fullscreen at native monitor resolution
    bool vsync            = true;
    bool unlock100Fps     = true;  // High-precision QPC 10ms timer + 100 FPS cap (stock: 50 FPS / 36 FPS on 144Hz)
    int  menuAspectMode   = ASPECT_4_3;             // Keep 2D menus / briefings clean 4:3
    int  aspectMode       = ASPECT3D_SMART_HORPLUS; // True Hor+ widescreen in wide views, 4:3 in cockpits/sights
    int  mapAspectMode    = MAP_ASPECT_AUTO_NATIVE; // Aspect-native Tactical Map / Battleplanner (auto 4:3, zero stretch)
    int  msaaSamples      = 4;     // 0, 2, 4, 8
    int  anisotropy       = 16;    // 1, 2, 4, 8, 16
    int  texFilter3D      = TEXFILT_TRILINEAR;
    bool linearFilter2D   = true;
    bool linearFilterLfb  = false; // Crisp cockpit/HUD text by default
    int  drawDistMode     = DRAWDIST_EXTENDED; // 3.0 km extended draw distance & 2x terrain LOD by default
    int  fogMode          = FOG_DEFAULT;
    float gamma           = 1.0f;
    bool forceGlideOnD3D       = true;
    bool skipIntro             = true;  // Skip startup logo/intro Smacker videos
    bool showLauncherOnStartup = true;  // Open M1TP2 Native GUI Launcher on startup
    bool checkForUpdates       = true;  // Check GitHub Releases for newer versions on launcher startup
    bool enableMods            = true;  // Enable native mods/ VFS override loader
    bool fixCampaignFrontBug   = true;  // Fix 1998 AI-front /10 bug at 0x42A395
    bool campaignIntelHud      = true;  // Show Campaign Win/Loss & Front Intel on campaign screens
    int  gunnerMouseMode       = GUNNER_PRECISION_STICK;
    float gunnerMouseSens      = 1.0f;  // 0.25x .. 3.00x
    int  ballisticLeadMode     = LEAD_SMOOTH_FCS;
    bool disableJoystick       = true;  // Ignore WinMM joysticks/HOTAS/gamepads unless enabled
    bool  audioMute            = false;
    float audioMasterVol       = 1.0f;  // 0.00 .. 1.00
    float audioMusicVol        = 1.0f;  // Menu / briefing / victory music
    float audioSfxVol          = 1.0f;  // Weapons, explosions, UI clicks
    float audioEngineVol       = 1.0f;  // Turbine, diesel, treads, turret motor
    float audioVoiceVol        = 1.0f;  // Crew radio & speech
    bool showStatsHud          = false;
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
    int   unitOverrides  = 0;
};

extern Config     g_cfg;
extern FrameStats g_stats;

void LogInit();
void LogShutdown();
void LogPrintf(const char* fmt, ...);
void LoadConfig();
void SaveConfig();

} // namespace m1

#include "gunnery.h"
#include "common.h"
#include "patch.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace m1 {

namespace {

constexpr double kAngleUnitsPerDeg = 4294967296.0 / 360.0; // 2^32 = 360 deg
constexpr double kMaxM1SlewDegS    = 42.0;                 // Real M1A2 max turret traverse (~40-42 deg/s)
constexpr double kMaxM1PitchDegS   = 22.0;                 // Real M1A2 max gun elevation (~20-25 deg/s)
constexpr int32_t kMaxFcsLeadFp    = 35 * 256;             // 35 m/s (~126 km/h) max cross-speed clamp

using MouseToStickFn = void(__cdecl*)(const int32_t* vpRect, int32_t scaleX, int32_t scaleY);
using MulDivFnType   = int32_t(__cdecl*)(int32_t a, int32_t b, int32_t c);
using SinMulFnType   = int32_t(__cdecl*)(int32_t val, int32_t angle32);

// Sub-pixel mouse state updated from SDL3 events
float  g_mouseGameX   = 320.0f;
float  g_mouseGameY   = 240.0f;
double g_rawAccumDx   = 0.0;
double g_rawAccumDy   = 0.0;
bool   g_relModeOn    = false;

// Active player gunner state (identified from 0x43594F GPS/GAS sight handler)
void*  g_playerTankData   = nullptr;
void*  g_playerTankObj    = nullptr;
bool   g_gunnerInputReady = false;
bool   g_zoom10x          = false;
double g_preciseYawRate   = 0.0; // angle units / sec (Precision Stick)
double g_precisePitchRate = 0.0; // angle units / sec (Precision Stick)
double g_yawFracAccum     = 0.0;
double g_pitchFracAccum   = 0.0;
double g_pendingRawYaw    = 0.0; // angle units (Direct Raw Mouse)
double g_pendingRawPitch  = 0.0; // angle units (Direct Raw Mouse)

// Smooth FCS exponential moving average of player turret angular rate (angle units / sec)
double g_fcsOmegaEma      = 0.0;
uint64_t g_lastSightMs    = 0;

GunneryTelemetry g_tel{};

static double SmoothStickCurve(double n, double deadzone) {
    double a = std::fabs(n);
    if (a <= deadzone) return 0.0;
    double t = (a - deadzone) / (1.0 - deadzone);
    t = std::clamp(t, 0.0, 1.0);
    // Gentle linear base for ultra-fine long-range micro-tracking + smooth cubic ramp toward edges
    double mag = 0.12 * t + 0.88 * t * t * t;
    return (n < 0.0) ? -mag : mag;
}

} // namespace

// Called from naked thunk at 0x43594F (replaces call 0x434980 inside Gunner Primary/Aux Sight)
extern "C" void __cdecl Impl_GunnerMouseUpdate(
    const int32_t* vpRect, int32_t scaleX, int32_t scaleY, void* tankObj, void* tankData) {

    // Always run stock MouseToStick so click-to-capture (0x493558), right-click release,
    // fire/lase buttons (0x750E70), and viewport bounds stay 100% authentic.
    reinterpret_cast<MouseToStickFn>(addr::MouseToStick)(vpRect, scaleX, scaleY);

    g_playerTankObj  = tankObj;
    g_playerTankData = tankData;
    g_lastSightMs    = SDL_GetTicks();

    bool active = (mem<uint32_t>(addr::g_GunnerMouseActive) != 0);
    bool is10x  = false;
    if (tankData) {
        is10x = (*reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(tankData) + 0x100) == 1);
    }
    g_zoom10x          = is10x;
    g_tel.sightActive  = active;
    g_tel.zoom10x      = is10x;

    if (!active || g_cfg.gunnerMouseMode == GUNNER_STOCK_1998 || !vpRect) {
        g_gunnerInputReady = false;
        g_preciseYawRate   = 0.0;
        g_precisePitchRate = 0.0;
        g_pendingRawYaw    = 0.0;
        g_pendingRawPitch  = 0.0;
        g_rawAccumDx       = 0.0;
        g_rawAccumDy       = 0.0;
        g_tel.stickNormX   = 0.0f;
        g_tel.stickNormY   = 0.0f;
        return;
    }

    float sens = std::clamp(g_cfg.gunnerMouseSens, 0.25f, 3.0f);
    g_gunnerInputReady = true;

    if (g_cfg.gunnerMouseMode == GUNNER_PRECISION_STICK) {
        // Sub-pixel floating-point distance from viewport center (eliminates 640x480 quantization
        // and the stock integer /8 truncation deadzone in 10x zoom!)
        double left = static_cast<double>(vpRect[5]); // +0x14
        double top  = static_cast<double>(vpRect[6]); // +0x18
        double w    = std::max(1.0, static_cast<double>(vpRect[7])); // +0x1C
        double h    = std::max(1.0, static_cast<double>(vpRect[8])); // +0x20
        double cx   = left + w * 0.5;
        double cy   = top  + h * 0.5;

        double nx = std::clamp((static_cast<double>(g_mouseGameX) - cx) / (w * 0.5), -1.0, 1.0);
        double ny = std::clamp((static_cast<double>(g_mouseGameY) - cy) / (h * 0.5), -1.0, 1.0);
        g_tel.stickNormX = static_cast<float>(nx);
        g_tel.stickNormY = static_cast<float>(ny);

        // Tiny 0.8% deadzone (~2-3 physical pixels at 1080p) vs stock ~80px deadzone
        double sx = SmoothStickCurve(nx, 0.008);
        double sy = SmoothStickCurve(ny, 0.008);

        // Positive mouse X (right) -> negative yaw (matches stock 0x435A37 neg eax)
        // Positive mouse Y (down)  -> negative pitch (matches stock 0x434AD0 neg eax)
        double maxYawDegS   = (is10x ? 12.0 : 34.0) * sens;
        double maxPitchDegS = (is10x ?  6.0 : 16.0) * sens;
        maxYawDegS   = std::min(maxYawDegS,   kMaxM1SlewDegS);
        maxPitchDegS = std::min(maxPitchDegS, kMaxM1PitchDegS);

        g_preciseYawRate   = -sx * maxYawDegS   * kAngleUnitsPerDeg;
        g_precisePitchRate = -sy * maxPitchDegS * kAngleUnitsPerDeg;
    } else if (g_cfg.gunnerMouseMode == GUNNER_DIRECT_RAW) {
        // Direct 1:1 raw SDL relative mouse delta aiming (auto-scaled for 3x vs 10x zoom)
        double degPerPx = (is10x ? 0.015 : 0.048) * sens;
        g_pendingRawYaw   += -g_rawAccumDx * degPerPx * kAngleUnitsPerDeg;
        g_pendingRawPitch += -g_rawAccumDy * degPerPx * kAngleUnitsPerDeg;
        g_rawAccumDx = 0.0;
        g_rawAccumDy = 0.0;
        g_tel.stickNormX = 0.0f;
        g_tel.stickNormY = 0.0f;
    }
}

extern "C" int32_t __cdecl Impl_TurretYawMulDiv(
    int32_t rate, int32_t dt, int32_t denom, void* tankObj, void* tankData) {

    int32_t stockDelta = reinterpret_cast<MulDivFnType>(addr::MulDivFn)(rate, dt, denom);

    // Never touch AI tanks or non-player vehicles
    if (!tankData || tankData != g_playerTankData) {
        return stockDelta;
    }
    g_playerTankObj = tankObj;

    // Track player tank speed for telemetry
    if (tankObj) {
        const uint8_t* t = static_cast<const uint8_t*>(tankObj);
        float vx = *reinterpret_cast<const int32_t*>(t + 0x58) / 256.0f;
        float vz = *reinterpret_cast<const int32_t*>(t + 0x60) / 256.0f;
        g_tel.tankSpeedMps = std::sqrt(vx * vx + vz * vz);
    }

    int32_t delta = stockDelta;
    uint64_t nowMs = SDL_GetTicks();
    bool sightRecent = (nowMs - g_lastSightMs < 120);
    bool useOverride = g_gunnerInputReady && sightRecent &&
                       (mem<uint32_t>(addr::g_GunnerMouseActive) != 0) &&
                       (g_cfg.gunnerMouseMode != GUNNER_STOCK_1998);

    double dtSec = std::clamp(static_cast<double>(dt) / std::max(1.0, static_cast<double>(denom)), 0.001, 0.25);

    if (useOverride) {
        if (g_cfg.gunnerMouseMode == GUNNER_PRECISION_STICK) {
            g_yawFracAccum += g_preciseYawRate * dtSec;
            double whole = std::trunc(g_yawFracAccum);
            g_yawFracAccum -= whole;
            delta = static_cast<int32_t>(whole);
        } else if (g_cfg.gunnerMouseMode == GUNNER_DIRECT_RAW) {
            double maxStep = kMaxM1SlewDegS * kAngleUnitsPerDeg * dtSec;
            double step = std::clamp(g_pendingRawYaw, -maxStep, maxStep);
            g_pendingRawYaw -= step;
            g_yawFracAccum += step;
            double whole = std::trunc(g_yawFracAccum);
            g_yawFracAccum -= whole;
            delta = static_cast<int32_t>(whole);
        }
    } else {
        g_yawFracAccum = 0.0;
    }

    // Update Smooth FCS exponential moving average (0.45s time constant)
    double instOmega = static_cast<double>(delta) / dtSec;
    double alpha = 1.0 - std::exp(-dtSec / 0.45);
    g_fcsOmegaEma += alpha * (instOmega - g_fcsOmegaEma);

    g_tel.instOmegaDegS   = static_cast<float>(instOmega / kAngleUnitsPerDeg);
    g_tel.smoothOmegaDegS = static_cast<float>(g_fcsOmegaEma / kAngleUnitsPerDeg);
    return delta;
}

extern "C" int32_t __cdecl Impl_GunPitchMulDiv(
    int32_t rate, int32_t dt, int32_t denom, void* tankObj, void* tankData) {

    int32_t stockDelta = reinterpret_cast<MulDivFnType>(addr::MulDivFn)(rate, dt, denom);

    if (!tankData || tankData != g_playerTankData) {
        return stockDelta;
    }

    uint64_t nowMs = SDL_GetTicks();
    bool sightRecent = (nowMs - g_lastSightMs < 120);
    bool useOverride = g_gunnerInputReady && sightRecent &&
                       (mem<uint32_t>(addr::g_GunnerMouseActive) != 0) &&
                       (g_cfg.gunnerMouseMode != GUNNER_STOCK_1998);

    if (!useOverride) {
        g_pitchFracAccum = 0.0;
        return stockDelta;
    }

    double dtSec = std::clamp(static_cast<double>(dt) / std::max(1.0, static_cast<double>(denom)), 0.001, 0.25);

    if (g_cfg.gunnerMouseMode == GUNNER_PRECISION_STICK) {
        g_pitchFracAccum += g_precisePitchRate * dtSec;
        double whole = std::trunc(g_pitchFracAccum);
        g_pitchFracAccum -= whole;
        return static_cast<int32_t>(whole);
    } else if (g_cfg.gunnerMouseMode == GUNNER_DIRECT_RAW) {
        double maxStep = kMaxM1PitchDegS * kAngleUnitsPerDeg * dtSec;
        double step = std::clamp(g_pendingRawPitch, -maxStep, maxStep);
        g_pendingRawPitch -= step;
        g_pitchFracAccum += step;
        double whole = std::trunc(g_pitchFracAccum);
        g_pitchFracAccum -= whole;
        return static_cast<int32_t>(whole);
    }
    return stockDelta;
}

extern "C" int32_t __cdecl Impl_ShellLeadSinMul(
    int32_t lasedRange, int32_t stockOmega, void* shooterTank) {

    auto origSinMul = reinterpret_cast<SinMulFnType>(addr::SinMulFn);
    int32_t stockLead = origSinMul(lasedRange, stockOmega);

    // Only apply FCS lead fix to the player's tank (AI tanks already use lockedTarget velocity)
    if (!shooterTank || shooterTank != g_playerTankObj) {
        return stockLead;
    }

    g_tel.lastLasedRangeM  = static_cast<float>(lasedRange) / 256.0f;
    g_tel.lastStockLeadMps = static_cast<float>(stockLead) / 256.0f;

    int32_t outLead = stockLead;
    if (g_cfg.ballisticLeadMode == LEAD_MANUAL) {
        outLead = 0;
    } else if (g_cfg.ballisticLeadMode == LEAD_SMOOTH_FCS) {
        // Use 0.45s EMA-smoothed angular tracking rate + physical cross-speed clamp (35 m/s)
        double clampedOmega = std::clamp(g_fcsOmegaEma, -600000000.0, 600000000.0);
        int32_t smoothLead  = origSinMul(lasedRange, static_cast<int32_t>(clampedOmega));
        outLead = std::clamp(smoothLead, -kMaxFcsLeadFp, kMaxFcsLeadFp);
    }

    g_tel.lastLeadMps = static_cast<float>(outLead) / 256.0f;
    return outLead;
}

// ==================== Register-Preserving Naked x86 Thunks ====================

// 0x43594F: call 0x434980(vpRect, scaleX, scaleY) with ebx=tankObj, edi=tankData
__attribute__((naked)) static void Thunk_GunnerMouseUpdate() {
    asm volatile(
        "pushl %%edi\n\t"          // arg4: tankData
        "pushl %%ebx\n\t"          // arg3: tankObj
        "pushl 20(%%esp)\n\t"      // arg2: scaleY (orig esp+12, +8 pushed = +20)
        "pushl 20(%%esp)\n\t"      // arg1: scaleX (orig esp+8,  +12 pushed = +20)
        "pushl 20(%%esp)\n\t"      // arg0: vpRect (orig esp+4,  +16 pushed = +20)
        "call _Impl_GunnerMouseUpdate\n\t"
        "addl $20, %%esp\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

// 0x41705D / 0x41728C: call MulDiv(rate, dt, denom) with esi=tankObj, edi=tankData
__attribute__((naked)) static void Thunk_TurretYawMulDiv() {
    asm volatile(
        "pushl %%ecx\n\t"
        "pushl %%edx\n\t"
        "pushl %%edi\n\t"          // arg4: tankData
        "pushl %%esi\n\t"          // arg3: tankObj
        "pushl 28(%%esp)\n\t"      // arg2: denom (orig esp+12, +16 pushed = +28)
        "pushl 28(%%esp)\n\t"      // arg1: dt    (orig esp+8,  +20 pushed = +28)
        "pushl 28(%%esp)\n\t"      // arg0: rate  (orig esp+4,  +24 pushed = +28)
        "call _Impl_TurretYawMulDiv\n\t"
        "addl $20, %%esp\n\t"
        "popl %%edx\n\t"
        "popl %%ecx\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

// 0x4170A1 / 0x4172CA: call MulDiv(rate, dt, denom) with esi=tankObj, edi=tankData
__attribute__((naked)) static void Thunk_GunPitchMulDiv() {
    asm volatile(
        "pushl %%ecx\n\t"
        "pushl %%edx\n\t"
        "pushl %%edi\n\t"          // arg4: tankData
        "pushl %%esi\n\t"          // arg3: tankObj
        "pushl 28(%%esp)\n\t"      // arg2: denom
        "pushl 28(%%esp)\n\t"      // arg1: dt
        "pushl 28(%%esp)\n\t"      // arg0: rate
        "call _Impl_GunPitchMulDiv\n\t"
        "addl $20, %%esp\n\t"
        "popl %%edx\n\t"
        "popl %%ecx\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

// 0x406B74: call SinMul(lasedRange, stockOmega); shooterTank* is at [orig_esp + 0x60]
__attribute__((naked)) static void Thunk_ShellLeadSinMul() {
    asm volatile(
        "pushl %%ecx\n\t"
        "pushl %%edx\n\t"
        "pushl 0x68(%%esp)\n\t"    // arg2: shooterTank* (orig esp+0x60, +8 pushed = +0x68)
        "pushl 20(%%esp)\n\t"      // arg1: stockOmega   (orig esp+8,    +12 pushed = +20)
        "pushl 20(%%esp)\n\t"      // arg0: lasedRange   (orig esp+4,    +16 pushed = +20)
        "call _Impl_ShellLeadSinMul\n\t"
        "addl $12, %%esp\n\t"
        "popl %%edx\n\t"
        "popl %%ecx\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

void GunneryInstallHooks() {
    InstallCall(addr::CallGunnerMouseStick,  reinterpret_cast<void*>(&Thunk_GunnerMouseUpdate));
    InstallCall(addr::CallStabYawMulDiv,     reinterpret_cast<void*>(&Thunk_TurretYawMulDiv));
    InstallCall(addr::CallDirectYawMulDiv,   reinterpret_cast<void*>(&Thunk_TurretYawMulDiv));
    InstallCall(addr::CallStabPitchMulDiv,   reinterpret_cast<void*>(&Thunk_GunPitchMulDiv));
    InstallCall(addr::CallDirectPitchMulDiv, reinterpret_cast<void*>(&Thunk_GunPitchMulDiv));
    InstallCall(addr::CallShellLeadSinMul,   reinterpret_cast<void*>(&Thunk_ShellLeadSinMul));

    LogPrintf("[GUNNERY] Installed precision mouse & ballistic FCS hooks (mouseMode=%d sens=%.2f leadMode=%d)\n",
              g_cfg.gunnerMouseMode, g_cfg.gunnerMouseSens, g_cfg.ballisticLeadMode);
}

void GunneryOnMouseMotion(float gameFloatX, float gameFloatY, float relDx, float relDy) {
    g_mouseGameX = gameFloatX;
    g_mouseGameY = gameFloatY;
    if (g_cfg.gunnerMouseMode == GUNNER_DIRECT_RAW &&
        mem<uint32_t>(addr::g_GunnerMouseActive) != 0) {
        g_rawAccumDx += static_cast<double>(relDx);
        g_rawAccumDy += static_cast<double>(relDy);
    } else {
        g_rawAccumDx = 0.0;
        g_rawAccumDy = 0.0;
    }
}

void GunnerySyncRelativeMouse(SDL_Window* win, bool overlayOpen) {
    if (!win) return;
    int view = mem<int32_t>(addr::g_CurrentView);
    bool inGunnerSight = (g_stats.inGlide3D && (view == 3 || view == 4));
    bool sightCaptured = (mem<uint32_t>(addr::g_GunnerMouseActive) != 0);
    bool wantRel = (!overlayOpen &&
                    mem<uint32_t>(addr::g_AppActive) != 0 &&
                    g_cfg.gunnerMouseMode == GUNNER_DIRECT_RAW &&
                    inGunnerSight &&
                    sightCaptured);
    if (wantRel != g_relModeOn) {
        g_relModeOn = wantRel;
        SDL_SetWindowRelativeMouseMode(win, wantRel);
        g_rawAccumDx = 0.0;
        g_rawAccumDy = 0.0;
        g_pendingRawYaw = 0.0;
        g_pendingRawPitch = 0.0;
    }
}

void GunneryGetTelemetry(GunneryTelemetry& out) {
    out = g_tel;
    if (SDL_GetTicks() - g_lastSightMs > 250) {
        out.sightActive = false;
    }
}

} // namespace m1

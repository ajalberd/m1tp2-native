#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

namespace m1 {

struct GunneryTelemetry {
    bool  sightActive      = false;
    bool  zoom10x          = false;
    float instOmegaDegS    = 0.0f;
    float smoothOmegaDegS  = 0.0f;
    float lastLeadMps      = 0.0f;
    float lastStockLeadMps = 0.0f;
    float lastLasedRangeM  = 0.0f;
    float tankSpeedMps     = 0.0f;
    float stickNormX       = 0.0f;
    float stickNormY       = 0.0f;
};

void GunneryInstallHooks();
void GunneryOnMouseMotion(float gameFloatX, float gameFloatY, float relDx, float relDy);
void GunnerySyncRelativeMouse(SDL_Window* win, bool overlayOpen);
void GunneryGetTelemetry(GunneryTelemetry& out);

} // namespace m1

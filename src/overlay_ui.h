#pragma once
#include <SDL3/SDL.h>

namespace m1 {

void OverlayInit();
void OverlayShutdown();
bool OverlayIsOpen();
void OverlayToggle();
// Returns true if the event was consumed by the Recomp menu
bool OverlayHandleEvent(const SDL_Event& ev);
// Renders both the optional FPS HUD and the F12 Recomp Settings Menu in OpenGL 3.3
void OverlayRender(int winW, int winH);

} // namespace m1

#pragma once
#include <SDL3/SDL.h>

namespace m1 {

void OverlayInit(SDL_Window* window, SDL_GLContext glCtx);
void OverlayShutdown();
bool OverlayIsOpen();
void OverlayToggle();
// Returns true if the event was consumed by the Recomp menu
bool OverlayHandleEvent(const SDL_Event& ev);
// Renders Campaign Intel HUD, optional FPS HUD, and the F12 Dear ImGui Recomp Menu
void OverlayRender(int winW, int winH);

} // namespace m1

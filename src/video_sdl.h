#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

namespace m1 {

void VideoSDLInstallHooks();
void VideoSDLShutdown();
void VideoApplyDisplaySettings();

SDL_Window* VideoGetWindow();
void VideoGetViewport(bool is3D, int& vx, int& vy, int& vw, int& vh);
void VideoGet43Viewport(int& vx, int& vy, int& vw, int& vh);
void VideoGetWindowSize(int& w, int& h);
void VideoPresentSwap();
void VideoPresent2DNow();
void VideoDrawFullscreenQuad();

} // namespace m1

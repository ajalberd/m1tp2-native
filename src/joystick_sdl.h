#pragma once
#include <cstdint>

namespace m1 {

void JoystickSDLInstallHooks();
void JoystickSDLShutdown();
void JoystickSDLOnConfigChanged();
const char* JoystickSDLGetActiveName();

} // namespace m1

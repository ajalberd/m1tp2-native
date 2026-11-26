#pragma once

namespace m1 {

void GlideGLInstallHooks();
void GlideGLShutdown();
void GlideApplySettings();
bool GlideIsHorPlusView();
const char* GlideGetViewName(int viewId);

} // namespace m1

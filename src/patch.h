#pragma once
#include <cstdint>
#include <cstddef>

namespace m1 {

bool WriteBytes(uint32_t addr, const void* data, size_t len);
bool InstallJump(uint32_t addr, void* target);
bool InstallCall(uint32_t addr, void* target);

} // namespace m1

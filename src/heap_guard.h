#pragma once
#include <cstddef>

namespace m1 {

void   HeapGuardInit();
void   HeapGuardShutdown();
size_t HeapGuardQueryReadableBytes(const void* ptr);

} // namespace m1

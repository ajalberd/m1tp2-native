#include "heap_guard.h"
#include "common.h"
#include "patch.h"
#include <windows.h>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <cstring>

namespace m1 {

namespace {

CRITICAL_SECTION g_heapLock;
bool g_heapLockInit = false;

struct AllocInfo {
    uint32_t gen;
    size_t size;
};

// Track all allocations from M1TP2's tracked allocator (0x411500) with unlimited capacity
std::unordered_map<void*, AllocInfo> g_trackedAllocs;
uint32_t g_doubleFreeCount = 0;
uint32_t g_unknownFreeCount = 0;

using CrtFreeFn = void(__cdecl*)(void*);

void* __cdecl Hook_TrackedAlloc(size_t size) {
    if (size == 0) size = 16;
    // Zero-initialize and add 64 bytes tail padding to absorb small off-by-one/overrun bugs
    // while keeping allocations cleanly separated from M1TP2's CRT heap.
    void* p = std::calloc(1, size + 64);
    if (!p) {
        LogPrintf("[HEAP] TrackedAlloc FAILED size=%zu\n", size);
        return nullptr;
    }
    EnterCriticalSection(&g_heapLock);
    g_trackedAllocs[p] = AllocInfo{ mem<uint32_t>(addr::g_AllocGen), size };
    LeaveCriticalSection(&g_heapLock);
    return p;
}

void __cdecl Hook_TrackedFree(void* p) {
    if (!p) return;
    EnterCriticalSection(&g_heapLock);
    auto it = g_trackedAllocs.find(p);
    if (it != g_trackedAllocs.end()) {
        g_trackedAllocs.erase(it);
        LeaveCriticalSection(&g_heapLock);
        std::free(p);
        return;
    }
    LeaveCriticalSection(&g_heapLock);

    // Not in our tracked map: could be a double-free or a pointer allocated by CRT malloc.
    // Log it and avoid crashing on already-freed tracked pointers.
    g_unknownFreeCount++;
    if (g_unknownFreeCount <= 32) {
        LogPrintf("[HEAP] TrackedFree on untracked/already-freed ptr %p (count=%u)\n", p, g_unknownFreeCount);
    }
}

void __cdecl Hook_TrackedFreeGen(uint32_t gen) {
    std::vector<void*> toFree;
    EnterCriticalSection(&g_heapLock);
    for (auto it = g_trackedAllocs.begin(); it != g_trackedAllocs.end(); ) {
        if (it->second.gen == gen) {
            toFree.push_back(it->first);
            it = g_trackedAllocs.erase(it);
        } else {
            ++it;
        }
    }
    LeaveCriticalSection(&g_heapLock);
    for (void* p : toFree) {
        std::free(p);
    }
}

void __cdecl Hook_GlideInitFreeFix(void* p) {
    // 0x423263 originally called raw crt_free on a TrackedAlloc buffer without removing it
    // from the tracking table, causing a double-free later! Route cleanly to TrackedFree.
    Hook_TrackedFree(p);
}

} // namespace

void HeapGuardInit() {
    InitializeCriticalSection(&g_heapLock);
    g_heapLockInit = true;
    g_trackedAllocs.reserve(8192);

    InstallJump(addr::TrackedAlloc, reinterpret_cast<void*>(&Hook_TrackedAlloc));
    InstallJump(addr::TrackedFree, reinterpret_cast<void*>(&Hook_TrackedFree));
    InstallJump(addr::TrackedFreeGen, reinterpret_cast<void*>(&Hook_TrackedFreeGen));
    // Fix the confirmed Glide init double-free bug at 0x423263!
    InstallCall(addr::GlideInitBadFreeCall, reinterpret_cast<void*>(&Hook_GlideInitFreeFix));

    LogPrintf("[HEAP] Tracked allocator guard installed (4096-entry cap removed, 0x423263 double-free fixed)\n");
}

void HeapGuardShutdown() {
    if (g_heapLockInit) {
        LogPrintf("[HEAP] Shutdown: remaining tracked=%zu, unknownFrees=%u, doubleFrees=%u\n",
                  g_trackedAllocs.size(), g_unknownFreeCount, g_doubleFreeCount);
        DeleteCriticalSection(&g_heapLock);
        g_heapLockInit = false;
    }
}

} // namespace m1

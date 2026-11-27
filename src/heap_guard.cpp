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
uint32_t g_allocFailCount = 0;
uint32_t g_crtCrossHeapFrees = 0;
uint32_t g_crtInvalidFrees = 0;

using CrtFreeFn        = void(__cdecl*)(void*);
using CrtSbhFindFn     = void*(__cdecl*)(void* ptr, void** outBlock, void** outHdr);
using CrtSbhFreeFn     = void(__cdecl*)(void* block, void* hdr, void* res);

void* __cdecl Hook_TrackedAlloc(size_t size) {
    if (size == 0) size = 16;
    // Zero-initialize and add 64 bytes tail padding to absorb small off-by-one/overrun bugs
    // while keeping allocations cleanly separated from M1TP2's CRT heap.
    void* p = std::calloc(1, size + 64);
    if (!p) {
        g_allocFailCount++;
        if (g_allocFailCount <= 16 || (g_allocFailCount % 100) == 0) {
            LogPrintf("[HEAP] TrackedAlloc FAILED size=%zu (0x%zX) caller=%p (count=%u)\n",
                      size, size, __builtin_return_address(0), g_allocFailCount);
        }
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

void __cdecl Hook_CrtFree(void* p) {
    if (!p) return;

    // 1. If this pointer belongs to our TrackedAlloc heap, free it safely here instead of
    //    passing a foreign heap pointer into M1TP2.exe's VC5 __crtheap (fixes ntdll!RtlFreeHeap crash!).
    if (g_heapLockInit) {
        EnterCriticalSection(&g_heapLock);
        auto it = g_trackedAllocs.find(p);
        if (it != g_trackedAllocs.end()) {
            g_trackedAllocs.erase(it);
            g_crtCrossHeapFrees++;
            LeaveCriticalSection(&g_heapLock);
            std::free(p);
            return;
        }
        LeaveCriticalSection(&g_heapLock);
    }

    // 2. Replicate M1TP2.exe's VC5 CRT free (0x45C730): check small-block heap first
    void* sbhBlock = nullptr;
    void* sbhHdr   = nullptr;
    void* sbhRes   = reinterpret_cast<CrtSbhFindFn>(addr::CrtSbhFindBlock)(p, &sbhBlock, &sbhHdr);
    if (sbhRes) {
        reinterpret_cast<CrtSbhFreeFn>(addr::CrtSbhFreeBlock)(sbhBlock, sbhHdr, sbhRes);
        return;
    }

    // 3. Otherwise free from M1TP2's __crtheap ONLY if HeapValidate confirms it is a live block
    HANDLE hCrtHeap = mem<HANDLE>(addr::g_CrtHeapHandle);
    if (hCrtHeap && HeapValidate(hCrtHeap, 0, p)) {
        HeapFree(hCrtHeap, 0, p);
    } else {
        g_crtInvalidFrees++;
        if (g_crtInvalidFrees <= 16) {
            LogPrintf("[HEAP] Guarded crt_free skipped invalid/already-freed ptr %p caller=%p (count=%u)\n",
                      p, __builtin_return_address(0), g_crtInvalidFrees);
        }
    }
}

} // namespace

void HeapGuardInit() {
    InitializeCriticalSection(&g_heapLock);
    g_heapLockInit = true;
    g_trackedAllocs.reserve(8192);

    InstallJump(addr::TrackedAlloc, reinterpret_cast<void*>(&Hook_TrackedAlloc));
    InstallJump(addr::TrackedFree, reinterpret_cast<void*>(&Hook_TrackedFree));
    InstallJump(addr::TrackedFreeGen, reinterpret_cast<void*>(&Hook_TrackedFreeGen));
    InstallJump(addr::CrtFree, reinterpret_cast<void*>(&Hook_CrtFree));
    // Fix the confirmed Glide init double-free bug at 0x423263!
    InstallCall(addr::GlideInitBadFreeCall, reinterpret_cast<void*>(&Hook_GlideInitFreeFix));

    LogPrintf("[HEAP] Tracked allocator & guarded crt_free installed (4096-entry cap removed, cross-heap & double-free safe)\n");
}

size_t HeapGuardQueryReadableBytes(const void* ptr) {
    if (!ptr) return 0;

    // 1. Fast path: check tracked allocations (including CreateSurface's +4/+8 alignment shift at 0x44FC5B)
    if (g_heapLockInit) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        EnterCriticalSection(&g_heapLock);
        for (uintptr_t back : { uintptr_t(0), uintptr_t(4), uintptr_t(8), uintptr_t(12), uintptr_t(16) }) {
            if (addr >= back) {
                auto it = g_trackedAllocs.find(reinterpret_cast<void*>(addr - back));
                if (it != g_trackedAllocs.end()) {
                    size_t avail = (it->second.size > back) ? (it->second.size - back) : 0;
                    LeaveCriticalSection(&g_heapLock);
                    return avail;
                }
            }
        }
        LeaveCriticalSection(&g_heapLock);
    }

    // 2. Fallback: VirtualQuery contiguous readable region size (prevents page-fault OOB reads)
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    DWORD prot = mbi.Protect & 0xFFu;
    if (prot == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD)) return 0;
    uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    uintptr_t pAddr = reinterpret_cast<uintptr_t>(ptr);
    return (regionEnd > pAddr) ? static_cast<size_t>(regionEnd - pAddr) : 0;
}

void HeapGuardShutdown() {
    if (g_heapLockInit) {
        LogPrintf("[HEAP] Shutdown: remaining tracked=%zu, unknownFrees=%u, crossHeapFrees=%u, invalidCrtFrees=%u\n",
                  g_trackedAllocs.size(), g_unknownFreeCount, g_crtCrossHeapFrees, g_crtInvalidFrees);
        DeleteCriticalSection(&g_heapLock);
        g_heapLockInit = false;
    }
}

} // namespace m1

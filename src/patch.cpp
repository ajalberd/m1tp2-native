#include "patch.h"
#include "common.h"
#include <windows.h>
#include <cstring>

namespace m1 {

bool WriteBytes(uint32_t addr, const void* data, size_t len) {
    void* ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(addr));
    DWORD oldProt = 0;
    if (!VirtualProtect(ptr, len, PAGE_EXECUTE_READWRITE, &oldProt)) {
        LogPrintf("ERROR: VirtualProtect failed at 0x%08X (err=%lu)\n", addr, GetLastError());
        return false;
    }
    std::memcpy(ptr, data, len);
    VirtualProtect(ptr, len, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), ptr, len);
    return true;
}

bool InstallJump(uint32_t addr, void* target) {
    uint8_t jmp[5];
    jmp[0] = 0xE9;
    int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(target) - (static_cast<uintptr_t>(addr) + 5));
    std::memcpy(&jmp[1], &rel, 4);
    return WriteBytes(addr, jmp, sizeof(jmp));
}

bool InstallCall(uint32_t addr, void* target) {
    uint8_t call_ins[5];
    call_ins[0] = 0xE8;
    int32_t rel = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(target) - (static_cast<uintptr_t>(addr) + 5));
    std::memcpy(&call_ins[1], &rel, 4);
    return WriteBytes(addr, call_ins, sizeof(call_ins));
}

} // namespace m1

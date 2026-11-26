// Zero-CRT thin native bootstrap ddraw.dll (ZERO DDrawCompat / DxWnd / dgVoodoo2!)
// Hooks M1TP2.exe's WinMain (0x4116A0) on attach; inside Hook_WinMain (outside loader lock)
// loads m1tp2_recomp.dll to initialize our native SDL3 + OpenGL 3.3 runtime.

#include <windows.h>
#include <stdint.h>

#define WINMAIN_ADDR 0x4116A0u

static uint8_t g_origWinMain[5];

typedef int (WINAPI *WinMainFn)(HINSTANCE, HINSTANCE, LPSTR, int);
typedef void (__cdecl *RecompInitFn)(void);

static void copy5(void* dst, const void* src) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (int i = 0; i < 5; ++i) d[i] = s[i];
}

static void Patch5(uint32_t addr, const uint8_t bytes[5]) {
    void* p = (void*)(uintptr_t)addr;
    DWORD oldProt = 0;
    if (VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        copy5(p, bytes);
        VirtualProtect(p, 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
    }
}

static int WINAPI Hook_WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    Patch5(WINMAIN_ADDR, g_origWinMain);

    HMODULE hCore = LoadLibraryA("m1tp2_recomp.dll");
    if (hCore) {
        RecompInitFn initFn = (RecompInitFn)GetProcAddress(hCore, "M1TP2_RecompInit");
        if (initFn) {
            initFn();
        }
    } else {
        MessageBoxA(NULL, "Failed to load m1tp2_recomp.dll (ensure SDL3.dll is present).",
                    "M1TP2 Native Recomp", MB_ICONERROR);
    }

    WinMainFn orig = (WinMainFn)(uintptr_t)WINMAIN_ADDR;
    return orig(hInst, hPrev, lpCmd, nShow);
}

__declspec(dllexport) HRESULT WINAPI DirectDrawCreate(void* a, void** b, void* c) {
    (void)a; (void)b; (void)c;
    return E_FAIL;
}

__declspec(dllexport) HRESULT WINAPI DirectDrawEnumerateA(void* a, void* b) {
    (void)a; (void)b;
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        OutputDebugStringA("[SHIM] ddraw.dll ATTACH enter\n");
        DisableThreadLibraryCalls(hInst);
        const uint8_t* p = (const uint8_t*)(uintptr_t)WINMAIN_ADDR;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(p, &mbi, sizeof(mbi)) &&
            mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE))) {
            if (p[0] == 0x83 && p[1] == 0xEC && p[2] == 0x1C && p[3] == 0x56 && p[4] == 0xE8) {
                copy5(g_origWinMain, p);
                uint8_t jmp[5];
                int32_t rel = (int32_t)((uintptr_t)&Hook_WinMain - ((uintptr_t)WINMAIN_ADDR + 5));
                jmp[0] = 0xE9;
                jmp[1] = (uint8_t)(rel & 0xFF);
                jmp[2] = (uint8_t)((rel >> 8) & 0xFF);
                jmp[3] = (uint8_t)((rel >> 16) & 0xFF);
                jmp[4] = (uint8_t)((rel >> 24) & 0xFF);
                Patch5(WINMAIN_ADDR, jmp);
                OutputDebugStringA("[SHIM] WinMain patched OK!\n");
            }
        }
        OutputDebugStringA("[SHIM] ddraw.dll ATTACH return TRUE\n");
    }
    return TRUE;
}

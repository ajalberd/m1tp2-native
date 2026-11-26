// Native launcher for M1 Tank Platoon II (bypasses Windows 11's broken sysmain.sdb
// EmulateHeap shim matched on the filename "M1TP2.exe" by launching a hardlink M1TP2_bin.exe).
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;

    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    char* slash = NULL;
    for (DWORD i = 0; i < n; ++i) {
        if (dir[i] == '\\' || dir[i] == '/') slash = &dir[i];
    }
    if (slash) *slash = '\0';

    char origExe[MAX_PATH];
    char binExe[MAX_PATH];
    wsprintfA(origExe, "%s\\M1TP2.exe", dir);
    wsprintfA(binExe,  "%s\\M1TP2_bin.exe", dir);

    if (GetFileAttributesA(binExe) == INVALID_FILE_ATTRIBUTES) {
        if (!CreateHardLinkA(binExe, origExe, NULL)) {
            if (!CopyFileA(origExe, binExe, FALSE)) {
                MessageBoxA(NULL, "Could not locate M1TP2.exe in the game directory.",
                            "M1TP2 Native Launcher", MB_ICONERROR);
                return 1;
            }
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(binExe, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        MessageBoxA(NULL, "Failed to launch M1TP2_bin.exe.", "M1TP2 Native Launcher", MB_ICONERROR);
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <objbase.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define APP_TITLE       "M1 Tank Platoon II - Native Setup"
#define APP_VERSION     "v0.3.1"
#define PAYLOAD_MAGIC   "M1NATV01"

#define IDC_PATH_EDIT   101
#define IDC_BROWSE_BTN  102
#define IDC_STATUS_TXT  103
#define IDC_CHK_BACKUP  104
#define IDC_CHK_SHORT   105
#define IDC_CHK_GLIDE   106
#define IDC_INSTALL_BTN 107
#define IDC_UNINST_BTN  108
#define IDC_CLOSE_BTN   109
#define IDC_PROGRESS    110

#pragma pack(push, 1)
typedef struct {
    uint32_t offsetFromPayload;
    uint32_t size;
    uint32_t crc32;
    char     relPath[128];
} PayloadEntry;

typedef struct {
    char     magic[8];
    uint32_t payloadStartOffset;
    uint32_t entryTableOffset;
    uint32_t entryCount;
    uint32_t reserved;
} PayloadTrailer;
#pragma pack(pop)

static HWND  g_hwnd = NULL;
static HFONT g_fontBody = NULL;
static HFONT g_fontBold = NULL;
static HFONT g_fontTitle = NULL;
static char  g_selfPath[MAX_PATH] = {0};

static uint32_t CalcCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static int FileExistsA(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int DirExistsA(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void EnsureParentDirs(const char* filePath) {
    char tmp[MAX_PATH];
    lstrcpynA(tmp, filePath, MAX_PATH);
    for (char* p = tmp + 3; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = '\0';
            if (!DirExistsA(tmp)) CreateDirectoryA(tmp, NULL);
            *p = saved;
        }
    }
}

static int HasM1TP2Exe(const char* dir) {
    if (!dir || !dir[0]) return 0;
    char p[MAX_PATH];
    snprintf(p, sizeof(p), "%s\\M1TP2.exe", dir);
    return FileExistsA(p);
}

static int IsOurNativeDdraw(const char* path) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL);
    int found = 0;
    if (sz > 0 && sz < 2 * 1024 * 1024) {
        uint8_t* buf = (uint8_t*)malloc(sz);
        if (buf) {
            DWORD rd = 0;
            if (ReadFile(h, buf, sz, &rd, NULL) && rd == sz) {
                const char* needle = "M1TP2_RecompInit";
                size_t nlen = strlen(needle);
                for (size_t i = 0; i + nlen <= sz; ++i) {
                    if (memcmp(buf + i, needle, nlen) == 0) { found = 1; break; }
                }
            }
            free(buf);
        }
    }
    CloseHandle(h);
    return found;
}

static int g_autoDetected = 0;

static void TrimTrailingSlashes(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\\' || s[n - 1] == '/' || s[n - 1] == ' ' || s[n - 1] == '"')) {
        s[--n] = '\0';
    }
}

static int QueryRegPath(HKEY root, const char* subkey, REGSAM sam, const char* valName, char* outDir, size_t outCap) {
    HKEY hk = NULL;
    if (RegOpenKeyExA(root, subkey, 0, KEY_READ | sam, &hk) != ERROR_SUCCESS) return 0;
    char buf[MAX_PATH] = {0};
    DWORD cb = sizeof(buf) - 1;
    DWORD type = 0;
    int found = 0;
    if (RegQueryValueExA(hk, valName, NULL, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && buf[0]) {
        TrimTrailingSlashes(buf);
        if (HasM1TP2Exe(buf)) {
            lstrcpynA(outDir, buf, (int)outCap);
            found = 1;
        }
    }
    RegCloseKey(hk);
    return found;
}

static void DetectGameDir(char* outDir, size_t outCap) {
    outDir[0] = '\0';
    g_autoDetected = 0;

    // 1. Check same folder as installer
    char selfDir[MAX_PATH];
    lstrcpynA(selfDir, g_selfPath, MAX_PATH);
    char* slash = strrchr(selfDir, '\\');
    if (slash) {
        *slash = '\0';
        if (HasM1TP2Exe(selfDir)) {
            lstrcpynA(outDir, selfDir, (int)outCap);
            g_autoDetected = 1;
            return;
        }
    }

    // 2. Check GOG.com / GOG Galaxy / Inno Setup & Retail Registry keys (both 32-bit & 64-bit views, HKLM & HKCU)
    struct { HKEY root; const char* key; const char* val; } regChecks[] = {
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1658549380", "path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1658549380", "workingDir" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\1658549380", "path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\1658549380", "workingDir" },
        { HKEY_CURRENT_USER,  "SOFTWARE\\GOG.com\\Games\\1658549380", "path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1658549380_is1", "InstallLocation" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1658549380_is1", "Inno Setup: App Path" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1658549380_is1", "InstallLocation" },
        { HKEY_CURRENT_USER,  "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\1658549380_is1", "InstallLocation" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\MicroProse\\M1 Tank Platoon II", "InstallPath" },
        { HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\MicroProse\\M1 Tank Platoon II", "InstallPath" },
    };
    const REGSAM sams[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (size_t i = 0; i < sizeof(regChecks) / sizeof(regChecks[0]); ++i) {
        for (int s = 0; s < 2; ++s) {
            if (QueryRegPath(regChecks[i].root, regChecks[i].key, sams[s], regChecks[i].val, outDir, outCap)) {
                g_autoDetected = 1;
                return;
            }
        }
    }

    // 3. Scan common GOG & Retail CD install paths across drives C: through H:
    const char* relPaths[] = {
        "GOG Games\\M1 Tank Platoon II",
        "Program Files (x86)\\GOG Galaxy\\Games\\M1 Tank Platoon II",
        "Program Files\\GOG Galaxy\\Games\\M1 Tank Platoon II",
        "Games\\M1 Tank Platoon II",
        "Games\\GOG\\M1 Tank Platoon II",
        "Program Files (x86)\\MicroProse\\M1 Tank Platoon II",
        "MicroProse\\M1 Tank Platoon II",
    };
    for (char drv = 'C'; drv <= 'H'; ++drv) {
        for (size_t i = 0; i < sizeof(relPaths) / sizeof(relPaths[0]); ++i) {
            char cand[MAX_PATH];
            snprintf(cand, sizeof(cand), "%c:\\%s", drv, relPaths[i]);
            if (HasM1TP2Exe(cand)) {
                lstrcpynA(outDir, cand, (int)outCap);
                g_autoDetected = 1;
                return;
            }
        }
    }
}

static int GetDesktopShortcutPath(char* outPath, size_t cap) {
    char desk[MAX_PATH] = {0};
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk) != S_OK) return 0;
    snprintf(outPath, cap, "%s\\M1 Tank Platoon II [Native].lnk", desk);
    return 1;
}

static int CreateDesktopShortcut(const char* gameDir) {
    char lnkPath[MAX_PATH];
    if (!GetDesktopShortcutPath(lnkPath, sizeof(lnkPath))) return 0;

    char targetExe[MAX_PATH], icoPath[MAX_PATH];
    snprintf(targetExe, sizeof(targetExe), "%s\\m1tp2_native.exe", gameDir);
    snprintf(icoPath, sizeof(icoPath), "%s\\M1TP2.ico", gameDir);

    HRESULT hr = CoInitialize(NULL);
    IShellLinkA* psl = NULL;
    int ok = 0;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void**)&psl))) {
        IShellLinkA_SetPath(psl, targetExe);
        IShellLinkA_SetWorkingDirectory(psl, gameDir);
        IShellLinkA_SetDescription(psl, "M1 Tank Platoon II (Native SDL3/OpenGL HD)");
        if (FileExistsA(icoPath)) {
            IShellLinkA_SetIconLocation(psl, icoPath, 0);
        }
        IPersistFile* ppf = NULL;
        if (SUCCEEDED(IShellLinkA_QueryInterface(psl, &IID_IPersistFile, (void**)&ppf))) {
            WCHAR wpath[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, lnkPath, -1, wpath, MAX_PATH);
            if (SUCCEEDED(IPersistFile_Save(ppf, wpath, TRUE))) ok = 1;
            IPersistFile_Release(ppf);
        }
        IShellLinkA_Release(psl);
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    return ok;
}

static void UpgradeGameCfgAndIni(const char* gameDir, int enableGlideAndSkipIntro) {
    if (enableGlideAndSkipIntro) {
        char cfgPath[MAX_PATH];
        snprintf(cfgPath, sizeof(cfgPath), "%s\\game.cfg", gameDir);
        HANDLE h = CreateFileA(cfgPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD sz = GetFileSize(h, NULL);
            if (sz >= 20 && sz <= 128) {
                uint8_t buf[128];
                DWORD rd = 0;
                if (ReadFile(h, buf, sz, &rd, NULL) && rd == sz) {
                    int32_t* ints = (int32_t*)buf;
                    ints[1] = 1; // Detail Level = HIGH
                    ints[2] = 1; // Render Mode = GLIDE (Native OpenGL HD)
                    ints[3] = 1; // Alpha Blending = ON
                    SetFilePointer(h, 0, NULL, FILE_BEGIN);
                    DWORD wr = 0;
                    WriteFile(h, buf, sz, &wr, NULL);
                }
            }
            CloseHandle(h);
        }
    }

    char iniPath[MAX_PATH];
    snprintf(iniPath, sizeof(iniPath), "%s\\m1tp2_recomp.ini", gameDir);
    if (!FileExistsA(iniPath)) {
        FILE* f = fopen(iniPath, "w");
        if (f) {
            fprintf(f,
                "# M1 Tank Platoon II Native Recomp Settings (press F12 in-game to configure)\n"
                "WindowWidth = 0\n"
                "WindowHeight = 0\n"
                "Fullscreen = 1\n"
                "VSync = 1\n"
                "Unlock100Fps = 1\n"
                "# MenuAspectMode: 0=FillScreen, 1=4:3 (Default), 2=16:10, 3=CropZoom\n"
                "MenuAspectMode = 1\n"
                "# AspectMode (3D Combat): 0=SmartHor+ (Default), 1=All3DHor+, 2=4:3, 3=Stretch\n"
                "AspectMode = 0\n"
                "# MapAspectMode (Battleplanner / IVIS Map): 0=AutoNative4:3 (Default), 2=16:10, 3=16:9, 4=Stretch\n"
                "MapAspectMode = 0\n"
                "MSAA = 4\n"
                "Anisotropy = 16\n"
                "TexFilter3D = 0\n"
                "LinearFilter2D = 1\n"
                "LinearFilterLfb = 0\n"
                "# DrawDistance: 0=2km(Stock), 1=3km(Extended), 2=4km(Long), 3=6km(Max)\n"
                "DrawDistance = 1\n"
                "FogMode = 0\n"
                "Gamma = 1.00\n"
                "ForceGlideOnD3D = 1\n"
                "SkipIntro = %d\n"
                "ShowLauncherOnStartup = 1\n"
                "CheckForUpdates = 1\n"
                "EnableMods = 1\n"
                "FixCampaignFrontBug = 1\n"
                "CampaignIntelHud = 1\n"
                "# GunnerMouseMode: 0=PrecisionStick (Default), 1=DirectRawMouse, 2=Stock1998\n"
                "GunnerMouseMode = 0\n"
                "GunnerMouseSens = 1.00\n"
                "# BallisticLeadMode: 0=SmoothFCS (Default), 1=ManualNoAutoLead, 2=Stock1998\n"
                "BallisticLeadMode = 0\n"
                "DisableJoystick = 1\n"
                "AudioMute = 0\n"
                "AudioMasterVol = 1.00\n"
                "AudioMusicVol = 1.00\n"
                "AudioSfxVol = 1.00\n"
                "AudioEngineVol = 1.00\n"
                "AudioVoiceVol = 1.00\n"
                "ShowStatsHud = 0\n",
                enableGlideAndSkipIntro ? 1 : 0);
            fclose(f);
        }
    }
}

static int DoInstall(const char* gameDir, int doBackup, int doShortcut, int doGlideOpt, char* errMsg, size_t errCap) {
    if (!HasM1TP2Exe(gameDir)) {
        snprintf(errMsg, errCap, "M1TP2.exe was not found in:\n%s\n\nPlease select your M1 Tank Platoon II installation folder.", gameDir);
        return 0;
    }

    // Read embedded payload trailer from our own executable
    HANDLE hSelf = CreateFileA(g_selfPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSelf == INVALID_HANDLE_VALUE) {
        snprintf(errMsg, errCap, "Could not open installer executable to read payload.");
        return 0;
    }
    DWORD fileSize = GetFileSize(hSelf, NULL);
    if (fileSize < sizeof(PayloadTrailer)) {
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Installer payload is missing or corrupted.");
        return 0;
    }

    PayloadTrailer tr;
    SetFilePointer(hSelf, (LONG)(fileSize - sizeof(PayloadTrailer)), NULL, FILE_BEGIN);
    DWORD rd = 0;
    if (!ReadFile(hSelf, &tr, sizeof(tr), &rd, NULL) || rd != sizeof(tr) ||
        memcmp(tr.magic, PAYLOAD_MAGIC, 8) != 0 || tr.entryCount == 0 || tr.entryCount > 64) {
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Invalid installer payload signature. Please re-download the release binary.");
        return 0;
    }

    PayloadEntry* entries = (PayloadEntry*)malloc(tr.entryCount * sizeof(PayloadEntry));
    if (!entries) {
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Out of memory.");
        return 0;
    }
    SetFilePointer(hSelf, (LONG)tr.entryTableOffset, NULL, FILE_BEGIN);
    if (!ReadFile(hSelf, entries, tr.entryCount * sizeof(PayloadEntry), &rd, NULL) ||
        rd != tr.entryCount * sizeof(PayloadEntry)) {
        free(entries);
        CloseHandle(hSelf);
        snprintf(errMsg, errCap, "Failed to read embedded file table.");
        return 0;
    }

    // 1. Backup pre-existing non-native ddraw / DDrawCompat files
    if (doBackup) {
        const char* backupFiles[] = { "ddraw.dll", "DDrawCompat.ini", "ddraw_readme.txt" };
        char bakDir[MAX_PATH];
        snprintf(bakDir, sizeof(bakDir), "%s\\_m1tp2_backup", gameDir);

        for (int i = 0; i < 3; ++i) {
            char src[MAX_PATH], dst[MAX_PATH];
            snprintf(src, sizeof(src), "%s\\%s", gameDir, backupFiles[i]);
            snprintf(dst, sizeof(dst), "%s\\%s", bakDir, backupFiles[i]);
            if (FileExistsA(src)) {
                if (i == 0 && IsOurNativeDdraw(src)) continue; // Already our native shim
                if (!DirExistsA(bakDir)) CreateDirectoryA(bakDir, NULL);
                if (!FileExistsA(dst)) {
                    CopyFileA(src, dst, TRUE);
                }
                if (i > 0) DeleteFileA(src); // Remove old DDrawCompat config after backup
            }
        }
    }

    // 2. Extract all bundled payload files with CRC32 verification
    for (uint32_t i = 0; i < tr.entryCount; ++i) {
        uint8_t* buf = (uint8_t*)malloc(entries[i].size > 0 ? entries[i].size : 1);
        if (!buf) {
            free(entries);
            CloseHandle(hSelf);
            snprintf(errMsg, errCap, "Out of memory extracting %s.", entries[i].relPath);
            return 0;
        }
        SetFilePointer(hSelf, (LONG)(tr.payloadStartOffset + entries[i].offsetFromPayload), NULL, FILE_BEGIN);
        if (entries[i].size > 0 && (!ReadFile(hSelf, buf, entries[i].size, &rd, NULL) || rd != entries[i].size)) {
            free(buf);
            free(entries);
            CloseHandle(hSelf);
            snprintf(errMsg, errCap, "Read error extracting %s.", entries[i].relPath);
            return 0;
        }
        if (CalcCrc32(buf, entries[i].size) != entries[i].crc32) {
            free(buf);
            free(entries);
            CloseHandle(hSelf);
            snprintf(errMsg, errCap, "CRC checksum mismatch on %s. Download may be corrupted.", entries[i].relPath);
            return 0;
        }

        char dest[MAX_PATH];
        snprintf(dest, sizeof(dest), "%s\\%s", gameDir, entries[i].relPath);
        EnsureParentDirs(dest);

        HANDLE hOut = CreateFileA(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hOut == INVALID_HANDLE_VALUE) {
            DWORD gle = GetLastError();
            free(buf);
            free(entries);
            CloseHandle(hSelf);
            snprintf(errMsg, errCap,
                     "Failed to write:\n%s\n\nError code: %lu\n(Close M1TP2 if it is running, or run installer as Administrator if installed in Program Files.)",
                     dest, (unsigned long)gle);
            return 0;
        }
        DWORD wr = 0;
        BOOL ok = (entries[i].size == 0) || (WriteFile(hOut, buf, entries[i].size, &wr, NULL) && wr == entries[i].size);
        CloseHandle(hOut);
        free(buf);
        if (!ok) {
            free(entries);
            CloseHandle(hSelf);
            snprintf(errMsg, errCap, "Disk write failed on %s.", dest);
            return 0;
        }
    }

    free(entries);
    CloseHandle(hSelf);

    // 3. Configure Glide HD & INI defaults
    UpgradeGameCfgAndIni(gameDir, doGlideOpt);

    // 4. Optional Desktop shortcut
    if (doShortcut) {
        CreateDesktopShortcut(gameDir);
    }

    return 1;
}

static int DoUninstall(const char* gameDir, char* errMsg, size_t errCap) {
    if (!HasM1TP2Exe(gameDir)) {
        snprintf(errMsg, errCap, "M1TP2.exe was not found in:\n%s", gameDir);
        return 0;
    }

    const char* removeFiles[] = {
        "m1tp2_native.exe",
        "M1TP2_bin.exe",
        "m1tp2_recomp.dll",
        "SDL3.dll",
        "ddraw.dll",
        "THIRD_PARTY_LICENSES.txt",
        "docs\\M1TP2_Quickstart_Keys.png"
    };
    for (size_t i = 0; i < sizeof(removeFiles) / sizeof(removeFiles[0]); ++i) {
        char p[MAX_PATH];
        snprintf(p, sizeof(p), "%s\\%s", gameDir, removeFiles[i]);
        if (FileExistsA(p)) DeleteFileA(p);
    }

    // Restore backups if present
    const char* bakFiles[] = { "ddraw.dll", "DDrawCompat.ini", "ddraw_readme.txt" };
    char bakDir[MAX_PATH];
    snprintf(bakDir, sizeof(bakDir), "%s\\_m1tp2_backup", gameDir);
    for (int i = 0; i < 3; ++i) {
        char src[MAX_PATH], dst[MAX_PATH];
        snprintf(src, sizeof(src), "%s\\%s", bakDir, bakFiles[i]);
        snprintf(dst, sizeof(dst), "%s\\%s", gameDir, bakFiles[i]);
        if (FileExistsA(src)) {
            MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING);
        }
    }
    RemoveDirectoryA(bakDir);

    char lnkPath[MAX_PATH];
    if (GetDesktopShortcutPath(lnkPath, sizeof(lnkPath)) && FileExistsA(lnkPath)) {
        DeleteFileA(lnkPath);
    }
    return 1;
}

static void RefreshPathStatus(HWND hwnd) {
    char dir[MAX_PATH] = {0};
    GetDlgItemTextA(hwnd, IDC_PATH_EDIT, dir, MAX_PATH);
    size_t len = strlen(dir);
    while (len > 0 && (dir[len - 1] == '\\' || dir[len - 1] == '/' || dir[len - 1] == ' ')) {
        dir[--len] = '\0';
    }
    int ok = HasM1TP2Exe(dir);
    SetDlgItemTextA(hwnd, IDC_STATUS_TXT,
                    ok ? (g_autoDetected ? "Status: Auto-detected M1TP2.exe (GOG/Retail) - ready for 1-click install!"
                                         : "Status: Found M1TP2.exe - ready for 1-click installation!")
                       : "Status: M1TP2.exe not auto-detected - click Browse... to select your game folder.");
    EnableWindow(GetDlgItem(hwnd, IDC_INSTALL_BTN), ok);
    EnableWindow(GetDlgItem(hwnd, IDC_UNINST_BTN), ok);
}

static void BrowseForFolder(HWND hwnd) {
    BROWSEINFOA bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = "Select your M1 Tank Platoon II folder (containing M1TP2.exe):";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH] = {0};
        if (SHGetPathFromIDListA(pidl, path)) {
            SetDlgItemTextA(hwnd, IDC_PATH_EDIT, path);
            RefreshPathStatus(hwnd);
        }
        CoTaskMemFree(pidl);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_fontTitle = CreateFontA(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            g_fontBold  = CreateFontA(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            g_fontBody  = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            HWND h;
            h = CreateWindowA("STATIC", "M1 Tank Platoon II - Native Runtime " APP_VERSION,
                              WS_CHILD | WS_VISIBLE, 20, 16, 520, 28, hwnd, NULL, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);

            h = CreateWindowA("STATIC",
                              "100% Native SDL3 + OpenGL 3.3 HD Renderer, True Hor+ Widescreen, F12 Menu & Mods VFS.\n"
                              "No DxWnd, DDrawCompat, or dgVoodoo2 required.",
                              WS_CHILD | WS_VISIBLE, 20, 48, 520, 36, hwnd, NULL, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("STATIC", "M1 Tank Platoon II Install Folder:",
                              WS_CHILD | WS_VISIBLE, 20, 96, 300, 20, hwnd, NULL, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                20, 118, 420, 26, hwnd, (HMENU)IDC_PATH_EDIT, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("BUTTON", "Browse...",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              448, 117, 92, 28, hwnd, (HMENU)IDC_BROWSE_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("STATIC", "",
                              WS_CHILD | WS_VISIBLE, 20, 150, 520, 20, hwnd, (HMENU)IDC_STATUS_TXT, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            h = CreateWindowA("BUTTON", "Backup existing ddraw.dll / DDrawCompat files to _m1tp2_backup\\",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              20, 184, 520, 22, hwnd, (HMENU)IDC_CHK_BACKUP, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
            SendMessageA(h, BM_SETCHECK, BST_CHECKED, 0);

            h = CreateWindowA("BUTTON", "Create Desktop shortcut (M1 Tank Platoon II [Native])",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              20, 212, 520, 22, hwnd, (HMENU)IDC_CHK_SHORT, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
            SendMessageA(h, BM_SETCHECK, BST_CHECKED, 0);

            h = CreateWindowA("BUTTON", "Enable Glide HD in game.cfg && skip startup intros (F12 to customize)",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              20, 240, 520, 22, hwnd, (HMENU)IDC_CHK_GLIDE, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
            SendMessageA(h, BM_SETCHECK, BST_CHECKED, 0);

            h = CreateWindowA("BUTTON", "Install / Update",
                              WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                              20, 288, 170, 36, hwnd, (HMENU)IDC_INSTALL_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBold, TRUE);

            h = CreateWindowA("BUTTON", "Uninstall / Restore",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              202, 288, 160, 36, hwnd, (HMENU)IDC_UNINST_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            h = CreateWindowA("BUTTON", "Close",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              448, 288, 92, 36, hwnd, (HMENU)IDC_CLOSE_BTN, NULL, NULL);
            SendMessageA(h, WM_SETFONT, (WPARAM)g_fontBody, TRUE);

            char detected[MAX_PATH] = {0};
            DetectGameDir(detected, sizeof(detected));
            SetDlgItemTextA(hwnd, IDC_PATH_EDIT, detected);
            RefreshPathStatus(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDC_PATH_EDIT && code == EN_CHANGE) {
                RefreshPathStatus(hwnd);
                return 0;
            }
            if (id == IDC_BROWSE_BTN) {
                BrowseForFolder(hwnd);
                return 0;
            }
            if (id == IDC_CLOSE_BTN) {
                PostQuitMessage(0);
                return 0;
            }
            if (id == IDC_INSTALL_BTN) {
                char dir[MAX_PATH] = {0}, err[512] = {0};
                GetDlgItemTextA(hwnd, IDC_PATH_EDIT, dir, MAX_PATH);
                size_t len = strlen(dir);
                while (len > 0 && (dir[len - 1] == '\\' || dir[len - 1] == '/')) dir[--len] = '\0';

                int doBackup = (SendDlgItemMessageA(hwnd, IDC_CHK_BACKUP, BM_GETCHECK, 0, 0) == BST_CHECKED);
                int doShort  = (SendDlgItemMessageA(hwnd, IDC_CHK_SHORT,  BM_GETCHECK, 0, 0) == BST_CHECKED);
                int doGlide  = (SendDlgItemMessageA(hwnd, IDC_CHK_GLIDE,  BM_GETCHECK, 0, 0) == BST_CHECKED);

                if (!DoInstall(dir, doBackup, doShort, doGlide, err, sizeof(err))) {
                    MessageBoxA(hwnd, err, APP_TITLE, MB_OK | MB_ICONERROR);
                    return 0;
                }
                int res = MessageBoxA(hwnd,
                    "M1 Tank Platoon II Native " APP_VERSION " installed successfully!\n\n"
                    "- Launch anytime via m1tp2_native.exe or your Desktop shortcut.\n"
                    "- Press F12 in-game for MSAA, AF, True Hor+ Widescreen, Fog & Mods.\n\n"
                    "Launch M1 Tank Platoon II now?",
                    APP_TITLE, MB_YESNO | MB_ICONINFORMATION);
                if (res == IDYES) {
                    char exe[MAX_PATH];
                    snprintf(exe, sizeof(exe), "%s\\m1tp2_native.exe", dir);
                    ShellExecuteA(NULL, "open", exe, NULL, dir, SW_SHOWNORMAL);
                }
                PostQuitMessage(0);
                return 0;
            }
            if (id == IDC_UNINST_BTN) {
                char dir[MAX_PATH] = {0}, err[512] = {0};
                GetDlgItemTextA(hwnd, IDC_PATH_EDIT, dir, MAX_PATH);
                size_t len = strlen(dir);
                while (len > 0 && (dir[len - 1] == '\\' || dir[len - 1] == '/')) dir[--len] = '\0';

                if (MessageBoxA(hwnd,
                        "Remove M1TP2 Native runtime files and restore any backed-up ddraw.dll?\n(Your saves and mods/ folder will be kept.)",
                        APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (!DoUninstall(dir, err, sizeof(err))) {
                        MessageBoxA(hwnd, err, APP_TITLE, MB_OK | MB_ICONERROR);
                    } else {
                        MessageBoxA(hwnd, "Uninstalled and restored backup successfully.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
                    }
                }
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontBold)  DeleteObject(g_fontBold);
            if (g_fontBody)  DeleteObject(g_fontBody);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int ParseSilentArgs(const char* cmd, int* outSilent, int* outUninstall, int* outRelaunch,
                           DWORD* outWaitPid, char* outDir, size_t cap) {
    *outSilent    = 0;
    *outUninstall = 0;
    *outRelaunch  = 0;
    *outWaitPid   = 0;
    outDir[0]     = '\0';
    if (!cmd || !cmd[0]) return 0;

    if (strstr(cmd, "/S") || strstr(cmd, "/s") || strstr(cmd, "--silent"))    *outSilent = 1;
    if (strstr(cmd, "/U") || strstr(cmd, "/u") || strstr(cmd, "--uninstall")) *outUninstall = 1;
    if (strstr(cmd, "/R") || strstr(cmd, "/r") || strstr(cmd, "--relaunch"))  *outRelaunch = 1;

    const char* w = strstr(cmd, "/W=");
    if (!w) w = strstr(cmd, "/w=");
    if (w) {
        *outWaitPid = (DWORD)strtoul(w + 3, NULL, 10);
    }

    const char* d = strstr(cmd, "/D=");
    if (!d) d = strstr(cmd, "/d=");
    if (d) {
        d += 3;
        int quoted = (*d == '"');
        if (quoted) ++d;
        size_t i = 0;
        while (*d && i + 1 < cap) {
            if (quoted && *d == '"') break;
            if (!quoted && (*d == ' ' || *d == '\t')) break;
            outDir[i++] = *d++;
        }
        outDir[i] = '\0';
        while (i > 0 && (outDir[i - 1] == '\\' || outDir[i - 1] == '/')) outDir[--i] = '\0';
    }
    return (*outSilent || *outUninstall);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd) {
    (void)hPrev; (void)nShowCmd;
    GetModuleFileNameA(NULL, g_selfPath, MAX_PATH);

    // Silent CLI mode:
    //   /S /D="C:\Path\To\M1TP2"                  (silent install)
    //   /S /W=<pid> /R /D="C:\Path\To\M1TP2"      (self-update: wait for PID, install preserving game.cfg, relaunch)
    //   /U /D="C:\Path\To\M1TP2"                  (silent uninstall + restore backups)
    int silent = 0, uninst = 0, relaunch = 0;
    DWORD waitPid = 0;
    char cliDir[MAX_PATH] = {0};
    if (ParseSilentArgs(lpCmdLine, &silent, &uninst, &relaunch, &waitPid, cliDir, sizeof(cliDir))) {
        if (waitPid != 0) {
            HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, waitPid);
            if (hProc) {
                WaitForSingleObject(hProc, 15000);
                CloseHandle(hProc);
            }
            Sleep(150);
        }
        if (!cliDir[0]) DetectGameDir(cliDir, sizeof(cliDir));
        char err[512] = {0};
        if (uninst) {
            return DoUninstall(cliDir, err, sizeof(err)) ? 0 : 1;
        }
        // When self-updating (/R), pass doGlideOpt=0 so existing user game.cfg is never overwritten
        if (!DoInstall(cliDir, 1, 0, relaunch ? 0 : 1, err, sizeof(err))) {
            if (relaunch) {
                MessageBoxA(NULL, err, APP_TITLE " (Auto-Update)", MB_OK | MB_ICONERROR);
            }
            return 1;
        }
        if (relaunch) {
            char exe[MAX_PATH];
            snprintf(exe, sizeof(exe), "%s\\m1tp2_native.exe", cliDir);
            ShellExecuteA(NULL, "open", exe, "--launcher", cliDir, SW_SHOWNORMAL);
        }
        return 0;
    }

    InitCommonControls();

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconA(NULL, IDI_APPLICATION);
    wc.lpszClassName = "M1TP2NativeSetupWnd";
    RegisterClassA(&wc);

    int w = 576, h = 382;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_hwnd = CreateWindowA(wc.lpszClassName, APP_TITLE " " APP_VERSION,
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           sx, sy, w, h, NULL, NULL, hInst, NULL);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

#include "vfs_mod.h"
#include "common.h"
#include "patch.h"
#include <windows.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace m1 {

namespace {

struct ModFileEntry {
    std::string fullPath;
    std::string modName;
    std::vector<uint8_t> cachedBytes;
    bool loaded = false;
    bool logged = false;
};

CRITICAL_SECTION g_vfsLock;
bool g_vfsLockInited = false;
std::string g_gameRoot;
std::unordered_map<std::string, ModFileEntry> g_modFiles;
int g_activeModCount = 0;

using CatalogFindEntryFn = void* (__cdecl*)(void* slot, const char* filename);
using CrtFsopenFn        = void* (__cdecl*)(const char* path, const char* mode, int shflag);

struct CatEntry {
    uint32_t hash;
    uint32_t offset;
    uint32_t size;
};

static std::string NormalizeRelKey(const std::string& raw) {
    std::string s = raw;
    for (char& ch : s) {
        if (ch == '/') ch = '\\';
        else ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    // Strip leading ".\" or "\"
    while (s.size() >= 2 && s[0] == '.' && s[1] == '\\') s.erase(0, 2);
    while (!s.empty() && s[0] == '\\') s.erase(0, 1);
    return s;
}

static bool FileExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void EnsureModsReadme(const std::string& modsDir) {
    if (!DirExists(modsDir)) {
        CreateDirectoryA(modsDir.c_str(), nullptr);
    }
    std::string readmePath = modsDir + "\\README_MODS.txt";
    if (!FileExists(readmePath)) {
        FILE* f = std::fopen(readmePath.c_str(), "w");
        if (f) {
            std::fprintf(f,
                "====================================================================\n"
                "  M1 Tank Platoon II - Native Modding VFS (mods/ directory)\n"
                "====================================================================\n\n"
                "HOW TO INSTALL MODS:\n"
                "  1. Create a folder for each mod inside this mods/ directory,\n"
                "     for example:  mods\\hd_textures\\\n"
                "                   mods\\custom_battles\\\n"
                "  2. Place modified files inside your mod folder matching either:\n"
                "     - Catalog paths (overrides m1.cat / 3dz.cat with NO limit):\n"
                "         mods\\my_mod\\3dz\\m1a2_h.3dz\n"
                "         mods\\my_mod\\pics\\m1a2_h.pcx\n"
                "         mods\\my_mod\\vpics\\m1a2_h.pcx\n"
                "         mods\\my_mod\\cockpits\\commander\\c-cpit_4.pcx\n"
                "     - Or root-mirrored disk paths:\n"
                "         mods\\my_mod\\Game\\battle.txt\n"
                "         mods\\my_mod\\Cockpits\\cockpit.dat\n"
                "         mods\\my_mod\\BATTLES\\ARMOR\\ARMOR_1.TXT\n"
                "         mods\\my_mod\\World\\NTC\\Tiles.txt\n\n"
                "LOAD ORDER:\n"
                "  - Optional: create mods\\loadorder.txt listing one mod folder name\n"
                "    per line (lines starting with # are comments). Later entries\n"
                "    override earlier entries.\n"
                "  - If loadorder.txt is absent, all folders in mods\\ load in\n"
                "    alphabetical order, with mods\\override\\ loaded last (highest).\n\n"
                "IN-GAME:\n"
                "  - Press F12 to toggle Mods VFS ON/OFF and view live override stats.\n"
                "  - Check m1tp2_recomp.log to see every overridden asset loaded.\n");
            std::fclose(f);
        }
    }
}

static void RegisterModFile(const std::string& modName, const std::string& relPath, const std::string& fullPath) {
    std::string key = NormalizeRelKey(relPath);
    if (key.empty()) return;

    auto upsert = [&](const std::string& k) {
        ModFileEntry e;
        e.fullPath = fullPath;
        e.modName  = modName;
        g_modFiles[k] = std::move(e);
    };

    upsert(key);
    // Also alias "game\<subpath>" <-> "<subpath>" so both root-mirrored and Game\-relative mods work seamlessly!
    if (key.rfind("game\\", 0) == 0 && key.size() > 5) {
        upsert(key.substr(5));
    } else if (key.rfind("3dz\\", 0) == 0 || key.rfind("pics\\", 0) == 0 ||
               key.rfind("vpics\\", 0) == 0 || key.rfind("dpics\\", 0) == 0 ||
               key.rfind("thermal\\", 0) == 0 || key.rfind("map\\", 0) == 0 ||
               key.rfind("cockpits\\", 0) == 0) {
        upsert("game\\" + key);
    }
}

static void ScanModDirRecursive(const std::string& modName, const std::string& baseDir, const std::string& relPrefix) {
    std::string pattern = baseDir + (relPrefix.empty() ? "\\*" : ("\\" + relPrefix + "\\*"));
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        const char* name = fd.cFileName;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
        std::string rel  = relPrefix.empty() ? std::string(name) : (relPrefix + "\\" + name);
        std::string full = baseDir + "\\" + rel;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanModDirRecursive(modName, baseDir, rel);
        } else {
            // Skip metadata/readme files at mod root
            std::string low = NormalizeRelKey(rel);
            if (low == "mod.txt" || low == "readme.txt" || low == "readme_mods.txt") continue;
            RegisterModFile(modName, rel, full);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}

static void RebuildModIndexLocked() {
    g_modFiles.clear();
    g_activeModCount = 0;

    std::string modsDir = g_gameRoot + "\\mods";
    EnsureModsReadme(modsDir);

    std::vector<std::string> modOrder;
    std::string loadOrderPath = modsDir + "\\loadorder.txt";
    if (FILE* f = std::fopen(loadOrderPath.c_str(), "r")) {
        char line[260];
        while (std::fgets(line, sizeof(line), f)) {
            char* p = line;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
            if (*p == '\0' || *p == '#' || *p == ';') continue;
            size_t len = std::strlen(p);
            while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n')) {
                p[--len] = '\0';
            }
            if (len > 0 && DirExists(modsDir + "\\" + p)) {
                modOrder.emplace_back(p);
            }
        }
        std::fclose(f);
        LogPrintf("[VFS] Using mods\\loadorder.txt (%zu active mods)\n", modOrder.size());
    } else {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((modsDir + "\\*").c_str(), &fd);
        bool hasOverride = false;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                const char* name = fd.cFileName;
                if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
                if (name[0] == '.' || name[0] == '_') continue; // Ignore _disabled or .hidden folders
                if (_stricmp(name, "override") == 0) {
                    hasOverride = true;
                } else {
                    modOrder.emplace_back(name);
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        std::sort(modOrder.begin(), modOrder.end(), [](const std::string& a, const std::string& b) {
            return _stricmp(a.c_str(), b.c_str()) < 0;
        });
        if (hasOverride) modOrder.emplace_back("override");
    }

    std::unordered_set<std::string> uniquePaths;
    for (const auto& modName : modOrder) {
        size_t before = g_modFiles.size();
        ScanModDirRecursive(modName, modsDir + "\\" + modName, "");
        g_activeModCount++;
        LogPrintf("[VFS] Mounted mod '%s' (+%zu keys)\n", modName.c_str(), g_modFiles.size() - before);
    }

    for (const auto& kv : g_modFiles) uniquePaths.insert(kv.second.fullPath);
    g_stats.activeMods = g_activeModCount;
    g_stats.modFiles   = static_cast<int>(uniquePaths.size());
    LogPrintf("[VFS] Ready: %d active mod(s), %d unique override file(s) (%zu lookup keys)\n",
              g_stats.activeMods, g_stats.modFiles, g_modFiles.size());
}

static ModFileEntry* LookupModEntryLocked(const char* rawPath) {
    if (!rawPath || !rawPath[0] || g_modFiles.empty()) return nullptr;

    std::string key = NormalizeRelKey(rawPath);
    auto it = g_modFiles.find(key);
    if (it != g_modFiles.end()) return &it->second;

    // If the game passed an absolute path inside g_gameRoot, strip g_gameRoot prefix
    std::string rootLow = NormalizeRelKey(g_gameRoot);
    if (!rootLow.empty() && key.size() > rootLow.size() + 1 &&
        key.compare(0, rootLow.size(), rootLow) == 0 && key[rootLow.size()] == '\\') {
        std::string rel = key.substr(rootLow.size() + 1);
        it = g_modFiles.find(rel);
        if (it != g_modFiles.end()) return &it->second;
    }

    // If CWD is inside a subdirectory of g_gameRoot (e.g. Game\), try "<cwd_rel>\<key>"
    char cwdBuf[MAX_PATH] = {};
    if (GetCurrentDirectoryA(MAX_PATH, cwdBuf) > 0) {
        std::string cwdLow = NormalizeRelKey(cwdBuf);
        if (cwdLow.size() > rootLow.size() + 1 &&
            cwdLow.compare(0, rootLow.size(), rootLow) == 0 && cwdLow[rootLow.size()] == '\\') {
            std::string sub = cwdLow.substr(rootLow.size() + 1);
            it = g_modFiles.find(sub + "\\" + key);
            if (it != g_modFiles.end()) return &it->second;
        }
    }

    return nullptr;
}

static bool LoadFileBytes(const std::string& fullPath, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileA(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, nullptr);
    if (sz == INVALID_FILE_SIZE) {
        CloseHandle(h);
        return false;
    }
    out.resize(sz);
    DWORD read = 0;
    bool ok = (sz == 0) || (ReadFile(h, out.data(), sz, &read, nullptr) && read == sz);
    CloseHandle(h);
    return ok;
}

// Hook for CatalogLookup(int catHandle, const char* filename, int* outSize) at 0x427120
void* __cdecl Hook_CatalogLookup(int catHandle, const char* filename, int* outSize) {
    if (outSize) *outSize = 0;

    if (g_cfg.enableMods && filename && filename[0] && g_vfsLockInited) {
        EnterCriticalSection(&g_vfsLock);
        ModFileEntry* entry = LookupModEntryLocked(filename);
        if (entry) {
            if (!entry->loaded) {
                entry->loaded = LoadFileBytes(entry->fullPath, entry->cachedBytes);
            }
            if (entry->loaded && !entry->cachedBytes.empty()) {
                if (outSize) *outSize = static_cast<int>(entry->cachedBytes.size());
                g_stats.vfsHits++;
                if (!entry->logged) {
                    entry->logged = true;
                    LogPrintf("[VFS-CAT] [%s] %s -> %s (%zu bytes)\n",
                              entry->modName.c_str(), filename, entry->fullPath.c_str(),
                              entry->cachedBytes.size());
                }
                void* ptr = entry->cachedBytes.data();
                LeaveCriticalSection(&g_vfsLock);
                return ptr;
            }
        }
        LeaveCriticalSection(&g_vfsLock);
    }

    // Stock fallback: exact CatalogLookup logic using CatalogFindEntry (0x4270D0)
    if (catHandle <= 0) return reinterpret_cast<void*>(static_cast<uintptr_t>(-1));
    uint8_t* slot = reinterpret_cast<uint8_t*>(addr::g_CatalogSlotBase) + static_cast<size_t>(catHandle) * 0x120u;
    auto findEntry = reinterpret_cast<CatalogFindEntryFn>(addr::CatalogFindEntry);
    CatEntry* e = reinterpret_cast<CatEntry*>(findEntry(slot, filename));
    if (!e) return reinterpret_cast<void*>(static_cast<uintptr_t>(-1));
    if (outSize) *outSize = static_cast<int>(e->size);
    uint8_t* mapView = *reinterpret_cast<uint8_t**>(slot + 0x11C);
    return mapView + e->offset;
}

// Hook for crt_fopen(const char* path, const char* mode) at 0x45C560
void* __cdecl Hook_CrtFopen(const char* path, const char* mode) {
    auto fsopen = reinterpret_cast<CrtFsopenFn>(addr::CrtFsopen);
    if (g_cfg.enableMods && path && path[0] && mode && mode[0] == 'r' &&
        std::strpbrk(mode, "+waWA") == nullptr && g_vfsLockInited) {
        std::string redirectPath;
        EnterCriticalSection(&g_vfsLock);
        ModFileEntry* entry = LookupModEntryLocked(path);
        if (entry) {
            redirectPath = entry->fullPath;
            g_stats.vfsHits++;
            if (!entry->logged) {
                entry->logged = true;
                LogPrintf("[VFS-FILE] [%s] %s -> %s\n",
                          entry->modName.c_str(), path, entry->fullPath.c_str());
            }
        }
        LeaveCriticalSection(&g_vfsLock);

        if (!redirectPath.empty()) {
            void* f = fsopen(redirectPath.c_str(), mode, 0x40);
            if (f) return f;
        }
    }
    return fsopen(path, mode, 0x40);
}

} // namespace

void VfsModInit() {
    InitializeCriticalSection(&g_vfsLock);
    g_vfsLockInited = true;

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    g_gameRoot = exePath;
    size_t slash = g_gameRoot.find_last_of("\\/");
    if (slash != std::string::npos) g_gameRoot.resize(slash);

    EnterCriticalSection(&g_vfsLock);
    RebuildModIndexLocked();
    LeaveCriticalSection(&g_vfsLock);

    InstallJump(addr::CatalogLookup, reinterpret_cast<void*>(&Hook_CatalogLookup));
    InstallJump(addr::CrtFopen,      reinterpret_cast<void*>(&Hook_CrtFopen));
    LogPrintf("[VFS] Modding VFS hooks installed (CatalogLookup 0x%X + CrtFopen 0x%X)\n",
              addr::CatalogLookup, addr::CrtFopen);
}

void VfsModReload() {
    if (!g_vfsLockInited) return;
    EnterCriticalSection(&g_vfsLock);
    RebuildModIndexLocked();
    LeaveCriticalSection(&g_vfsLock);
}

void VfsModShutdown() {
    if (!g_vfsLockInited) return;
    EnterCriticalSection(&g_vfsLock);
    g_modFiles.clear();
    LeaveCriticalSection(&g_vfsLock);
    DeleteCriticalSection(&g_vfsLock);
    g_vfsLockInited = false;
}

} // namespace m1

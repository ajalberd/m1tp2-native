#include "campaign_hud.h"
#include "common.h"
#include "patch.h"
#include <windows.h>
#include <algorithm>
#include <cstring>

namespace m1 {

namespace {

uint8_t* g_activeCampaign = nullptr;
int      g_mapScreenDepth = 0;
int      g_menuScreenDepth = 0;

using WinPermilleFn    = int(__cdecl*)(void* campSub);
using ShowCampMapFn    = void(__cdecl*)(void* camp);
using CampPreBattleFn  = int(__cdecl*)(void* camp);
using CampMainLoopFn   = int(__cdecl*)(void* camp);

// Exact 1998 MicroProse AI-Front / 10 Bugfix (0x42A395):
// 0x429F60 returns win rate in permille (0..1000). Checkpoint evaluator 0x429EC0 divides by 10,
// but SimulateOtherFronts (0x42A370) forgot to divide by 10, comparing a 0..100 roll against 0..1000!
int __cdecl Hook_GetWinPctForAiFronts(void* campSub) {
    int permille = reinterpret_cast<WinPermilleFn>(addr::CampaignGetWinPermille)(campSub);
    if (g_cfg.fixCampaignFrontBug) {
        int pct = permille / 10;
        LogPrintf("[CAMPAIGN] AI-Fronts winPct=%d%% (fixed from buggy permille=%d)\n", pct, permille);
        return pct;
    }
    return permille;
}

void __cdecl Hook_ShowCampaignMap(void* camp) {
    if (camp) g_activeCampaign = reinterpret_cast<uint8_t*>(camp);
    g_mapScreenDepth++;
    reinterpret_cast<ShowCampMapFn>(addr::ShowCampaignMap)(camp);
    g_mapScreenDepth--;
}

int __cdecl Hook_CampaignPreBattle(void* camp) {
    if (camp) g_activeCampaign = reinterpret_cast<uint8_t*>(camp);
    g_menuScreenDepth++;
    int res = reinterpret_cast<CampPreBattleFn>(addr::CampaignPreBattle)(camp);
    g_menuScreenDepth--;
    return res;
}

int __cdecl Hook_CampaignMainLoop(void* camp) {
    if (camp) g_activeCampaign = reinterpret_cast<uint8_t*>(camp);
    int res = reinterpret_cast<CampMainLoopFn>(addr::CampaignMainLoop)(camp);
    g_activeCampaign = nullptr;
    return res;
}

static bool IsReadablePtr(const void* p, size_t bytes) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    uintptr_t start = reinterpret_cast<uintptr_t>(p);
    uintptr_t end   = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return (start + bytes <= end);
}

} // namespace

void CampaignInstallHooks() {
    // 1. Fix 1998 AI-front / 10 bug at call site 0x42A395 inside SimulateOtherFronts
    InstallCall(addr::CallAiFrontWinPct, reinterpret_cast<void*>(&Hook_GetWinPctForAiFronts));

    // 2. Track active CampaignState* and Campaign Map / Pre-Battle UI visibility
    InstallCall(addr::CallShowCampMap1,    reinterpret_cast<void*>(&Hook_ShowCampaignMap));
    InstallCall(addr::CallShowCampMap2,    reinterpret_cast<void*>(&Hook_ShowCampaignMap));
    InstallCall(addr::CallCampPreBattle1,  reinterpret_cast<void*>(&Hook_CampaignPreBattle));
    InstallCall(addr::CallCampPreBattle2,  reinterpret_cast<void*>(&Hook_CampaignPreBattle));
    InstallCall(addr::CallCampPreBattle3,  reinterpret_cast<void*>(&Hook_CampaignPreBattle));
    InstallCall(addr::CallCampMainLoop1,   reinterpret_cast<void*>(&Hook_CampaignMainLoop));
    InstallCall(addr::CallCampMainLoop2,   reinterpret_cast<void*>(&Hook_CampaignMainLoop));
    InstallCall(addr::CallCampMainLoop3,   reinterpret_cast<void*>(&Hook_CampaignMainLoop));

    LogPrintf("[CAMPAIGN] Installed AI-front /10 bugfix & live Campaign Intel hooks (fix=%d, hud=%d)\n",
              g_cfg.fixCampaignFrontBug ? 1 : 0, g_cfg.campaignIntelHud ? 1 : 0);
}

void CampaignGetSnapshot(CampaignSnapshot& out) {
    out = CampaignSnapshot{};
    if (!g_activeCampaign || !IsReadablePtr(g_activeCampaign, 0x520)) return;

    const uint8_t* camp = g_activeCampaign;
    const uint8_t* sub  = camp + 0x3F4;

    out.valid        = true;
    out.onMapScreen  = (g_mapScreenDepth > 0);
    out.inCampaignUi = (g_mapScreenDepth > 0 || g_menuScreenDepth > 0);

    out.theaterId    = *reinterpret_cast<const int32_t*>(sub + 0x00);
    out.wins         = std::max(0, *reinterpret_cast<const int32_t*>(sub + 0x04));
    out.losses       = std::max(0, *reinterpret_cast<const int32_t*>(sub + 0x08));
    out.totalBattles = out.wins + out.losses;
    out.winPct       = (out.totalBattles > 0) ? ((out.wins * 100) / out.totalBattles) : 0;
    out.difficulty   = std::clamp(*reinterpret_cast<const int32_t*>(sub + 0x0C), 0, 2);
    out.elapsedHours = std::max(0, *reinterpret_cast<const int32_t*>(sub + 0x60));
    out.elapsedDays  = std::max(0, *reinterpret_cast<const int32_t*>(sub + 0x68));
    out.playerFront  = std::clamp(*reinterpret_cast<const int32_t*>(camp + 0x460), 0, 3);

    // Determine next unfired campaign checkpoint (matches 0x429DE0 & 0x429EC0 tables at 0x4866A8..0x486707)
    static const int kCpBattles[4] = { 10, 14, 21, 30 };
    static const int kCpHours[4]   = { 100, 175, 240, 480 };
    static const int kWinThresh[3][4] = {
        { 80, 80, 70, 70 }, // difficulty 0 (Hard)
        { 70, 70, 60, 60 }, // difficulty 1 (Medium)
        { 60, 60, 50, 50 }, // difficulty 2 (Easy)
    };
    static const int kLoseThresh[4] = { 30, 40, 40, 40 };

    int cp = 3;
    for (int i = 0; i < 3; ++i) {
        int fired = *reinterpret_cast<const int32_t*>(sub + 0x1C + i * 4);
        if (fired == 0) { cp = i; break; }
    }
    out.nextCpIdx     = cp;
    out.nextCpBattles = kCpBattles[cp];
    out.nextCpHours   = kCpHours[cp];
    out.winThreshold  = kWinThresh[out.difficulty][cp];
    out.loseThreshold = kLoseThresh[cp];

    // Read all 4 front-line tracks (camp + 0x468 + i*0x28) and node counts from g_FrontDataPtrs (0x873830)
    const uint32_t* frontPtrs = reinterpret_cast<const uint32_t*>(addr::g_FrontDataPtrs);
    for (int i = 0; i < 4; ++i) {
        const uint8_t* fr = camp + 0x468 + i * 0x28;
        out.fronts[i].active    = (*reinterpret_cast<const int32_t*>(fr + 0x00) != 0);
        out.fronts[i].curNode   = *reinterpret_cast<const int32_t*>(fr + 0x04);
        out.fronts[i].lastDelta = *reinterpret_cast<const int32_t*>(fr + 0x10);

        int maxNodes = 0;
        const int32_t* dat = reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(frontPtrs[i]));
        if (IsReadablePtr(dat, 4)) {
            int n = *dat;
            if (n > 1 && n <= 200) maxNodes = n;
        }
        out.fronts[i].maxNodes = maxNodes;
        if (maxNodes > 1) {
            int clamped = std::clamp(out.fronts[i].curNode, 0, maxNodes - 1);
            // Node 0 = Enemy HQ (100% friendly advance), Node N-1 = Friendly HQ (0%)
            out.fronts[i].progressPct = ((maxNodes - 1 - clamped) * 100) / (maxNodes - 1);
        } else {
            out.fronts[i].progressPct = 50;
        }
    }
}

} // namespace m1

#pragma once
#include <cstdint>

namespace m1 {

struct CampaignFrontInfo {
    bool active    = false;
    int  curNode   = 0;
    int  maxNodes  = 0;
    int  lastDelta = 0; // <0 advancing toward enemy, 0 holding, >0 retreating
    int  progressPct = 50; // 0% = Friendly HQ, 100% = Enemy HQ
};

struct CampaignSnapshot {
    bool valid         = false;
    bool onMapScreen   = false;
    bool inCampaignUi  = false;
    int  theaterId     = 0;
    int  wins          = 0;
    int  losses        = 0;
    int  totalBattles  = 0;
    int  winPct        = 0;
    int  difficulty    = 1; // 0=Hard, 1=Medium, 2=Easy
    int  elapsedHours  = 0;
    int  elapsedDays   = 0;
    int  nextCpIdx     = 0; // 0..3
    int  nextCpBattles = 10;
    int  nextCpHours   = 100;
    int  winThreshold  = 70;
    int  loseThreshold = 30;
    int  playerFront   = 0;
    CampaignFrontInfo fronts[4];
};

void CampaignInstallHooks();
void CampaignGetSnapshot(CampaignSnapshot& out);

} // namespace m1

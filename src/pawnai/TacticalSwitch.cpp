#include "stdafx.h"
#include "TacticalSwitch.h"
#include "../CombatBus.h"
namespace PawnAI {
void TacticalSwitch::Init(){
    busId = CombatBus::Instance().Subscribe([this](const ::CombatReport& r){ onReport(r); });
}
void TacticalSwitch::Shutdown(){ if(busId!=-1) CombatBus::Instance().Unsubscribe(busId); busId=-1; }
void TacticalSwitch::onReport(const ::CombatReport& r){ lastCategory = r.dominantCategory; }
int TacticalSwitch::GetActivePreset(int userPreset) const {
    if(!enabled || lastCategory<0) return userPreset;
    static const int map[5] = {1,5,0,3,3}; // small→CrowdCtrl, medium→Balanced, large→BossKiller, flying→Ranged, mage→Ranged
    if(lastCategory>=0 && lastCategory<5) return map[lastCategory];
    return userPreset;
}
}

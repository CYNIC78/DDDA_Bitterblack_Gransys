#include "stdafx.h"
#include "TacticalSwitch.h"
#include "../CombatBus.h"

namespace PawnAI {

void TacticalSwitch::Init(){
    busId = CombatBus::Instance().Subscribe([this](const ::CombatReport& r){ onReport(r); });
}

void TacticalSwitch::Shutdown(){
    if(busId != -1) {
        CombatBus::Instance().Unsubscribe(busId);
        busId = -1;
    }
}

void TacticalSwitch::onReport(const ::CombatReport& r){
    if (r.inCombat) {
        lastCategory = r.dominantCategory;
    } else {
        lastCategory = -1; // Вне боя возвращаем пользовательский пресет
    }
}

int TacticalSwitch::GetActivePreset(int userPreset) const {
    if (!enabled || lastCategory < 0) return userPreset;
    // 0=small (CrowdCtrl=1), 1=medium (Balanced=5), 2=large (BossKiller=0), 3=flying (RangedHunter=3), 4=mage (RangedHunter=3), 5=boss (BossKiller=0)
    static const int map[6] = { 1, 5, 0, 3, 3, 0 };
    if (lastCategory >= 0 && lastCategory < 6) return map[lastCategory];
    return userPreset;
}

}

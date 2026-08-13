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
        lastCategory = -1; // hit window closed — presence may still hold a category
    }
}

int TacticalSwitch::GetActivePreset(int userPreset) const {
    if (!enabled) return userPreset;

    int cat = lastCategory; // hit path (CombatIntel). May be 5 if a hare was punched.

    const WorldReport& w = CombatBus::Instance().LastWorld();
    DWORD now = GetTickCount();
    bool worldFresh = (w.count > 0 && w.timestampMs != 0 && (now - w.timestampMs) < 3000);
    if (worldFresh && w.dominantCategory >= 0) {
        // Presence before the first swing. World category never uses gid 0x61.
        if (cat < 0) cat = w.dominantCategory;
    }

    if (cat < 0) return userPreset;
    // 0=small (CrowdCtrl=1), 1=medium (Balanced=5), 2=large (BossKiller=0),
    // 3=flying (RangedHunter=3), 4=mage (RangedHunter=3), 5=boss (BossKiller=0)
    static const int map[6] = { 1, 5, 0, 3, 3, 0 };
    if (cat < 6) return map[cat];
    return userPreset;
}

}

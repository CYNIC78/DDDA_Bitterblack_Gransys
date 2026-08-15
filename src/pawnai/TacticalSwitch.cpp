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

// Ситуативная поправка к базе. Категория врага → дельта.
// 0=small, 1=medium, 2=large, 3=flying, 4=mage, 5=boss
void TacticalSwitch::GetDelta(const float* base, float* delta) const {
    if (!enabled || !base || !delta) return;

    int cat = lastCategory; // hit path (CombatIntel). May be 5 if a hare was punched.

    WorldReport w = CombatBus::Instance().LastWorld();
    DWORD now = MsNow();
    bool worldFresh = (w.count > 0 && w.timestampMs != 0 && (now - w.timestampMs) < 3000);
    if (worldFresh && w.dominantCategory >= 0) {
        // Presence before the first swing. World category never uses gid 0x61.
        if (cat < 0) cat = w.dominantCategory;
    }

    if (cat < 0) return;

    switch (cat) {
    case 0: // small (hordes) — давить массу рядом
        delta[I_MITIGATOR]  += 150.f;
        delta[I_SCATHER]    += 60.f;
        break;
    case 1: // medium (skeletons, saurians, humans)
        delta[I_CHALLENGER] += 100.f;
        delta[I_SCATHER]    += 60.f;
        break;
    case 2: // large (cyclops, ogres, golems)
        delta[I_SCATHER]    += 150.f;
        delta[I_CHALLENGER] += 100.f;
        break;
    case 3: // flying (harpies, griffins)
        delta[I_CHALLENGER] += 150.f;
        break;
    case 4: // mage (wights, liches)
        delta[I_CHALLENGER] += 120.f;
        delta[I_UTILITARIAN]+= 80.f;
        break;
    case 5: // boss (dragon, hydra, daimon)
        delta[I_SCATHER]    += 150.f;
        delta[I_CHALLENGER] += 150.f;
        break;
    default:
        break;
    }
}

}

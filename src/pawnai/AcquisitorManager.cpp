#include "stdafx.h"
#include "AcquisitorManager.h"
#include "../CombatBus.h"

namespace PawnAI {

void AcquisitorManager::Init(){
    busId = CombatBus::Instance().Subscribe([this](const ::CombatReport& r){ onReport(r); });
}

void AcquisitorManager::Shutdown(){
    if(busId != -1) CombatBus::Instance().Unsubscribe(busId);
    busId = -1;
}

void AcquisitorManager::onReport(const ::CombatReport& r){
    if(!enabled) return;
    wasInCombat = inCombat;
    inCombat = r.inCombat;
    if(inCombat){
        combatSeenMs = MsNow();
    } else if(wasInCombat){
        // Только что вышли из боя — засекаем старт boost-окна.
        combatEndedMs = MsNow();
    }
}

// Поправка к целевым весам. Acquisitor:
//   в бою — вниз; после боя — временно вверх; потом плавно домой (0).
void AcquisitorManager::GetDelta(const float* base, float* delta){
    if(!enabled || !base || !delta) return;

    DWORD now = MsNow();
    bool combat = inCombat || (combatSeenMs != 0 && (now - combatSeenMs) < kCombatTailMs);

    float cur  = base[I_ACQUISITOR];
    float want;
    State st;

    if(combat){
        want = suppressFloor;
        if(want > cur) want = cur;   // не поднимаем, если игрок сам поставил ниже
        st = ST_SUPPRESS;
    } else if(combatEndedMs != 0){
        DWORD elapsed = now - combatEndedMs;
        if(elapsed < boostWindowMs){
            // Бой только что кончился — временный подъём (пылесосит лут).
            want = cur + boostAmount;
            if(want > 1000.0f) want = 1000.0f;
            st = ST_BOOST;
        } else if(elapsed < boostWindowMs + returnMs){
            // Плавный возврат от boostAmount к 0.
            float t = (float)(elapsed - boostWindowMs) / (float)returnMs;
            if(t < 0.0f) t = 0.0f;
            if(t > 1.0f) t = 1.0f;
            want = cur + boostAmount * (1.0f - t);
            st = ST_RETURN;
        } else {
            want = cur;   // «домой» — дельта 0, ползунок игрока рулит
            st = ST_IDLE;
        }
    } else {
        want = cur;
        st = ST_IDLE;
    }

    delta[I_ACQUISITOR] += (want - cur);
    lastAppliedDelta = want - cur;
    lastState = st;
}

}

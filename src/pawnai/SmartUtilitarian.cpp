#include "stdafx.h"
#include "SmartUtilitarian.h"
#include "../CombatBus.h"
#include "../CombatIntel.h" // только для фоллбэка вне боя

extern BYTE** pBase; // из dinput8.cpp

namespace PawnAI {

void SmartUtilitarian::Init(){
    busId = CombatBus::Instance().Subscribe([this](const ::CombatReport& r){ onReport(r); });
}
void SmartUtilitarian::Shutdown(){ if(busId!=-1) CombatBus::Instance().Unsubscribe(busId); busId=-1; }

void SmartUtilitarian::onReport(const ::CombatReport& r){
    if(!enabled) return;
    lastConfidence = r.utilitarianConfidence; // 0.25..0.90 уже посчитан тренером через types.tsv+mStudyFlag
    // Вне боя тренер шлёт 0.5 — фоллбэк на общий known-count
    float conf = lastConfidence;
    if(conf==0.5f && !r.inCombat){
        int known = CountKnownEnemies();
        if(known==0) conf=0.20f;
        else if(known<=5) conf=0.35f;
        else if(known<=15) conf=0.35f + (known-5)*0.025f;
        else if(known<=40) conf=0.60f + (known-15)*0.01f;
        else conf=0.90f;
        lastConfidence = conf;
    }
    targetUtil = conf * 850.f;
}

void SmartUtilitarian::Process(float* incl){
    if(!enabled || !incl) return;
    // Низкое знание → переливаем
    if(lastConfidence < 0.5f){
        float excess = (0.5f - lastConfidence) * incl[I_UTILITARIAN];
        incl[I_UTILITARIAN] -= excess;
        incl[I_SCATHER]    += excess * 0.55f;
        incl[I_CHALLENGER] += excess * 0.30f;
        incl[I_MITIGATOR]  += excess * 0.15f;
    }
    incl[I_UTILITARIAN] += (targetUtil - incl[I_UTILITARIAN]) * smooth;
}

}

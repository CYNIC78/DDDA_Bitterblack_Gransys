#include "stdafx.h"
#include "SanitaryCordon.h"
#include "../CombatBus.h"
namespace PawnAI {
void SanitaryCordon::Init(){
    // Подписываемся на шину — можем реагировать на смену категории врага, но пока просто тикаем
    busId = CombatBus::Instance().Subscribe([this](const ::CombatReport& r){ onReport(r); });
}
void SanitaryCordon::Shutdown(){ if(busId!=-1) CombatBus::Instance().Unsubscribe(busId); busId=-1; }
void SanitaryCordon::onReport(const ::CombatReport&){ /* пока не используем, но готов к расширению */ }
void SanitaryCordon::Process(float* incl){
    if(!enabled || !incl) return;
    float useful[I_COUNT]; int n=0;
    for(int i=0;i<I_COUNT;i++) if(GetInclCategory(i)==CAT_USEFUL) useful[n++]=incl[i];
    for(int i=0;i<n-1;i++) for(int j=i+1;j<n;j++) if(useful[j]>useful[i]){ float t=useful[i]; useful[i]=useful[j]; useful[j]=t; }
    float cap = (n>=3)? useful[2] : 500.f;
    for(int i=0;i<I_COUNT;i++){
        if(GetInclCategory(i)!=CAT_JUNK) continue;
        if(incl[i]>cap){
            float decay=(incl[i]-cap)*0.05f; if(decay<0.5f) decay=0.5f;
            incl[i]-=decay;
        }
    }
}
}

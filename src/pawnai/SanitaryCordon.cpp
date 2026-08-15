#include "stdafx.h"
#include "SanitaryCordon.h"
#include "../CombatBus.h"
namespace PawnAI {
void SanitaryCordon::Init(){
    // Подписываемся на шину — можем реагировать на смену категории врага,
    // но пока просто фильтруем цель.
    busId = CombatBus::Instance().Subscribe([this](const ::CombatReport& r){ onReport(r); });
}
void SanitaryCordon::Shutdown(){ if(busId!=-1) CombatBus::Instance().Unsubscribe(busId); busId=-1; }
void SanitaryCordon::onReport(const ::CombatReport&){ /* пока не используем, но готов к расширению */ }

void SanitaryCordon::ApplyCap(float* target){
    if(!enabled || !target) return;

    // Кап = 3-я по величине ПОЛЕЗНАЯ инклинация (динамика, без хардкода).
    float useful[I_COUNT]; int n=0;
    for(int i=0;i<I_COUNT;i++) if(GetInclCategory(i)==CAT_USEFUL) useful[n++]=target[i];
    for(int i=0;i<n-1;i++) for(int j=i+1;j<n;j++) if(useful[j]>useful[i]){ float t=useful[i]; useful[i]=useful[j]; useful[j]=t; }
    float cap = (n>=3)? useful[2] : 500.f;
    lastCap = cap;

    // Мусорные — вниз, если выше капа. Это фильтр ЦЕЛИ, не самих значений.
    for(int i=0;i<I_COUNT;i++){
        if(GetInclCategory(i)!=CAT_JUNK) continue;
        if(target[i]>cap) target[i]=cap;
    }
}
}

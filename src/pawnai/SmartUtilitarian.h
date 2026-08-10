#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
/**
 * SmartUtilitarian — модуль-слушатель шины. Тренер (CombatIntel) кричит в мегафон,
 * а этот модуль сам решает как перелить Utilitarian в Scather/Challenger/Mitigator
 * на основе знания из types.tsv + mStudyFlag.
 * 
 * Это БАЗОВЫЙ пример для всех будущих модулей PawnAI.
 */
namespace PawnAI {
class SmartUtilitarian {
public:
    void Init();
    void Shutdown();
    bool enabled = true;
    float smooth = 0.02f; // для target-лерпа
    void Process(float* incl); // вызывается каждый кадр оркестратором (а не напрямую из шины)
    float lastConfidence = 0.5f;
    float targetUtil = 800.f;
private:
    int busId = -1;
    void onReport(const ::CombatReport& r);
};
}

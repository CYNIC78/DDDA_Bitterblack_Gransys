#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
/**
 * SmartUtilitarian — модуль-слушатель шины. Тренер (CombatIntel) кричит в мегафон,
 * а этот модуль возвращает delta[] — поправку к целевым весам.
 *
 * НОВАЯ МОДЕЛЬ: модуль НЕ пишет в incl[] и НЕ трогает ползунки.
 * Он возвращает поправку к базовой цели:
 *   - низкое знание → переливаем Utilitarian в Scather/Challenger/Mitigator
 *   - знание растёт → Utilitarian подтягивается к targetUtil
 *
 * Оркестратор складывает finalTarget = anchor + delta и делает один лерп.
 *
 * Это БАЗОВЫЙ пример для всех будущих модулей PawnAI.
 */
namespace PawnAI {
class SmartUtilitarian {
public:
    void Init();
    void Shutdown();
    bool enabled = true;
    float smooth = 0.02f; // для target-лерпа (модуль плавно «дышит»)
    void GetDelta(const float* base, float* delta); // вызывается оркестратором
    float lastConfidence = 0.5f;
    float targetUtil = 800.f;
private:
    int busId = -1;
    void onReport(const ::CombatReport& r);
};
}

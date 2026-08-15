#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
/**
 * SanitaryCordon — санитар, но в новой модели он фильтрует ЦЕЛЬ, а не incl[].
 *
 * Раньше: резал мусорные инклинации в самих значениях → драка с пресетом.
 * Теперь:  получает finalTarget (anchor + delta) и зажимает мусорные
 *          (Guardian/Nexus/Acquisitor) до динамического капа.
 *          Лерп делает ОДИН — к уже отфильтрованной цели. Никакой драки.
 *
 * ПРЕДУПРЕЖДЕНИЕ: в кризисе (Emergency) кордон обязан замолчать — иначе
 * он срежет Guardian, который мы специально подняли для спасения игрока.
 * Оркестратор просто не вызывает ApplyCap в override-режиме.
 */
namespace PawnAI {
class SanitaryCordon {
public:
    void Init();
    void Shutdown();
    bool enabled = true;
    // Фильтр цели: мусорные инклинации target[] не выше динамического капа.
    void ApplyCap(float* target);
    float lastCap = 500.0f;  // для UI: какой кап сейчас
private:
    int busId = -1;
    void onReport(const ::CombatReport& r);
};
}

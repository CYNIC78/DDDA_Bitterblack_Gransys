#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
/**
 * SanitaryCordon — модуль-санитар. Сам слушает шину, сам режет мусорные инклинации.
 * Независим от других модулей. Просто подписан на CombatBus и тикает.
 */
namespace PawnAI {
class SanitaryCordon {
public:
    void Init();
    void Shutdown();
    bool enabled = true;
    // вызывается оркестратором каждый кадр ДО записи в память
    void Process(float* incl);
private:
    int busId = -1;
    void onReport(const ::CombatReport& r);
};
}

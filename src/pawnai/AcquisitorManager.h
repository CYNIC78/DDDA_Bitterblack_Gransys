#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"

/**
 * AcquisitorManager — мягкий менеджер Acquisitor, единственной инклинации,
 * бесполезной в бою.
 *
 * Бывший SanitaryCordon. Build 55: Guardian/Nexus вышли из-под «кордона» —
 * теперь это доктрины (CAT_DOCTRINE), а не мусор; их реализацией займётся
 * GuardianDoctrine. Здесь остаётся только Acquisitor:
 *
 *   в бою   — подавляем: цель Acquisitor падает к suppressFloor, пешка
 *             не отвлекается на лут посреди драки;
 *   бой кончился — на ограниченное время приподнимаем дельту (boostAmount),
 *             пешка «пылесосит» лут вокруг;
 *   затем    — плавно возвращаем «домой», к ползунку игрока (delta → 0).
 *
 * Возвращает delta[] к целевым весам; сглаживание делает общий LERP
 * оркестратора. Значение на ползунке (anchor) остаётся «правдой» игрока —
 * менеджер лишь добавляет ситуативную поправку поверх.
 */
namespace PawnAI {
class AcquisitorManager {
public:
    void Init();
    void Shutdown();
    bool enabled = true;

    // Поправка к целевым весам. Трогает ТОЛЬКО I_ACQUISITOR.
    void GetDelta(const float* base, float* delta);

    // Для UI/диагностики.
    enum State { ST_IDLE, ST_SUPPRESS, ST_BOOST, ST_RETURN };
    State lastState = ST_IDLE;
    float lastAppliedDelta = 0.0f;

    float suppressFloor  = 100.0f;     // в бою Acquisitor не выше
    float boostAmount    = 180.0f;     // пост-бойный подъём (пылесосит лут)
    DWORD boostWindowMs  = 8000;       // сколько держим подъём после боя
    DWORD returnMs       = 4000;       // сколько длится плавный возврат «домой»
    static const DWORD kCombatTailMs = 1500; // держим «бой» после последнего отчёта

private:
    int   busId = -1;
    bool  inCombat = false;
    bool  wasInCombat = false;
    DWORD combatSeenMs = 0;   // последний «боевой» отчёт
    DWORD combatEndedMs = 0;  // момент выхода из боя (для boost-окна)
    void onReport(const ::CombatReport& r);
};
}

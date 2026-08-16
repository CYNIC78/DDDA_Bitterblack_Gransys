#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"

/**
 * GuardianDoctrine — поведенческая доктрина инклинации Guardian (и, позже,
 * Nexus — тем же ядром, но с другим anchor).
 *
 * ЭТО УЛУЧШЕНИЕ ИНКЛИНАЦИИ, а НЕ отдельная фича «спаси игрока».
 * Отдельная временная система (Critical Response) строится позже ПОВЕРХ
 * этого ядра — она включается на всю партию, когда игрок в опасности.
 *
 * Build 56 (draft, observe-only). Реализует решение, принятое в архитектуре:
 * мы НЕ пишем второй AI поверх Capcom и НЕ трогаем сохранённые значения
 * склонностей. Доктрина читает характер пешки (Guardian) и исправляет
 * ПОВЕДЕНЧЕСКУЮ РЕАЛИЗАЦИЮ этой черты — выдаёт «семантический совет по
 * приоритету», который позднее применится через штатный priority-слой.
 *
 * Ключевой принцип (из архитектуры):
 *
 *   Threat Anchor    — вокруг кого выбираются опасные враги;
 *   Movement Anchor  — куда физически должна двигаться пешка.
 *
 *   Guardian: anchor = Arisen. Nexus: anchor = выбранная пешка.
 *   Вокация пешки решает КАК реагировать: мили-пешка перехватывает,
 *   дальняя держит позицию и ведёт огонь по угрозе в зоне, кастер —
 *   дальняя поддержка без сближения.
 *
 * ЧТО ДОКТРИНА НЕ ДЕЛАЕТ (никогда):
 *   - не пишет target pointer напрямую;
 *   - не включает cPlAct вручную;
 *   - не телепортирует и не заставляет атаковать недоступную цель;
 *   - не вмешивается в damage/down/carry execution;
 *   - не меняет save.
 *
 * СЕЙЧАС (Build 56 draft) модуль работает ТОЛЬКО в observe-only: он
 * считает решение и отдаёт отчёт, но не пишет в память игры. Первая
 * запись появится в Build 56 A/B — снятие одного доказанного Guardian-
 * штрафа (code 54 WpnDaggerAtk) через транзакционный priority-профиль.
 */
namespace PawnAI {

// ============ Вход: снимок ситуации (SitRep) ============
// Заполняет BuildGuardianSitRep() (адаптер источника). Доктрина сама
// память НЕ трогает — только решает. Так её легко тестировать отдельно
// и держать честной относительно «не угадываем offsets».

struct GuardianThreat {
    float x, y, z;
    const char* kind;        // "uEm0100", "uHumanEnemy", ... (владелец — источник)
    bool  engaged;           // (резерв) враг уже в бою с партией
};

struct GuardianSitRep {
    // Якорь защиты (для Guardian — Arisen).
    float anchorX, anchorY, anchorZ;
    bool  anchorValid;
    int   anchorVocation;     // VocationId

    // Носитель доктрины (главная пешка).
    float pawnX, pawnY, pawnZ;
    bool  pawnValid;
    int   pawnVocation;

    // Характер пешки — для ownership доктрины и роли.
    float guardian, nexus;

    bool  inCombat;

    GuardianThreat threats[32];
    int   threatCount;

    DWORD timestampMs;
};

// ============ Выход: семантический совет по приоритету ============

enum AdviceAction {
    ADV_NONE = 0,
    ADV_REMOVE_PENALTY,   // убрать доказанный Guardian-штраф с offensive intent
    ADV_RAISE_INTERCEPT,  // поднять intent перехвата угрозы у anchor
    ADV_HOLD_NEAR_ANCHOR, // сохранить приоритет нахождения рядом с anchor
};

struct GuardianAdvice {
    AdviceAction action;
    uint32_t     code;        // raw priority code (если известен), иначе 0xFFFFFFFF
    const char*  intentKey;   // семантическое имя (если mapped), иначе nullptr
    int32_t      deltaS32;    // предлагаемое смещение AddS32 (для apply-фазы)
    const char*  reason;      // человекочитаемая причина (для UI/теллеметрии)
};

struct GuardianReport {
    bool        doctrineActive;   // нашлась ли активная доктрина
    const char* owner;            // "Guardian" / "Nexus" / "none"
    const char* responseMode;     // "Intercept"/"RangedHold"/"Support"/"Adaptive"/...
    bool        anchorResolved;   // известна ли позиция якоря
    bool        pawnResolved;     // известна ли позиция пешки
    int         threatsInZone;
    float       nearestThreatDist; // 1e9f, если угроз нет
    float       pawnAnchorDist;    // 1e9f, если не известно
    bool        zoneEngaged;       // внутренний флаг захвата зоны (hysteresis)
    int         adviceCount;
    GuardianAdvice advice[8];
    bool        observeOnly;
};

// ============ Доктрина ============

class GuardianDoctrine {
public:
    bool  enabled = true;
    bool  observeOnly = true;     // Build 56: пока ТОЛЬКО observe, без записи

    // Зона ответственности вокруг anchor. Все радиусы/дистанции задаются
    // в МЕТРАХ; внутрь Decide() переводит их в мировые единицы через
    // worldUnitsPerMeter (Build 56.3: мир измеряется в ~сантиметрах).
    float protectionRadius = 12.0f;   // м
    float leashDistance     = 18.0f;   // м
    // Hysteresis против дребезга на границе зоны.
    float hysteresisEnter   = 1.5f;   // м: допуск на входе (когда ещё не engaged)
    float hysteresisExit    = 3.0f;   // м: сколько «простить» при выходе
    DWORD minDwellMs        = 2000;   // мин. время удержания engaged-режима
    // Масштаб мировых координат: сколько world-единиц в одном метре.
    // 100.0 = сантиметры (подтверждено косвенно: AIPlActParam 500..4000 и
    // гоблин-сенсор 1500 осмысленны только как сантиметры). Точный фактор
    // уточняется touch-тестом; настраивается в ini.
    float worldUnitsPerMeter = 100.0f;

    void Init();
    void Shutdown();

    // Одна итерация: SitRep -> отчёт (совет). Память игры НЕ пишет.
    void Decide(const GuardianSitRep& s, GuardianReport& out);

private:
    bool  zoneEngaged = false;
    DWORD engagedSinceMs = 0;
    void ResetState();
};

// Адаптер источника: заполняет SitRep тем, что уже подтверждено
// (враги из WorldReport, вокация, инклинации, бой). Позиции anchor/pawn
// пока НЕ резолвятся — помечаются invalid (это следующий шаг discovery).
void BuildGuardianSitRep(GuardianSitRep& s);

// ============ Build 57.1: динамический Guardian-фикс ============
// Включается флагом g_guardianFixEnabled (ini [pawnAI] guardianFix, off по
// умолчанию — vanilla). Каждый тик доктрина решает и передаёт в DevTools,
// применять ли снятие штрафа code 54 (-3 -> 0) — только когда угроза в зоне
// вокруг Аризена И пешка melee/hybrid.
// Build 57.3: distance-aware — штраф снимается только когда ближайшая угроза
// в радиусе ближнего перехвата (иначе пешка честно держит лук, у которого
// нет склонностных штрафов — см. SOURCE_OF_TRUTH §3.5.3).
// Build 58: градиент. Две зоны:
//   preempt (10 м) — враг «потенциально опасен»: снять штраф (-3 → 0), готовность;
//   melee   (6 м)  — враг в даггер-радиусе: лёгкий бонус (-3 → +2), фиксация на перехвате.
// Это реализует «зону телохранителя»: пешка заранее готовится к атаке, если
// враг пересекает внешнюю границу, и закрепляет роль без бинарного дёрганья.
extern bool g_guardianFixEnabled;
extern float g_guardianMeleeRadius;    // м (по умолч. 6)
extern float g_guardianPreemptRadius;  // м (по умолч. 10)
extern int32_t g_guardianDaggerBiasMelee;     // desired при угрозе в melee-радиусе (по умолч. +2)
extern int32_t g_guardianDaggerBiasPreempt;   // desired при угрозе в preempt-радиусе (по умолч. 0)
void GuardianDoctrineTick();

} // namespace PawnAI

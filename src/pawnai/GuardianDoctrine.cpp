#include "stdafx.h"
#include "runtime/Runtime.h"
#include "runtime/MemProbe.h"
#include "runtime/AggroWatch.h"
#include <math.h>
#include "GuardianDoctrine.h"
#include "../CombatBus.h"
#include "../devtools/DevTools.h"

extern BYTE** pBase; // из dinput8.cpp

namespace PawnAI {

// ================= Статичные контракты =================

// Подтверждённые Guardian-модификаторы из cmc.prt (см. docs/SOURCE_OF_TRUTH.md,
// «Personality/order rules» и чат-анализ). Это и есть «поводок со штрафами»,
// который мы переписываем через priority-совет.
//
// КАЖДАЯ строка помечена статусом доверия:
//   CONFIRMED  — runtime evidence (Build 45/46/51..53) + cmc.prt;
//   HYPOTHESIS — структура совпадает с соседом, но имя намерения не доказано.
//
// BUILD 73.27 — ИМЕНА БОЛЬШЕ НЕ ГАДАЕМ.
//
// Дамп загруженных целей планировщика дал полную таблицу «код -> имя цели»:
// массив ресурсов идёт от planner+0x08 с шагом 4, и номер слота И ЕСТЬ код
// (обоснование — GoapProbe.h). Проверка на наших же данных: 15 -> Air и
// 60 -> Em0600Cover совпали с тем, что уже стояло CONFIRMED, а 54 ->
// WpnDaggerAtk — с главным рычагом. Три попадания из трёх.
//
// Что это закрыло:
//   - код 13 был «party relation (HYPOTHESIS)», на деле Recovery;
//   - коды 4 и 66 в наборе тестовой пешки ПУСТЫ (слот нулевой) — имя им
//     даст только пешка другой вокации, гадать смысла нет;
//   - появились коды меча и двуручника, которых не хватало для дыры
//     «главный рычаг работает только для кинжалов» — см. kWeaponIntents.
struct GuardianModifier {
    uint32_t     code;
    int32_t      addS32;         // штатное смещение (штраф/бонус) от Capcom
    const char*  intentKey;      // семантическое имя (nullptr, если не mapped)
    const char*  status;         // "CONFIRMED" / "HYPOTHESIS"
    const char*  note;
};
static const GuardianModifier kGuardianModifiers[] = {
    {  4, +3, nullptr, "HYPOTHESIS", "slot empty on the test pawn - name unknown"   },
    { 13, -2, "Recovery", "CONFIRMED", "named by the goal-code table (was a guess)" },
    { 15, -2, "Air",  "CONFIRMED",  "Air - shared Guardian/Nexus"                  },
    { 54, -3, "WpnDaggerAtk", "CONFIRMED", "offensive dagger attack - MAIN A/B lever" },
    { 60, -3, "Em0600Cover",  "CONFIRMED",  "enemy-specific cover (not touched yet)" },
    { 66, -4, nullptr, "HYPOTHESIS", "slot empty on the test pawn - name unknown"   },
};
static const int kGuardianModifierCount = sizeof(kGuardianModifiers) / sizeof(kGuardianModifiers[0]);

// ПАРАЛЛЕЛИ ГЛАВНОГО РЫЧАГА ПО ОРУЖИЮ.
//
// Рычаг Guardian трогает код 54 (кинжалы), поэтому у Файтера и Воина он не
// даёт ничего — это записанная в докладе дыра. Соседние слоты того же
// семейства теперь известны по именам целей.
//
// ЧЕГО ЗДЕСЬ НЕТ: штатного смещения (addS32). Таблицу правил Guardian мы
// читали только для кинжалов, для остальных кодов её никто не снимал.
// Поэтому это КАРТА, а не рецепт: сначала дамп правил на пешке-Файтере,
// и только потом запись.
struct WeaponIntent { uint32_t code; const char* goal; const char* vocations; };
static const WeaponIntent kWeaponIntents[] = {
    { 52, "WpnSwordAtk",  "Fighter / Mystic Knight / Assassin (sword)" },
    { 53, "WpnGSwordAtk", "Warrior / Fighter (greatsword, longsword)"  },
    { 54, "WpnDaggerAtk", "Strider / Ranger / Assassin (daggers)"      },
    { 55, "WpnWandAtk",   "Mage / Sorcerer (staff, archistaff)"        },
    { 56, "WpnShieldAtk", "Fighter / Mystic Knight (shield)"           },
    { 57, "WpnBowAtk2",   "Strider / Ranger (bow, longbow) - from tu2" },
};
static const int kWeaponIntentCount = sizeof(kWeaponIntents) / sizeof(kWeaponIntents[0]);

// ================= Вспомогательные =================

static float Dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Враг или безобидная живность? Повторяем логику Runtime::KindIsEnemy,
// чтобы SitRep не считал зайцев (uEm8000/uEm8600) угрозами.
static bool SitRepIsEnemy(const char* kind)
{
    if (!kind || !kind[0]) return false;
    bool creature = (kind[0] == 'u' && kind[1] == 'E' && kind[2] == 'm')
                 || (strcmp(kind, "uHumanEnemy") == 0);
    if (!creature) return false;
    if (!strcmp(kind, "uEm8000") || !strcmp(kind, "uEm8600")) return false; // harmless
    return true;
}

static bool EnemyTargetingArisen(uintptr_t body, bool inCombatAction)
{
    if (inCombatAction) return true;
    const int nRow = Runtime::Aggro::RowCount();
    for (int i = 0; i < nRow; ++i) {
        const Runtime::Aggro::Row* r = Runtime::Aggro::RowAt(i);
        if (r && r->body == body) {
            return (r->targetMember == Runtime::Aggro::MEMBER_ARISEN);
        }
    }
    return false;
}

// ================= Доктрина =================

void GuardianDoctrine::Init()
{
    ResetState();

    // Карта оружейных намерений — в лог одной таблицей. Иначе она
    // осталась бы мёртвым кодом, а нужна она ровно тогда, когда тестер
    // сядет за пешку-Файтера: список говорит, какой код искать.
    logFile << "GuardianDoctrine: weapon intent codes (from the goal-code table)"
            << std::endl;
    for (int i = 0; i < kWeaponIntentCount; ++i) {
        char l[200];
        sprintf_s(l, "  code %2u  %-14s %s%s",
                  kWeaponIntents[i].code, kWeaponIntents[i].goal,
                  kWeaponIntents[i].vocations,
                  kWeaponIntents[i].code == 54 ? "   <-- current lever" : "");
        logFile << l << std::endl;
    }
    logFile << "  offsets (addS32) measured for code 54 only - audit the rest "
               "with a Fighter pawn before writing." << std::endl;
}

void GuardianDoctrine::Shutdown()
{
    ResetState();
}

void GuardianDoctrine::ResetState()
{
    zoneEngaged = false;
    engagedSinceMs = 0;
}

void GuardianDoctrine::Decide(const GuardianSitRep& s, GuardianReport& out)
{
    memset(&out, 0, sizeof(out));
    out.observeOnly = observeOnly;
    out.nearestThreatDist = 1e9f;
    out.pawnAnchorDist = 1e9f;
    out.anchorResolved = s.anchorValid;
    out.pawnResolved = s.pawnValid;

    if (!enabled) {
        out.owner = "none";
        out.doctrineActive = false;
        return;
    }

    // --- ownership: Guardian vs Nexus по значению инклинации ---
    // (совпадает с решением из чата: выше побеждает; tie — primary inclination).
    if (s.guardian < s.nexus - 1.0f) {
        // Nexus — доктрина с anchor = выбранная пешка. Build 56 draft её
        // ещё не реализует (Guardian — первый, он проще). Честно отдаём пусто.
        out.owner = "Nexus";
        out.responseMode = "not implemented yet";
        out.doctrineActive = true; // доктрина ЕСТЬ, но без advice
        return;
    }

    out.owner = "Guardian";
    out.doctrineActive = true;

    // --- без позиции ЯКОРЯ зону не посчитать. Позиция ПЕШКИ не обязательна:
    // зона/угрозы/совет зависят только от anchor; пешка нужна лишь для leash.
    if (!s.anchorValid) {
        out.responseMode = "anchor (Arisen) position UNRESOLVED";
        out.threatsInZone = 0;
        return;
    }

    // --- 1) угрозы в зоне ответственности вокруг anchor (двухуровневый периметр) ---
    // Внутренний (melee): критическая опасность вплотную к Аризену (4-6 м).
    // Внешний (preempt): упреждающий перехват целей, нацеленных на Аризена (6-12 м).
    float scale  = worldUnitsPerMeter > 0.0f ? worldUnitsPerMeter : 100.0f;
    float meleeR = g_guardianMeleeRadius * scale;
    float enterR = (protectionRadius + (zoneEngaged ? 0.0f : hysteresisEnter)) * scale;
    float exitR  = (protectionRadius + hysteresisExit) * scale;
    int   inZone = 0;
    float nearest = 1e9f;
    uintptr_t nearestBody = 0;
    const char* nearestKind = nullptr;
    bool  isCritical = false;

    for (int i = 0; i < s.threatCount; ++i) {
        const GuardianThreat& t = s.threats[i];
        float d = Dist3(s.anchorX, s.anchorY, s.anchorZ, t.x, t.y, t.z);
        if (d < nearest) nearest = d;

        const bool inCriticalPerimeter = (d <= meleeR);
        const bool inPreemptPerimeter  = (d <= enterR && t.targetingArisen);

        if (inCriticalPerimeter || inPreemptPerimeter) {
            ++inZone;
            if (!nearestBody || inCriticalPerimeter || d < nearest) {
                nearestBody = t.body;
                nearestKind = t.kind;
                isCritical = inCriticalPerimeter;
            }
        }
    }

    // --- 2) захват зоны + dwell/hysteresis выхода ---
    DWORD now = s.timestampMs ? s.timestampMs : MsNow();
    if (inZone > 0 && !zoneEngaged) {
        zoneEngaged = true;
        engagedSinceMs = now;
    } else if (inZone == 0 && zoneEngaged) {
        // Выход разрешён, только если даже с запасом (hysteresisExit)
        // рядом никого нет и прошёл minDwell.
        if (nearest > exitR) {
            if (now - engagedSinceMs >= minDwellMs)
                zoneEngaged = false;
        }
        // иначе: держим engaged — угроза ещё «в прощаемом» радиусе.
    }

    // --- 3) роль доктрины: вокация пешки × вокация Аризена (Build 63) ---
    // Мили-пешка: кастер-игрок → Protector (телохранитель), мили/лучник →
    // Assault (штурмовая поддержка). Гибрид — Adaptive (универсал).
    GuardianRole role = GuardianRoleOf(s.pawnVocation, s.anchorVocation);
    out.responseMode = GuardianRoleName(role);

    out.threatsInZone = inZone;
    // Дистанции на экран — в МЕТРАХ (raw world-units / scale).
    out.nearestThreatDist = nearest / scale;
    // pawn->Arisen нужна только для leash; если пешка не резолвлена — считаем
    // «не известно» (1e9), leash-совет просто не сработает.
    out.pawnAnchorDist = s.pawnValid
        ? Dist3(s.pawnX, s.pawnY, s.pawnZ, s.anchorX, s.anchorY, s.anchorZ) / scale
        : 1e9f;
    out.zoneEngaged = zoneEngaged;

    if (inZone > 0 && out.pawnAnchorDist <= (leashDistance + hysteresisExit)) {
        out.targetThreatBody = nearestBody;
        out.targetThreatKind = nearestKind;
        out.criticalThreat = isCritical;
    }

    // --- 4) совет по приоритету ---
    if (!zoneEngaged) return; // vanilla: угроз в зоне нет — не советуем ничего

    // 4a) ГЛАВНЫЙ рычаг Build 56 A/B: снять доказанный Guardian-штраф (-3)
    //     с offensive intent code 54 (WpnDaggerAtk), когда враг реально в зоне.
    //     Это НЕ форсирует атаку — штатный GOAP/eligibility сами решат,
    //     возможна ли атака; мы лишь убираем искусственную пассивность.
    // Build 63: совет по роли.
    //   - гибрид/мили: снять Guardian-штраф с кинжалов (code 54). Для Файтер/
    //     Варриор код оружия (меч/двуручник) пока не раскрыт — поймает
    //     Guardian-аудит; тогда добавим сюда sword/gsword code.
    //   - дальнобойная/кастер: не тянем к якорю (Threat ≠ Movement Anchor).
    VocationClass vc = VocationClassOf(s.pawnVocation);
    if (vc == VCL_MELEE || vc == VCL_HYBRID) {
        GuardianAdvice& a = out.advice[out.adviceCount++];
        a.action = ADV_REMOVE_PENALTY;
        a.code = 54;
        a.intentKey = "WpnDaggerAtk";
        a.deltaS32 = 0; // штраф -3 → 0
        a.reason = (role == GROLE_PROTECTOR)
            ? "Protector: threat near caster Arisen — lift Guardian -3 on dagger"
            : (role == GROLE_ASSAULT)
                ? "Assault: threat in Arisen zone — lift Guardian -3 on dagger"
                : "threat in Arisen zone: lift Guardian -3 on WpnDaggerAtk";
    } else {
        // Дальнобойная/кастер: не тянем к якорю. Поднимаем выбор цели в зоне.
        GuardianAdvice& a = out.advice[out.adviceCount++];
        a.action = ADV_RAISE_INTERCEPT;
        a.code = 0xFFFFFFFF;
        a.intentKey = nullptr; // потребует mapping ranged-interception intent
        a.deltaS32 = 0;
        a.reason = "ranged pawn: engage threat in zone without closing (needs intent mapping)";
    }

    // 4b) Поводок: если пешка ушла дальше leash — вернуть приоритет «рядом с якорем».
    // Только когда позиция пешки известна (иначе дистанция = 1e9 → совет молчит).
    if (s.pawnValid && out.pawnAnchorDist > leashDistance) {
        GuardianAdvice& a = out.advice[out.adviceCount++];
        a.action = ADV_HOLD_NEAR_ANCHOR;
        a.code = 0xFFFFFFFF;
        a.intentKey = "HoldNearAnchor";
        a.deltaS32 = 0;
        a.reason = "pawn beyond leash: reinforce stay-near-anchor priority";
    }
}

// ================= Адаптер источника =================
// Заполняет всё, что уже подтверждено. Позиции uPlayer (anchor) и uCmc (pawn)
// в WorldReport отсутствуют — это следующий discovery-шаг. Пока честно
// помечаем их invalid: доктрина отработает и скажет «anchor position UNRESOLVED».

void BuildGuardianSitRep(GuardianSitRep& s)
{
    memset(&s, 0, sizeof(s));
    s.timestampMs = MsNow();

    // Вокации (record-based, подтверждено Build 55.1).
    if (pBase && *pBase) {
        uintptr_t playerRec = (uintptr_t)(*pBase) + PLAYER_BASE;
        uintptr_t pawnRec   = playerRec + PAWN_OFFSET;
        s.anchorVocation = ReadVocation(playerRec);
        s.pawnVocation   = ReadVocation(pawnRec);
    } else {
        s.anchorVocation = VOC_UNKNOWN;
        s.pawnVocation   = VOC_UNKNOWN;
    }

    // Инклинации главной пешки (для ownership).
    float incl[I_COUNT]; ReadAllIncl(incl, 0);
    s.guardian = incl[I_GUARDIAN];
    s.nexus    = incl[I_NEXUS];

    // Состояние боя.
    CombatReport bus = CombatBus::Instance().LastReport();
    s.inCombat = bus.inCombat;

    // Угрозы из WorldReport (только реальные враги, не живность).
    WorldReport w = CombatBus::Instance().LastWorld();
    s.threatCount = 0;
    for (int i = 0; i < w.count && s.threatCount < 32; ++i) {
        if (!SitRepIsEnemy(w.units[i].kind)) continue;
        GuardianThreat& t = s.threats[s.threatCount++];
        t.body = w.units[i].ptr;
        t.x = w.units[i].x;
        t.y = w.units[i].y;
        t.z = w.units[i].z;
        t.kind = w.units[i].kind;
        t.engaged = w.units[i].inCombatAction;
        t.targetingArisen = EnemyTargetingArisen(t.body, t.engaged);
    }

    // --- Позиции anchor/pawn: из census (DevTools) ---
    // ВАЖНО: актор-список (WorldReport) НЕ содержит uPlayer/uCmc — его фильтр
    // (LooksLikeCreatureAt) пропускает только uEm*/uHumanEnemy. Поэтому позиции
    // игрока/пешки берём из census-тел (PartyFindBodies), которые DevTools
    // резолвит один раз лениво (в фоне) и читает дёшево каждый тик.
    float ax = 0, ay = 0, az = 0, px = 0, py = 0, pz = 0;
    bool haveAnchor = Runtime::GetArisenWorldPos(&ax, &ay, &az);
    bool havePawn   = Runtime::GetMainPawnWorldPos(&px, &py, &pz);
    s.anchorValid = haveAnchor;
    s.pawnValid   = havePawn;
    s.anchorX = ax; s.anchorY = ay; s.anchorZ = az;
    s.pawnX = px; s.pawnY = py; s.pawnZ = pz;
}

// ================= Build 57.1: динамический Guardian-фикс =================

bool g_guardianFixEnabled = false; // vanilla по умолчанию (ini [pawnAI] guardianFix)
float g_guardianMeleeRadius = 6.0f;   // м, радиус ближнего перехвата (57.3)
float g_guardianPreemptRadius = 10.0f; // м, зона «потенциальной опасности» (58)
// ПОТОЛОК ПОДНЯТ ДО +5 (75.27).
//
// Замер 75.26 показал: фикс исправно применялся (`APPLIED code54 = 0`,
// затем `= 2`, десяток записей за бой) — а кинжалы пешка так и не
// достала. Значит снятия штрафа и бонуса +2 НЕ ХВАТАЕТ.
//
// Ориентир даёт сама игра: у Scather primary то же правило несёт +5, и
// при нём кинжалы занимают четверть боя. Разница между нашим +2 и
// ванильным +5 — три ведра приоритета, и, судя по всему, лук сидит как
// раз между ними.
int32_t g_guardianDaggerBiasMelee = 2;   // бонус в melee-радиусе (58; потолок +5)
int32_t g_guardianDaggerBiasPreempt = 0; // снятие штрафа в preempt-радиусе (58)

// ============================================================================
// РЫЧАГ, КОТОРЫЙ РАБОТАЕТ: СКЛОННОСТЬ, А НЕ ПРАВИЛО (75.28)
// ============================================================================
//
// РЕШАЮЩАЯ УЛИКА ИЗ ЛОГА 75.27. Фикс писал `AddS32` десятки раз за бой, и
// вместе с каждой записью печатался номер ведра, куда попала строка:
//
//     code54 = 0) slot=44   x21      code54 = 2) slot=44   x18
//     code54 = 0) slot=46   x17      code54 = 2) slot=46   x14
//
// Номер ведра НЕ ЗАВИСИТ от того, что мы записали: и при 0, и при +2
// строка оказывается то в 44, то в 46. Значит правка `AddS32` в рантайме
// **не двигает строку в живой раскладке**. Раскладка считается когда-то
// раньше — и наша запись до неё не доходит.
//
// Отсюда вывод, неприятный, но однозначный: весь наш Guardian-фикс всё
// это время был **поведенчески пустым**. Он честно писал, проверял и
// откатывал число, на которое игра в этот момент уже не смотрит.
//
// ЧТО ПРИ ЭТОМ РАБОТАЕТ ТОЧНО. Смена СКЛОННОСТЕЙ — доказано замером:
// Guardian 1000 -> 0 кадров кинжалов, Scather 1000 -> 1612 кадров, та же
// пешка, та же сессия, разница только в склонности. Значит игра
// пересчитывает раскладку по склонностям и делает это на живую.
//
// Поэтому доктрина переходит на тот рычаг, который доказан:
//   угроза в зоне телохранителя  -> временно поднять Scather;
//   зона очистилась              -> вернуть исходное значение.
//
// Это тот же принцип «совет, а не приказ»: мы не трогаем ни цель, ни
// действие, ни планировщик — только характер, который игра и без нас
// использует для выбора. И, в отличие от правки правила, у него есть
// живое доказательство.
static bool  g_inclLeverActive = false;
static float g_inclLeverBase[I_COUNT];    // ВСЕ склонности до вмешательства
static bool  g_inclLeverBaseOk = false;
static int   g_inclLeverWrites = 0;

// РЕЖИМ РЫЧАГА. Возражение тестера сняло с повестки первый вариант:
//
//   «Игрок окружён толпой мелочи, а за радиусом стоит циклоп. И что
//    сделает пешка с твоим решением?»
//
// С поднятым Scather — побежит к циклопу. Scather это «атакуй, и посильнее»,
// он меняет ВЫБОР ЦЕЛИ, а нам нужно всего лишь снять запрет на кинжалы.
// Лечить симптом лекарством с другим действием — плохая идея.
//
// Правильный рычаг вытекает из карты правил. Что Guardian реально делает
// на нашей пешке:
//
//   Guardian primary   : 54(-3)          <- запрет на кинжалы
//   Guardian secondary : 54(-2)          <- он же, послабее
//   Guardian tertiary  : 15(-2) 13(-2)   <- кинжалов НЕ КАСАЕТСЯ
//
// То есть достаточно, чтобы Guardian перестал быть первым или вторым по
// величине — и штраф на ближнюю атаку исчезает сам, без единого грамма
// чужой агрессии. Пешка сохраняет свои остальные склонности и решает,
// кого бить, по-прежнему сама.
//
// Понижение делается минимальным: Guardian опускается чуть ниже двух
// ближайших соседей, а не обнуляется. Как только зона очищается — точное
// исходное значение возвращается.
enum LeverMode { LEVER_OFF = 0, LEVER_DEMOTE_GUARDIAN = 1, LEVER_BOOST_SCATHER = 2 };
int  g_guardianLeverMode = LEVER_DEMOTE_GUARDIAN;   // ini [pawnAI] guardianLeverMode
float g_guardianScatherBoost = 800.0f;              // только для режима 2
bool  g_guardianUseInclLever = true;

static void InclLeverApply(bool want)
{
    if (want == g_inclLeverActive) return;

    float cur[I_COUNT];
    ReadAllIncl(cur, 0);

    if (want) {
        memcpy(g_inclLeverBase, cur, sizeof(g_inclLeverBase));
        g_inclLeverBaseOk = true;

        if (g_guardianLeverMode == LEVER_BOOST_SCATHER) {
            float v = g_guardianScatherBoost;
            if (v < 0.0f) v = 0.0f;
            if (v > 1000.0f) v = 1000.0f;
            if (v <= cur[I_SCATHER]) return;
            cur[I_SCATHER] = v;
        } else {
            // ПОНИЖЕНИЕ GUARDIAN ДО ТРЕТЬЕГО МЕСТА.
            //
            // Ищем две самые высокие ЧУЖИЕ склонности. Guardian ставим
            // ниже меньшей из них — тогда он третий, и правил на кинжалы
            // у него нет вовсе.
            int top1 = -1, top2 = -1;
            for (int i = 0; i < 9; ++i) {
                if (i == I_GUARDIAN) continue;
                if (top1 < 0 || cur[i] > cur[top1]) { top2 = top1; top1 = i; }
                else if (top2 < 0 || cur[i] > cur[top2]) top2 = i;
            }
            if (top1 < 0 || top2 < 0) return;

            float need = cur[top2] - 1.0f;
            if (need < 0.0f) {
                // Соседи в нуле — опустить Guardian ниже нельзя, поэтому
                // приподнимаем двух самых безобидных. Mitigator и
                // Challenger на кинжалы не влияют вовсе (карта правил),
                // так что характер боя от них не поедет.
                cur[I_MITIGATOR] = 120.0f;
                cur[I_CHALLENGER] = 100.0f;
                need = 60.0f;
            }
            if (cur[I_GUARDIAN] <= need) return;     // уже не первый — не трогаем
            cur[I_GUARDIAN] = need;
        }

        WriteAllIncl(cur, 0);
        const uintptr_t body = Runtime::MainPawnBody();
        if (body) {
            for (int i = 0; i < 9; ++i)
                if (cur[i] != g_inclLeverBase[i])
                    Runtime::PawnSetInclinationLive(body, i, cur[i]);
        }
        ++g_inclLeverWrites;
        g_inclLeverActive = true;

        char l[240];
        sprintf_s(l, "GuardianLever: threat in zone -> %s (Gua %.0f -> %.0f,"
                     " Sca %.0f -> %.0f), writes %d",
                  (g_guardianLeverMode == LEVER_BOOST_SCATHER)
                      ? "boost Scather" : "demote Guardian to third place",
                  g_inclLeverBase[I_GUARDIAN], cur[I_GUARDIAN],
                  g_inclLeverBase[I_SCATHER], cur[I_SCATHER], g_inclLeverWrites);
        logFile << l << std::endl;
    } else {
        if (g_inclLeverBaseOk) {
            WriteAllIncl(g_inclLeverBase, 0);
            const uintptr_t body = Runtime::MainPawnBody();
            if (body)
                for (int i = 0; i < 9; ++i)
                    Runtime::PawnSetInclinationLive(body, i, g_inclLeverBase[i]);
            char l[200];
            sprintf_s(l, "GuardianLever: zone clear -> restored (Gua %.0f, Sca %.0f)",
                      g_inclLeverBase[I_GUARDIAN], g_inclLeverBase[I_SCATHER]);
            logFile << l << std::endl;
        }
        g_inclLeverActive = false;
    }
}

void GuardianLeverRestore()
{
    if (g_inclLeverActive) InclLeverApply(false);
}

bool GuardianLeverIsActive() { return g_inclLeverActive; }

// У ОДНОГО ПРАВИЛА ОДИН ХОЗЯИН.
//
// Приборы, которые сами пишут в строку code 54 (развёртка по вёдрам),
// обязаны сначала спросить, не занят ли рычаг доктриной. Спрашивают через
// эту функцию, а не через сам флаг: девтулзам незачем видеть переменные
// продуктового слоя.
bool GuardianDoctrineOwnsRule() { return g_guardianFixEnabled; }

void GuardianDoctrineTick()
{
    static GuardianDoctrine d;

    GuardianSitRep s;
    BuildGuardianSitRep(s);
    GuardianReport r;
    d.Decide(s, r);

    VocationClass vc = VocationClassOf(s.pawnVocation);
    bool meleeOrHybrid = (vc == VCL_MELEE || vc == VCL_HYBRID);

    // 1. Поводок безопасности: пешка не атакует цели, если находится дальше leashDistance от Аризена
    const bool withinLeash = (r.pawnAnchorDist <= (d.leashDistance + d.hysteresisExit));

    // 2. Проактивный захват цели (Proactive Target Pinning)
    static uintptr_t s_lastTarget = 0;
    const uintptr_t pawn = Runtime::MainPawnBody();

    if (r.zoneEngaged && withinLeash && r.targetThreatBody && pawn) {
        // Направляем боевую цель планировщика (uCmc+0x2EB8) и фокус взгляда (+0x14E0) на угрозу в зоне
        Runtime::Mem::WrSafe((void*)(pawn + 0x2EB8), &r.targetThreatBody, sizeof(uintptr_t));
        Runtime::Mem::WrSafe((void*)(pawn + 0x14E0), &r.targetThreatBody, sizeof(uintptr_t));

        if (s_lastTarget != r.targetThreatBody) {
            s_lastTarget = r.targetThreatBody;
            char l[240];
            sprintf_s(l, "GuardianDoctrine: PROACTIVE TARGET -> %s 0x%08X (%s) dist %.1fm (pawn-Arisen %.1fm)",
                      r.criticalThreat ? "CRITICAL-MELEE" : "PREEMPT-INTERCEPT",
                      (unsigned)r.targetThreatBody, r.targetThreatKind ? r.targetThreatKind : "?",
                      r.nearestThreatDist, r.pawnAnchorDist);
            logFile << l << std::endl;
        }
    } else if (!r.zoneEngaged || r.threatsInZone == 0 || !withinLeash) {
        s_lastTarget = 0;
    }

    // 3. Динамический приоритет
    if (g_guardianFixEnabled) {
        int32_t desired = -3;
        if (r.zoneEngaged && withinLeash && meleeOrHybrid && r.threatsInZone > 0) {
            float dist = r.nearestThreatDist; // метры
            if (dist < g_guardianMeleeRadius)        desired = g_guardianDaggerBiasMelee;
            else if (dist < g_guardianPreemptRadius) desired = g_guardianDaggerBiasPreempt;
        }

        Runtime::GuardianFixSetTarget(desired);
        Runtime::GuardianFixTick();
    }

    // 4. Рычаг склонности — тот, у которого есть доказательство. Условие то
    // же самое, что у прежнего фикса: угроза в зоне и подходящая вокация.
    if (g_guardianUseInclLever) {
        const bool want = r.zoneEngaged && withinLeash && meleeOrHybrid && r.threatsInZone > 0;
        InclLeverApply(want);
    } else {
        GuardianLeverRestore();
    }
}

} // namespace PawnAI

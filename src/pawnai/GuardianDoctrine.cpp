#include "stdafx.h"
#include "runtime/Runtime.h"
#include "runtime/MemProbe.h"
#include "runtime/AggroWatch.h"
#include "runtime/MonsterTempo.h"
#include <math.h>
#include "GuardianDoctrine.h"
#include "DoctrineAnnounce.h"           // 85.63: окно тишины на объявление цели
#include "../runtime/LogMemSession.h"   // 85.63: сводка — в полевой пакет
#include "PawnPersona.h"
#include "OrderWatch.h"
#include "PartyRescueProtocol.h"
#include "../CombatBus.h"

extern BYTE** pBase; // из dinput8.cpp

namespace PawnAI {

using Runtime::Mem::Rd;
using Runtime::Mem::RdPtr;
using Runtime::Mem::WrSafe;

// 85.63: СЧЁТЧИКИ СЕССИИ. Лежат на уровне файла, а не в состоянии слота: слот
// сбрасывается при выходе из боя и при выгрузке мира, а сессия — нет. Нужны для
// сводки в полевом пакете: сколько раз объявляли цель и сколько объявлений
// свернули окном тишины — чтобы подавленный шум был виден числом, а не на слово.
static int s_probesStarted   = 0;
static int s_probesCompleted = 0;
static int s_targetAnnounces = 0;
static int s_targetMuted     = 0;

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
    float selectedDistance = 1e9f;
    uintptr_t nearestBody = 0;
    const char* nearestKind = nullptr;
    bool  isCritical = false;

    for (int i = 0; i < s.threatCount; ++i) {
        const GuardianThreat& t = s.threats[i];
        float d = Dist3(s.anchorX, s.anchorY, s.anchorZ, t.x, t.y, t.z);
        // Invalid entries must not become targets or hold the zone.
        if (!t.body || !(d >= 0.0f) || d > 1e9f) continue;
        if (d < nearest) nearest = d;

        const bool inCriticalPerimeter = (d <= meleeR);
        const bool inPreemptPerimeter  = (d <= enterR && t.targetingArisen);

        if (inCriticalPerimeter || inPreemptPerimeter) {
            ++inZone;
            const bool better = !nearestBody ||
                (inCriticalPerimeter != isCritical ? inCriticalPerimeter :
                 (d < selectedDistance || (d == selectedDistance && t.body < nearestBody)));
            if (better) {
                selectedDistance = d;
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
    // Hysteresis still uses nearest over all valid threats; telemetry uses
    // the selected candidate only (no candidate => sentinel).
    out.nearestThreatDist = nearestBody ? selectedDistance / scale : 1e9f;
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

// 85.63: СВОДКА ДЛЯ ПОЛЕВОГО ПАКЕТА. Зовётся из PawnAI_Shutdown — то есть до
// печати пакета в Unitialize(); счётчики к этому моменту уже не меняются.
// Строка отвечает на вопрос «что при этом осталось за кадром»: сколько замеров
// начато и не завершено (выгрузка мира, конец боя) и сколько объявлений цели
// свернуло окно тишины.
void GuardianSessionSummary()
{
    char l[220];
    sprintf_s(l, "GuardianDoctrine: session summary probes=%d completed=%d unpaired=%d"
                 " announces=%d muted=%d",
              s_probesStarted, s_probesCompleted,
              s_probesStarted - s_probesCompleted, s_targetAnnounces, s_targetMuted);
    LogMem::SessionNote(l);
}

// У ОДНОГО ПРАВИЛА ОДИН ХОЗЯИН.
//
// Приборы, которые сами пишут в строку code 54 (развёртка по вёдрам),
// обязаны сначала спросить, не занят ли рычаг доктриной. Спрашивают через
// эту функцию, а не через сам флаг: девтулзам незачем видеть переменные
// продуктового слоя.
bool GuardianDoctrineOwnsRule() { return g_guardianFixEnabled; }

// Bind only the selected actor. Never inherit MainPawn coordinates/weights.
// Kept separate so the production adapter can be exercised with a memory stub.
static bool BindGuardianActor(GuardianSitRep& s, uintptr_t body, int vocation,
                              float guardian, float nexus)
{
    s.pawnValid = false;
    s.pawnX = s.pawnY = s.pawnZ = 0.0f;
    s.pawnVocation = vocation;
    s.guardian = guardian;
    s.nexus = nexus;
    float x = 0, y = 0, z = 0;
    if (!body || !(guardian >= 350.f) || !(guardian <= 1000.f) ||
        !(nexus >= 0.f) || !(nexus <= 1000.f) || guardian < nexus)
        return false;
    if (!Rd((const void*)(body + 0x40), &x, 4) ||
        !Rd((const void*)(body + 0x44), &y, 4) ||
        !Rd((const void*)(body + 0x48), &z, 4)) return false;
    // Reject NaN/Inf and corrupt coordinate magnitudes without platform intrinsics.
    if (!(x >= -1e9f && x <= 1e9f) || !(y >= -1e9f && y <= 1e9f) ||
        !(z >= -1e9f && z <= 1e9f)) return false;
    s.pawnX = x; s.pawnY = y; s.pawnZ = z;
    s.pawnValid = true;
    return true;
}

// ============================================================================
// Роль пешки — из её собственного стека. Состояние — на КАЖДУЮ пешку.
// ============================================================================
//
// Раньше здесь был один статический `GuardianDoctrine` и один `s_lastTarget`
// на весь модуль, а носитель выбирался первым подходящим слотом с `break`.
// Из-за этого доктрина работала ровно на одной пешке — и, как показал лог
// 85.11, не на той: 27 событий ушли Страйдеру, у которого Guardian даже не
// входил в стек, а наёмный файтер с Guardian-первичной остался ни с чем.
//
// Теперь: состояние своё у каждого слота, гвардианов в партии может быть
// несколько, пешек между собой мы не сравниваем.
// docs/PAWN_ROLE_STACK.md.

// Карточка пешки: всё, что нужно, чтобы решить роль и написать её в лог.
struct PawnView {
    int            slot;
    uintptr_t      body;
    int            vocation;
    float          incl[9];
    int            rank[9];
    Persona::Kind  persona;
    int            personaRank;
    float          personaValue;
    bool           eligible;
};

struct GuardianProbe {
    uintptr_t body;
    DWORD sinceMs, nextMs;
    float pawnEnemyM, arisenEnemyM;
    // 85.63: СОСТОЯНИЕ НА СТАРТЕ. Смысл замера — «что изменилось за окно»,
    // значит нужны оба конца. Раньше старт печатался отдельной строкой, и его
    // приходилось сводить с результатом глазами через десятки чужих строк.
    int  code;
    char act[48];
};

struct GuardianSlotState {
    GuardianDoctrine doctrine;
    uintptr_t        lastTarget;
    DWORD            lastTargetSinceMs;
    DWORD            lastTelemetryMs;
    bool             tempoHeld;
    DWORD            wakeLastMs; // один сигнал, затем пауза; per body
    uintptr_t        wakeTarget;
    DWORD            wakeSinceMs;
    PawnView         view;       // кем оказалась пешка в последнем тике
    bool             viewValid;
    GuardianLiveCard live;       // снимок для панели, обновляется раз в секунду
    GuardianProbe probe[2];     // 0=WAKE, 1=INTERCEPT; independent windows
    AnnounceThrottle announce;  // 85.63: окно тишины на объявление цели
};

// Индекс = slot - PARTY_MAIN (0..2).
static GuardianSlotState s_slotState[3];

// Пороги роли. Ранг — основной фильтр: именно он решает, стреляют ли
// правила cmc.prt. Значение — вторичный.
int   g_guardianMinRank     = Persona::RANK_SECOND;  // ini [pawnAI] guardianMinRank
float g_guardianMinIncl     = 350.0f;                // ini [pawnAI] guardianMinIncl
// Телеметрия: как часто печатать строку состояния по каждому гвардиану.
DWORD g_guardianTelemetryMs = 0;                     // ini [pawnAI] guardianTelemetryMs (0 = тишина)
bool g_guardianProbeLog = true;                       // ini [pawnAI] guardianProbeLog

// Measures exactly the enemy signalled, not a different nearest enemy.
// Only WorldReport coordinates; never dereference an enemy address.
static void GuardianProbeStart(GuardianSlotState& st, int slot,
                               const GuardianSitRep& s, uintptr_t body,
                               uintptr_t enemy, int kind)
{
    if (!g_guardianProbeLog || kind < 0 || kind > 1) return;
    GuardianProbe& p = st.probe[kind];
    const DWORD now = MsNow();
    if (p.body || (p.nextMs && DWORD(now - p.nextMs) < 8000)) return;
    const float scale = st.doctrine.worldUnitsPerMeter > 0.0f
                      ? st.doctrine.worldUnitsPerMeter : 100.0f;
    for (int i = 0; i < s.threatCount; ++i) if (s.threats[i].body == enemy) {
        const GuardianThreat& t = s.threats[i];
        p.body = enemy;
        p.sinceMs = now;
        p.pawnEnemyM = Dist3(s.pawnX, s.pawnY, s.pawnZ, t.x, t.y, t.z) / scale;
        p.arisenEnemyM = Dist3(s.anchorX, s.anchorY, s.anchorZ, t.x, t.y, t.z) / scale;
        // 85.63: НА СТАРТЕ МОЛЧИМ — запоминаем состояние. Измерение печатает одну
        // строку на финише окна (см. GuardianProbeResult), с обоими концами:
        // было -> стало. В поле 85.60 три замера дали шесть строк, стоявших
        // врозь и вперемешку с объявлениями цели.
        Runtime::PawnPriorityCodeFor(body, &p.code);
        Runtime::ReadLiveAct(body, p.act, sizeof(p.act));
        ++s_probesStarted;
        break;
    }
}

static void GuardianProbeResult(GuardianSlotState& st, int slot,
                                const GuardianSitRep& s, uintptr_t body)
{
    for (int kind = 0; kind < 2; ++kind) {
        GuardianProbe& p = st.probe[kind];
        if (!p.body || DWORD(MsNow() - p.sinceMs) < 2500) continue;
        const GuardianThreat* t = 0;
        for (int i = 0; i < s.threatCount; ++i)
            if (s.threats[i].body == p.body) { t = &s.threats[i]; break; }
        const float scale = st.doctrine.worldUnitsPerMeter > 0.0f
                          ? st.doctrine.worldUnitsPerMeter : 100.0f;
        const float pe = t ? Dist3(s.pawnX,s.pawnY,s.pawnZ,t->x,t->y,t->z)/scale : -1.0f;
        const float ae = t ? Dist3(s.anchorX,s.anchorY,s.anchorZ,t->x,t->y,t->z)/scale : -1.0f;
        int32_t code = -1;
        Runtime::PawnPriorityCodeFor(body, &code);
        char act[48] = {};
        Runtime::ReadLiveAct(body, act, sizeof(act));
        uintptr_t current = 0;
        const bool readOk = RdPtr((void*)(body + 0x2EB8), &current);
        ++s_probesCompleted;
        // 85.63: ОДНА СТРОКА НА ЗАМЕР, оба конца в ней: было -> стало. Переход
        // `code 1->73 act Run->DmgDown` и есть результат наблюдения — раньше его
        // приходилось собирать из двух строк, стоящих в разных местах лога.
        char line[400];
        sprintf_s(line, "GuardianDoctrine: [%s] PROBE %s enemy 0x%08X %s P-E %.1f->%.1fm"
                        " A-E %.1f->%.1fm code %d->%d act %s->%s target %s0x%08X",
                  Runtime::PartyCombatSlotName(slot), kind ? "INTERCEPT" : "WAKE",
                  (unsigned)p.body, t ? "seen" : "gone",
                  p.pawnEnemyM, pe, p.arisenEnemyM, ae,
                  p.code, code, p.act[0] ? p.act : "?", act[0] ? act : "?",
                  readOk ? "" : "unreadable/", (unsigned)current);
        if (g_guardianProbeLog) logFile << line << std::endl;
        p.body = 0;
        p.nextMs = MsNow();
    }
}

static bool ReadPawnView(int slot, PawnView& v)
{
    memset(&v, 0, sizeof(v));
    v.slot     = slot;
    v.vocation = VOC_UNKNOWN;
    v.persona  = Persona::PERSONA_NONE;

    if (slot == Runtime::PARTY_MAIN) {
        v.body = Runtime::MainPawnBody();
        if (pBase && *pBase) {
            const uintptr_t record = (uintptr_t)(*pBase) + PLAYER_BASE + PAWN_OFFSET;
            v.vocation = ReadVocation(record);
        }
        // Свою пешку оцениваем по ПОЛЗУНКАМ ИГРОКА: импульсы («Вперёд!»
        // даёт Scather +300) не должны на шесть секунд менять её роль.
        float full[I_COUNT];
        if (PawnAIBaseInclinations(full)) {
            for (int i = 0; i < 9; ++i) v.incl[i] = full[i];
        } else {
            ReadAllIncl(full, 0);
            for (int i = 0; i < 9; ++i) v.incl[i] = full[i];
        }
    } else {
        int level = 0;
        if (!Runtime::PartyRecordInfo(slot - 1, &v.vocation, &level, &v.body) || !v.body)
            return false;
        float full[I_COUNT];
        ReadAllIncl(full, slot - 1);
        for (int i = 0; i < 9; ++i) v.incl[i] = full[i];
    }

    v.persona = Persona::Of(v.incl, &v.personaRank, &v.personaValue);
    Persona::RanksOf(v.incl, v.rank);
    v.eligible = (v.persona == Persona::PERSONA_GUARDIAN) &&
                 Persona::Eligible(v.personaRank, v.personaValue,
                                   g_guardianMinRank, g_guardianMinIncl);
    return v.body != 0;
}

// Full inclination evidence at body discovery and on material change.
// The main pawn's source is the saved player anchor. Hired pawns are read
// from their own LIVE record: this is the best available snapshot, NOT a
// proven persistent baseline. Do not describe it as "base" in the log.
struct StackLogState {
    uintptr_t body;
    int vocation;
    float incl[9];
    int rank[9];
    DWORD lastMs;
};
static StackLogState s_stackLog[3] = {};
static char s_roleSig[512] = {};

void GuardianStackLogReset()
{
    for (int i = 0; i < 3; ++i) s_stackLog[i] = StackLogState();
    s_roleSig[0] = 0;
}


static void LogPawnStackIfChanged(const PawnView* views, int n)
{
    bool seen[3] = {};
    for (int vi = 0; vi < n; ++vi) {
        const PawnView& v = views[vi];
        const int idx = v.slot - Runtime::PARTY_MAIN;
        if (idx < 0 || idx >= 3) continue;
        seen[idx] = true;
        StackLogState& prev = s_stackLog[idx];
        const bool newBody = prev.body != v.body || prev.vocation != v.vocation;
        bool rankChanged = false;
        float maxDelta = 0.0f;
        for (int j = 0; j < 9; ++j) {
            if (prev.rank[j] != v.rank[j]) rankChanged = true;
            float d = v.incl[j] - prev.incl[j];
            if (d < 0.0f) d = -d;
            if (d > maxDelta) maxDelta = d;
        }
        const DWORD now = MsNow();
        // Dragging main-pawn sliders is a meaningful experiment. Hired-pawn
        // live values may drift naturally, so do not log small fluctuations.
        const bool changed = rankChanged ||
                             (maxDelta >= (v.slot == Runtime::PARTY_MAIN ? 1.0f : 25.0f));
        if (!newBody && (!changed ||
            (prev.lastMs && DWORD(now - prev.lastMs) < 5000))) continue;
        prev.body = v.body;
        prev.vocation = v.vocation;
        prev.lastMs = now;
        memcpy(prev.incl, v.incl, sizeof(prev.incl));
        memcpy(prev.rank, v.rank, sizeof(prev.rank));
        int order[3] = {-1, -1, -1};
        for (int j = 0; j < 9; ++j)
            if (v.rank[j] >= Persona::RANK_THIRD)
                order[Persona::RANK_FIRST - v.rank[j]] = j;
        char line[768];
        sprintf_s(line,
                  "InclStack: [%s][%s] body 0x%08X source=%s reason=%s top=%s>%s>%s | Scather=%.0f Medicant=%.0f Mitigator=%.0f Challenger=%.0f Utilitarian=%.0f Guardian=%.0f Nexus=%.0f Pioneer=%.0f Acquisitor=%.0f",
                  Runtime::PartyCombatSlotName(v.slot), VocationName(v.vocation),
                  (unsigned)v.body,
                  v.slot == Runtime::PARTY_MAIN ? "anchor" : "hired-live",
                  newBody ? "discovered" : "changed",
                  order[0] >= 0 ? InclName(order[0]) : "?",
                  order[1] >= 0 ? InclName(order[1]) : "?",
                  order[2] >= 0 ? InclName(order[2]) : "?",
                  v.incl[I_SCATHER], v.incl[I_MEDICANT], v.incl[I_MITIGATOR],
                  v.incl[I_CHALLENGER], v.incl[I_UTILITARIAN],
                  v.incl[I_GUARDIAN], v.incl[I_NEXUS],
                  v.incl[I_PIONEER], v.incl[I_ACQUISITOR]);
        logFile << line << std::endl;
    }
    for (int i = 0; i < 3; ++i)
        if (!seen[i]) s_stackLog[i] = StackLogState();
}

// Один раз на смену состава партии печатаем, кто есть кто. Это и есть ответ
// на вопрос «кого мы вообще отслеживаем» — раньше его в логе не было вовсе.
static void LogRolesIfChanged(const PawnView* views, int n)
{
    char sig[512] = {};
    char line[1536] = {};
    size_t used = 0;

    for (int i = 0; i < n; ++i) {
        const PawnView& v = views[i];

        char part[256];
        sprintf_s(part, "%s %s %s r%d v%.0f | ",
                  Runtime::PartyCombatSlotName(v.slot),
                  VocationName(v.vocation),
                  Persona::Name(v.persona),
                  v.personaRank, v.personaValue);
        if (used + strlen(part) < sizeof(line) - 1) {
            memcpy(line + used, part, strlen(part));
            used += strlen(part);
            line[used] = 0;
        }

        // Ручное добавление вместо strcat_s: в портируемой проверке под g++
        // его нет, а сил на ещё один shim тратить нечего.
        char chunk[64];
        sprintf_s(chunk, "%d:%d:%.0f ", v.persona, v.personaRank, v.personaValue);
        const size_t have = strlen(sig), add = strlen(chunk);
        if (have + add < sizeof(sig) - 1)
            memcpy(sig + have, chunk, add + 1);
    }

    if (!strcmp(sig, s_roleSig)) return;          // состав и роли не менялись
    lstrcpynA(s_roleSig, sig, sizeof(s_roleSig));

    if (!line[0]) return;
    logFile << "Roles: " << line << std::endl;
}

// Ближайший враг к ПЕШКЕ и положение пешки относительно пары «Аризен—враг».
//
// ПОЧЕМУ ЭТО ЧИСЛО ДОБАВЛЕНО. В прежней строке `dist` означало Аризен→враг,
// а не пешка→враг. Из-за этого по логу было нельзя понять, в контакте пешка
// или стоит в арьергарде. `side` — знак скалярного произведения в плоскости
// XZ: > 0 пешка на стороне врага (перед игроком), < 0 — за спиной.
struct GuardianTelemetry {
    uintptr_t body;
    float     pawnEnemyDist;    // м
    float     anchorEnemyDist;  // м
    float     bearing;          // -1..1
};

static bool NearestThreatToPawn(const GuardianSitRep& s, float worldUnitsPerMeter, GuardianTelemetry& out)
{
    memset(&out, 0, sizeof(out));
    out.pawnEnemyDist   = 1e9f;
    out.anchorEnemyDist = 1e9f;
    if (!s.pawnValid || !s.anchorValid) return false;

    float best = 1e9f;
    for (int i = 0; i < s.threatCount; ++i) {
        const GuardianThreat& t = s.threats[i];
        if (!t.body) continue;
        const float d = Dist3(s.pawnX, s.pawnY, s.pawnZ, t.x, t.y, t.z);
        if (d >= best) continue;
        best = d;
        out.body            = t.body;
        const float scale = worldUnitsPerMeter > 0.0f ? worldUnitsPerMeter : 100.0f;
        out.pawnEnemyDist   = d / scale;
        out.anchorEnemyDist = Dist3(s.anchorX, s.anchorY, s.anchorZ, t.x, t.y, t.z) / scale;
    }
    if (!out.body) return false;

    // Перед / за Аризеном относительно выбранного врага. Скалярное
    // произведение в плоскости XZ (высоту не учитываем): > 0 пешка на
    // стороне врага, < 0 — за спиной Аризена. Нормировка на единицу, чтобы
    // число было одним и тем же на любой дистанции.
    float tvx = 0.0f, tvz = 0.0f;
    for (int i = 0; i < s.threatCount; ++i)
        if (s.threats[i].body == out.body) { tvx = s.threats[i].x; tvz = s.threats[i].z; break; }

    const float pvx = s.pawnX - s.anchorX, pvz = s.pawnZ - s.anchorZ;
    const float evx = tvx - s.anchorX,     evz = tvz - s.anchorZ;
    const float pn  = sqrtf(pvx * pvx + pvz * pvz);
    const float en  = sqrtf(evx * evx + evz * evz);
    if (pn > 1.0f && en > 1.0f)
        out.bearing = (pvx * evx + pvz * evz) / (pn * en);
    return true;
}

void GuardianDoctrineTick()
{
    GuardianSitRep s;
    BuildGuardianSitRep(s);   // общий якорь (Аризен) и список угроз

    // Карточки читаются ОДИН раз на тик и идут и в лог, и в доктрину, и в
    // панель. Читать стек склонностей дважды (сначала для строки Roles,
    // потом для доктрины) — лишняя работа каждые 150 мс без всякой пользы.
    PawnView views[3];
    int      nViews = 0;
    for (int slot = Runtime::PARTY_MAIN; slot <= Runtime::PARTY_HIRED2; ++slot) {
        PawnView v;
        if (ReadPawnView(slot, v)) views[nViews++] = v;
    }

    LogPawnStackIfChanged(views, nViews);
    LogRolesIfChanged(views, nViews);

    // То, что умеет только главная пешка: рычаг склонности и эррата code 54.
    // Считаем по ходу цикла, применяем после.
    bool  mainSeen    = false;
    bool  mainWant    = false;
    int32_t mainFixDesired = -3;

    for (int vi = 0; vi < nViews; ++vi) {
        const PawnView& v = views[vi];
        const int slot = v.slot;

        GuardianSlotState* st = 0;
        {
            const int idx = slot - Runtime::PARTY_MAIN;
            if (idx < 0 || idx >= 3) continue;
            st = &s_slotState[idx];
        }
        // Сменилась пешка в слоте (уволили, наняли, пересоздали тело):
        // гистерезис, цель и темп от прошлой нам больше не нужны.
        if (st->viewValid && st->view.body != v.body) {
            memset(&st->live, 0, sizeof(st->live));
            memset(st->probe, 0, sizeof(st->probe));
            st->lastTarget        = 0;
            st->lastTargetSinceMs = 0;
            st->tempoHeld         = false;
            st->wakeLastMs        = 0;
            st->wakeTarget        = 0;
            st->wakeSinceMs       = 0;
            st->doctrine          = GuardianDoctrine();   // гистерезис зоны сбросить
        }
        // Карточка пишется ДО проверки годности: панель обязана показать и
        // страйдера без гвардиана — иначе непонятно, почему доктрина молчит.
        st->view      = v;
        st->viewValid = true;

        if (!v.eligible) {
            // Не гвардиан: доктрина к ней не applies. Снимок гасим — иначе
            // панель показывала бы числа, посчитанные при другом стеке.
            memset(&st->live, 0, sizeof(st->live));
            memset(st->probe, 0, sizeof(st->probe));
            continue;
        }

        // Привязать ЭТУ пешку: её склонности, вокация, координаты.
        if (!BindGuardianActor(s, v.body, v.vocation,
                               v.incl[I_GUARDIAN], v.incl[I_NEXUS])) {
            memset(&st->live, 0, sizeof(st->live));
            memset(st->probe, 0, sizeof(st->probe));
            continue;   // тело или координаты недоступны — пробуем следующую
        }

        GuardianProbeResult(*st, slot, s, v.body);
        GuardianReport r;
        st->doctrine.Decide(s, r);

        const VocationClass vc = VocationClassOf(s.pawnVocation);
        const bool meleeOrHybrid = (vc == VCL_MELEE || vc == VCL_HYBRID);
        const bool withinLeash = (r.pawnAnchorDist <=
                                  (st->doctrine.leashDistance + st->doctrine.hysteresisExit));
        const bool want = r.zoneEngaged && withinLeash && r.threatsInZone > 0;

        if (want && r.targetThreatBody) {
            // Направляем боевую цель планировщика и фокус взгляда на угрозу.
            const bool targetWritten = Runtime::Mem::WrSafe((void*)(v.body + 0x2EB8), &r.targetThreatBody,
                                                              sizeof(uintptr_t));
            if (targetWritten)
                GuardianProbeStart(*st, slot, s, v.body, r.targetThreatBody, 1);
            Runtime::Mem::WrSafe((void*)(v.body + 0x14E0), &r.targetThreatBody,
                                 sizeof(uintptr_t));
            // Мощность по рангу: первичная — полный подгон, вторичная — мягче,
            // третичная — без темпа (docs/PAWN_ROLE_STACK.md §5.5).
            if (v.personaRank >= Persona::RANK_FIRST)
                Runtime::Tempo::SetOverride(v.body, 1.25f, 1.15f, 2500);
            else if (v.personaRank >= Persona::RANK_SECOND)
                Runtime::Tempo::SetOverride(v.body, 1.15f, 1.10f, 2500);
            st->tempoHeld = true;

            if (st->lastTarget != r.targetThreatBody) {
                st->lastTarget = r.targetThreatBody;
                st->lastTargetSinceMs = MsNow();
                // 85.63: КАЧЕЛИ МЕЖДУ ДВУМЯ ТЕЛАМИ БОЛЬШЕ НЕ ПЕЧАТАЮТСЯ. В поле
                // 85.60 из одиннадцати объявлений половина была про «ближе стал
                // другой гоблин» — решение не менялось, менялся нос. Сама цель
                // ставится как и раньше: молчит только строка, а число
                // свернутых строк уходит в сводку (muted=).
                if (st->announce.Allow(r.targetThreatBody, MsNow())) {
                    ++s_targetAnnounces;
                    char l[256];
                    sprintf_s(l, "GuardianDoctrine: [%s][%s][%s r%d v%.0f] PROACTIVE TARGET -> %s 0x%08X (%s) Arisen-enemy %.1fm (pawn-Arisen %.1fm)",
                              Runtime::PartyCombatSlotName(slot),
                              VocationName(v.vocation),
                              Persona::Name(v.persona), v.personaRank, v.personaValue,
                              r.criticalThreat ? "CRITICAL-MELEE" : "PREEMPT-INTERCEPT",
                              (unsigned)r.targetThreatBody,
                              r.targetThreatKind ? r.targetThreatKind : "?",
                              r.nearestThreatDist, r.pawnAnchorDist);
                    logFile << l << std::endl;
                } else {
                    ++s_targetMuted;
                }
            }
        } else if (!r.zoneEngaged || r.threatsInZone == 0 || !withinLeash) {
            if (st->tempoHeld) Runtime::Tempo::ClearOverride(v.body);
            st->tempoHeld = false;
            st->lastTarget = 0;
        }

        // Будильник: НЕ срочная зона, а потенциальная угроза в 8..25 м
        // от Аризена. Только пассивная пешка (Wait/Follow), одна запись и
        // пауза. Ни темпа, ни команд FSM, ни изменения чужих инклинаций.
        // Ближний перехват выше по приоритету и исполняется до этого блока.
        {
            const DWORD wakeNow = MsNow();
            const bool manualOrder = OrderWatch::GetStats().orderRemainingMs != 0;
            const bool rescue = Rescue::IsActive();
            int32_t wakeCode = -1;
            const bool passive = Runtime::PawnPriorityCodeFor(v.body, &wakeCode) &&
                                 (wakeCode == 0 || wakeCode == 1);
            // Наш пин не имеет TTL в движке. Возвращаем только собственную
            // запись, только при пассивном планировщике и без чужого приказа.
            if (st->wakeTarget && wakeNow - st->wakeSinceMs >= 3000) {
                uintptr_t current = 0;
                const bool readOk = RdPtr((void*)(v.body + 0x2EB8), &current);
                // Только если всё ещё Follow/Wait и цель наша: не стираем
                // решение игрового ИИ, ручной приказ или срочный перехват.
                bool released = false;
                if (!manualOrder && !rescue && !want && passive && readOk &&
                    current == st->wakeTarget) {
                    const uintptr_t empty = 0;
                    released = WrSafe((void*)(v.body + 0x2EB8), &empty, sizeof(empty));
                }
                char result[200];
                sprintf_s(result, "GuardianDoctrine: [%s] WAKE RESULT code %d target 0x%08X released %d",
                          Runtime::PartyCombatSlotName(slot), wakeCode,
                          (unsigned)current, released ? 1 : 0);
                logFile << result << std::endl;
                st->wakeTarget = 0;
            }
            if (!want && !manualOrder && !rescue && passive &&
                r.pawnAnchorDist <= st->doctrine.leashDistance &&
                (!st->wakeLastMs || wakeNow - st->wakeLastMs >= 8000)) {
                // Не отбираем цель, которую уже выбрала игра или другой модуль.
                uintptr_t current = 0;
                if (RdPtr((void*)(v.body + 0x2EB8), &current) && !current) {
                    const float scale = st->doctrine.worldUnitsPerMeter > 0.0f
                                      ? st->doctrine.worldUnitsPerMeter : 100.0f;
                    uintptr_t candidate = 0;
                    float best = 25.0f;
                    const char* kind = "?";
                    if (s.anchorValid) for (int ti = 0; ti < s.threatCount; ++ti) {
                        const GuardianThreat& t = s.threats[ti];
                        if (!t.body) continue;
                        const float d = Dist3(s.anchorX, s.anchorY, s.anchorZ,
                                              t.x, t.y, t.z) / scale;
                        if (!(d >= 8.0f && d < best)) continue;
                        best = d;
                        candidate = t.body;
                        kind = t.kind ? t.kind : "?";
                    }
                    if (candidate && WrSafe((void*)(v.body + 0x2EB8),
                                             &candidate, sizeof(candidate))) {
                        st->wakeLastMs = wakeNow;
                        st->wakeSinceMs = wakeNow;
                        st->wakeTarget = candidate;
                        GuardianProbeStart(*st, slot, s, v.body, candidate, 0);
                        char msg[256];
                        sprintf_s(msg, "GuardianDoctrine: [%s] WAKE target 0x%08X (%s) Arisen-enemy %.1fm pawn-Arisen %.1fm code %d cooldown 8s",
                                  Runtime::PartyCombatSlotName(slot),
                                  (unsigned)candidate, kind, best,
                                  r.pawnAnchorDist, wakeCode);
                        logFile << msg << std::endl;
                    }
                }
            }
        }

        // --- телеметрия по этой пешке -------------------------------------
        // Снимок обновляется ВСЕГДА, раз в секунду: его читает панель. В лог
        // строка уходит только если телеметрия включена (guardianTelemetryMs).
        const DWORD now = MsNow();
        const DWORD teleMs = g_guardianTelemetryMs ? g_guardianTelemetryMs : 1000;
        if (now - st->lastTelemetryMs >= teleMs) {
            st->lastTelemetryMs = now;

            GuardianTelemetry tm;
            NearestThreatToPawn(s, st->doctrine.worldUnitsPerMeter, tm);

            GuardianLiveCard& lc = st->live;
            memset(&lc, 0, sizeof(lc));
            lc.valid           = true;
            lc.pawnAnchorDist  = r.pawnAnchorDist;
            lc.pawnEnemyDist   = tm.pawnEnemyDist;
            lc.anchorEnemyDist = tm.anchorEnemyDist;
            lc.bearing         = tm.bearing;
            lc.zoneEngaged     = r.zoneEngaged ? 1 : 0;
            lc.threatsInZone   = r.threatsInZone;
            lc.target          = st->lastTarget;
            lc.code            = -1;

            Runtime::ReadLiveAct(v.body, lc.act, sizeof(lc.act));

            int32_t code = -1;
            char goal[32] = {};
            if (Runtime::PawnPriorityCodeFor(v.body, &code) && code >= 0)
                Runtime::PawnGoalNameFor(v.body, code, goal, sizeof(goal));
            lc.code = code;
            lstrcpynA(lc.goal, goal, sizeof(lc.goal));

            if (g_guardianTelemetryMs) {
                char side[16] = "n/a";
                if (tm.body) lstrcpynA(side, tm.bearing >= 0.0f ? "FRONT" : "BACK", sizeof(side));

                char l[320];
                sprintf_s(l, "GuardianDoctrine: [%s][%s][%s r%d] pawn-Arisen %.1fm pawn-enemy %.1fm Arisen-enemy %.1fm side %s | tgt 0x%08X | act %s | code %d \"%s\" | zone %d threats %d",
                          Runtime::PartyCombatSlotName(slot),
                          VocationName(v.vocation),
                          Persona::Name(v.persona), v.personaRank,
                          r.pawnAnchorDist, tm.pawnEnemyDist, tm.anchorEnemyDist,
                          side, (unsigned)st->lastTarget,
                          lc.act[0] ? lc.act : "?", lc.code, lc.goal[0] ? lc.goal : "?",
                          r.zoneEngaged ? 1 : 0, r.threatsInZone);
                logFile << l << std::endl;
            }
        }

        // --- что остаётся только главной пешке -----------------------------
        if (slot == Runtime::PARTY_MAIN) {
            mainSeen = true;
            mainWant = want && meleeOrHybrid;
            mainFixDesired = -3;
            if (mainWant) {
                if (r.nearestThreatDist < g_guardianMeleeRadius)
                    mainFixDesired = g_guardianDaggerBiasMelee;
                else if (r.nearestThreatDist < g_guardianPreemptRadius)
                    mainFixDesired = g_guardianDaggerBiasPreempt;
            }
        }
    }

    // 3. Динамический приоритет (code 54) — только главная пешка.
    if (g_guardianFixEnabled) {
        Runtime::GuardianFixSetTarget(mainSeen ? mainFixDesired : -3);
        Runtime::GuardianFixTick();
    }

    // 4. Рычаг склонности — только главная пешка. Чужой билд не трогаем
    //    (HIRED_PAWNS_SCOPE.md): наёмный гвардиан получает пин и темп, но не
    //    правку character record.
    if (g_guardianUseInclLever) {
        if (mainSeen) InclLeverApply(mainWant);
        else          GuardianLeverRestore();
    } else {
        GuardianLeverRestore();
    }
}

// ============================================================================
// Снимок для панели
// ============================================================================
// Никаких пересчётов: панель читает то, что уже посчитал тик. Если тик не
// ходил (выгрузка, доктрина выключена), вернём valid = false, а панель
// напишет «—» вместо выдуманных нулей.

bool GuardianPawnCard(int slot, PawnRoleCard* roleOut, GuardianLiveCard* liveOut)
{
    const int idx = slot - Runtime::PARTY_MAIN;
    if (idx < 0 || idx >= 3) return false;
    const GuardianSlotState& st = s_slotState[idx];

    if (roleOut) {
        memset(roleOut, 0, sizeof(*roleOut));
        roleOut->slot = slot;
        roleOut->valid = st.viewValid;
        if (st.viewValid) {
            roleOut->vocation     = st.view.vocation;
            roleOut->body         = st.view.body;
            roleOut->guardianIncl = st.view.incl[I_GUARDIAN];
            roleOut->nexusIncl    = st.view.incl[I_NEXUS];
            roleOut->persona      = (int)st.view.persona;
            roleOut->personaRank  = st.view.personaRank;
            roleOut->personaValue = st.view.personaValue;
            roleOut->eligible     = st.view.eligible;
        }
    }
    if (liveOut) {
        if (st.live.valid) *liveOut = st.live;
        else               memset(liveOut, 0, sizeof(*liveOut));
    }
    return true;
}

const char* GuardianPersonaName(int persona)
{
    // Имена латиницей: у ImGui здесь шрифт по умолчанию (Basic Latin),
    // кириллица в панели не отрисуется. В лог пишутся русские комментарии,
    // а не эти строки.
    switch (persona) {
    case (int)Persona::PERSONA_GUARDIAN: return "Guardian";
    case (int)Persona::PERSONA_NEXUS:    return "Nexus";
    default:                             return "-";
    }
}

} // namespace PawnAI

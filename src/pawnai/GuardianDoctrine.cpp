#include "stdafx.h"
#include "runtime/Runtime.h"
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
struct GuardianModifier {
    uint32_t     code;
    int32_t      addS32;         // штатное смещение (штраф/бонус) от Capcom
    const char*  intentKey;      // семантическое имя (nullptr, если не mapped)
    const char*  status;         // "CONFIRMED" / "HYPOTHESIS"
    const char*  note;
};
static const GuardianModifier kGuardianModifiers[] = {
    {  4, +3, nullptr, "HYPOTHESIS", "Guardian wait/follow response (semantic TBD)" },
    { 13, -2, nullptr, "HYPOTHESIS", "party relation (shared Guardian/Nexus)"       },
    { 15, -2, "Air",  "CONFIRMED",  "Air - shared Guardian/Nexus"                  },
    { 54, -3, "WpnDaggerAtk", "CONFIRMED", "offensive dagger attack - MAIN A/B lever" },
    { 60, -3, "Em0600Cover",  "CONFIRMED",  "enemy-specific cover (not touched yet)" },
    { 66, -4, nullptr, "HYPOTHESIS", "battle response (semantic TBD)"              },
};
static const int kGuardianModifierCount = sizeof(kGuardianModifiers) / sizeof(kGuardianModifiers[0]);

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

// ================= Доктрина =================

void GuardianDoctrine::Init()
{
    ResetState();
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

    // --- 1) угрозы в зоне ответственности вокруг anchor ---
    // Радиус входа чуть шире (hysteresisEnter), если зона ещё не захвачена, —
    // чтобы пешка «защёлкнулась» на угрозе и не дребезжала на границе.
    // Build 56.3: радиусы в МЕТРАХ → мировые единицы (* worldUnitsPerMeter).
    float scale  = worldUnitsPerMeter > 0.0f ? worldUnitsPerMeter : 100.0f;
    float enterR = (protectionRadius + (zoneEngaged ? 0.0f : hysteresisEnter)) * scale;
    float exitR  = (protectionRadius + hysteresisExit) * scale;
    int   inZone = 0;
    float nearest = 1e9f;
    for (int i = 0; i < s.threatCount; ++i) {
        const GuardianThreat& t = s.threats[i];
        float d = Dist3(s.anchorX, s.anchorY, s.anchorZ, t.x, t.y, t.z);
        if (d < nearest) nearest = d;
        if (d <= enterR) ++inZone;
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
        t.x = w.units[i].x;
        t.y = w.units[i].y;
        t.z = w.units[i].z;
        t.kind = w.units[i].kind;
        t.engaged = false;
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
int32_t g_guardianDaggerBiasMelee = 2;   // бонус в melee-радиусе (58)
int32_t g_guardianDaggerBiasPreempt = 0; // снятие штрафа в preempt-радиусе (58)

void GuardianDoctrineTick()
{
    static GuardianDoctrine d;
    Runtime::GuardianFixSetTarget(-3); // vanilla по умолчанию
    if (!g_guardianFixEnabled) {
        Runtime::GuardianFixTick();
        return;
    }

    GuardianSitRep s;
    BuildGuardianSitRep(s);
    GuardianReport r;
    d.Decide(s, r);

    VocationClass vc = VocationClassOf(s.pawnVocation);
    bool meleeOrHybrid = (vc == VCL_MELEE || vc == VCL_HYBRID);

    // Build 58: градиент по дистанции ближайшей угрозы (Threat Anchor ≠ Movement Anchor).
    //   вне зоны            → vanilla (-3), пешка свободна (лук и т.д.);
    //   в preempt-радиусе   → снять штраф (0), пешка «готовится» (кинжалы в руке);
    //   в melee-радиусе     → лёгкий бонус (+2), пешка фиксируется на перехвате.
    int32_t desired = -3;
    if (r.zoneEngaged && meleeOrHybrid && r.threatsInZone > 0) {
        float dist = r.nearestThreatDist; // метры
        if (dist < g_guardianMeleeRadius)      desired = g_guardianDaggerBiasMelee;
        else if (dist < g_guardianPreemptRadius) desired = g_guardianDaggerBiasPreempt;
    }

    Runtime::GuardianFixSetTarget(desired);
    Runtime::GuardianFixTick();
}

} // namespace PawnAI

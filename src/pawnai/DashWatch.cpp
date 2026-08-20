// PawnAI::DashWatch — наблюдатель за рывками пешки. См. DashWatch.h.

#include "stdafx.h"
#include "DashWatch.h"
#include "../CombatBus.h"
#include "../runtime/Runtime.h"
#include "../runtime/MonsterTempo.h"   // счётчик хука спринта — второй прибор

namespace PawnAI {
namespace DashWatch {

static Stats s_st = {};
static char  s_prevAct[48] = {};
static int   s_logged = 0;

// ПРОБА ВЫНОСЛИВОСТИ УБРАНА (вопрос закрыт, см. DashWatch.h).
// Живого cPlStamina в теле пешки нет и не будет: счётчик лежит в записи
// персонажа. Проба печатала «not found» каждый сеанс и заставляла заново
// обсуждать решённое. Правило проекта: вопрос -> ответ -> документ ->
// удалить инструмент.

// Рывок ли это состояние. Имена подтверждены атласом типов:
// cPlActDash (116 B), cPlActDashBegin (120 B), cPlActDashJump,
// cPlActDashJumpBegin, cPlActDashJumpLand.
static bool IsDashAct(const char* a)
{
    return a && a[0] && (strstr(a, "ActDash") != 0);
}

void Init()
{
    Reset();
    logFile << "DashWatch: watching pawn dash states (cPlActDash*)" << std::endl;
}

void Shutdown() {}

void Reset()
{
    memset(&s_st, 0, sizeof(s_st));
    s_prevAct[0] = 0;
    s_logged = 0;
}

Stats Get() { return s_st; }

void Tick()
{
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!pawn) return;

    char act[48] = {};
    if (!Runtime::ReadLiveAct(pawn, act, sizeof(act)) || !act[0]) return;

    ++s_st.samples;
    lstrcpynA(s_st.lastAct, act, sizeof(s_st.lastAct));

    // Считаем ПЕРЕХОД в состояние рывка, а не каждый тик внутри него:
    // иначе длинный рывок насчитал бы десяток «рывков».
    const bool wasDash = IsDashAct(s_prevAct);
    const bool isDash  = IsDashAct(act);
    lstrcpynA(s_prevAct, act, sizeof(s_prevAct));
    if (!isDash || wasDash) return;

    // Контекст: как далеко ближайший враг от ПЕШКИ и что говорят
    // сигналы боя.
    const WorldReport w = CombatBus::Instance().LastWorld();
    float px = 0, py = 0, pz = 0;
    float best = -1.0f;
    if (Runtime::GetMainPawnWorldPos(&px, &py, &pz)) {
        float b = 1.0e9f;
        for (int i = 0; i < w.count; ++i) {
            const WorldPresence& u = w.units[i];
            if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
            const float dx = u.x - px, dy = u.y - py, dz = u.z - pz;
            const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
            if (d < b) b = d;
        }
        if (b < 1.0e8f) best = b;
    }

    // ЗДЕСЬ БЫЛА ОШИБКА ЗАМЕРА.
    //
    // Сначала «бой» определялся как `w.enemyCount > 0` — то есть «в списке
    // актёров есть хоть один враг». Но список набивается сканом на сотни
    // метров вокруг: стоило один раз увидеть лагерь, и любой рывок пешки
    // при спокойном следовании засчитывался как боевой. Тестер увидел
    // `in combat 6 | out of combat 0` там, где всё было ровно наоборот.
    //
    // Теперь бой — это ответ продуктового детектора (урон, боевые действия
    // врагов, выбранная цель пешки) И близкий враг. Далёкая драка на
    // соседнем холме боем для ПЕШКИ не является.
    const CombatReport r = CombatBus::Instance().LastReport();
    const bool signalled = r.inCombat || (w.enemyCombatCount > 0) || w.pawnEngaged;
    const bool nearby    = (best >= 0.0f && best <= 25.0f);
    const bool inCombat  = signalled && nearby;

    if (inCombat) ++s_st.inCombat; else ++s_st.outOfCombat;
    s_st.lastDistM = best;
    lstrcpynA(s_st.lastDash, act, sizeof(s_st.lastDash));

    // СШИВКА ПРИБОРОВ. Замер 73.27 показал ноль выборов кода 84/85 и при
    // этом живые рывки, в том числе в бою. Пока код приоритета и состояние
    // FSM снимались разными кнопками в разное время, это оставалось
    // загадкой. Теперь оба числа берутся в ОДИН момент — момент рывка.
    int32_t code = -1;
    const bool codeOk = Runtime::PawnPriorityCode(&code);
    char goal[32] = {};
    if (codeOk && code >= 0) Runtime::PawnGoalName(code, goal, sizeof(goal));

    s_st.lastCode = codeOk ? code : -1;
    lstrcpynA(s_st.lastGoal, goal[0] ? goal : "?", sizeof(s_st.lastGoal));
    if (!codeOk || code < 0)            ++s_st.dashCodeUnknown;
    else if (code == 84 || code == 85)  ++s_st.dashUnderDash;
    else if (code == 1)                 ++s_st.dashUnderFollow;
    else                                ++s_st.dashUnderOther;

    // ЧТО ИМЕННО ВШИТО В ПЛАН В ЭТУ СЕКУНДУ. Раз все рывки приходят под
    // кодом 1, интересна не цель (она известна), а моторный интерфейс
    // внутри неё: cCmcFollow или cCmcDashFollow. Это и есть искомая
    // развилка, и снимать её надо ровно в момент рывка.
    char ifaces[96] = {};
    if (codeOk && code >= 0) Runtime::PawnPlanInterfaces(code, ifaces, sizeof(ifaces));

    // Третий прибор рядом: сколько тел прошло через хук спринтового
    // перемещения. Если состояние рывка входит, а счётчик пешек стоит —
    // значит движение ускоряет не он, и копать надо в другом месте.
    const Runtime::Tempo::SprintStats sp = Runtime::Tempo::GetSprintStats();

    // Первые события — в лог с контекстом. Дальше только счётчики:
    // рывков за вечер могут быть сотни, лог не резиновый.
    if (s_logged < 20) {
        ++s_logged;
        char l[420];
        sprintf_s(l, "DashWatch: %s | %s | nearest enemy %.1f m | priority code %d \"%s\""
                     " | plan interfaces: %s"
                     " | signals: detector %d, enemyActs %d, pawnTarget %d"
                     " | sprint hook: player %u pawn %u enemy %u",
                  act, inCombat ? "IN COMBAT" : "out of combat", best,
                  codeOk ? code : -1, goal[0] ? goal : "?",
                  ifaces[0] ? ifaces : "(none found)",
                  r.inCombat ? 1 : 0, w.enemyCombatCount, w.pawnEngaged ? 1 : 0,
                  sp.player, sp.pawn, sp.enemy);
        logFile << l << std::endl;
    }
}

} // namespace DashWatch
} // namespace PawnAI

// PawnAI::DashWatch — наблюдатель за рывками пешки. См. DashWatch.h.

#include "stdafx.h"
#include "DashWatch.h"
#include "../CombatBus.h"
#include "../runtime/Runtime.h"

namespace PawnAI {
namespace DashWatch {

static Stats s_st = {};
static char  s_prevAct[48] = {};
static int   s_logged = 0;

// Стамина пешки: объект cPlStamina (16 B) ищется в теле по ИМЕНИ класса
// один раз и запоминается смещением. Повод — догадка тестера: рывок в
// данных бывает в двух вариантах, обычный и `St500`, то есть у него есть
// порог выносливости. Если пешка почти не дашит даже вне боя, стоит
// посмотреть, что у неё с этим числом.
static uintptr_t s_pawnForStamina = 0;
static uint32_t  s_staminaOff = 0;
static bool      s_staminaSearched = false;

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

// Первые 16 байт cPlStamina как четыре числа. Что там именно — пока
// неизвестно, поэтому печатаем и float, и int: одно из них окажется
// текущей выносливостью.
static void StaminaText(uintptr_t pawn, char* out, int cap)
{
    out[0] = 0;
    if (!pawn) return;
    if (pawn != s_pawnForStamina) {   // тело сменилось — искать заново
        s_pawnForStamina = pawn;
        s_staminaSearched = false;
        s_staminaOff = 0;
    }
    if (!s_staminaSearched) {
        s_staminaSearched = true;
        uintptr_t obj = Runtime::FindChildByClass(pawn, 0x5A10, "cPlStamina", &s_staminaOff);
        if (obj) {
            char l[120];
            sprintf_s(l, "DashWatch: cPlStamina found at pawn +0x%04X", (unsigned)s_staminaOff);
            logFile << l << std::endl;
        } else {
            logFile << "DashWatch: cPlStamina not found in the pawn body" << std::endl;
        }
    }
    if (!s_staminaOff) return;

    uintptr_t obj = 0;
    if (!Runtime::ReadPtrSafe(pawn + s_staminaOff, &obj) || !obj) return;
    float f[4] = {};
    if (!Runtime::ReadSafe(obj, f, sizeof(f))) return;
    int32_t iv[4];
    memcpy(iv, f, sizeof(iv));
    sprintf_s(out, cap, " | stamina obj: %.1f %.1f %.1f %.1f (ints %d %d %d %d)",
              f[0], f[1], f[2], f[3], iv[0], iv[1], iv[2], iv[3]);
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

    // Первые события — в лог с контекстом. Дальше только счётчики:
    // рывков за вечер могут быть сотни, лог не резиновый.
    if (s_logged < 20) {
        ++s_logged;
        char st[160] = {};
        StaminaText(pawn, st, sizeof(st));
        char l[400];
        sprintf_s(l, "DashWatch: %s | %s | nearest enemy %.1f m | signals: detector %d,"
                     " enemyActs %d, pawnTarget %d%s",
                  act, inCombat ? "IN COMBAT" : "out of combat", best,
                  r.inCombat ? 1 : 0, w.enemyCombatCount, w.pawnEngaged ? 1 : 0, st);
        logFile << l << std::endl;
    }
}

} // namespace DashWatch
} // namespace PawnAI

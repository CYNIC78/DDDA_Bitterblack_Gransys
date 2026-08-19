// MonsterAI::Director — режиссёр стороны монстров. См. MonsterDirector.h.

#include "stdafx.h"
#include "MonsterDirector.h"
#include "../CombatBus.h"
#include "../runtime/Runtime.h"
#include "../runtime/MonsterTempo.h"
#include "../ActMap.Generated.h"

namespace MonsterAI {

static const int kMaxView = 32;

static bool        s_enabled = false;
static MonsterView s_view[kMaxView];
static int         s_nView = 0;
static char        s_status[192] = "monster director: off";
static DWORD       s_lastTick = 0;

// Когда особь впервые попала в поле зрения. Нужно будущим политикам:
// «бой идёт дольше N секунд» — это про длительность, а не про кадр.
struct FirstSeen { uintptr_t body; DWORD ms; };
static FirstSeen s_seen[kMaxView];
static int       s_nSeen = 0;

static DWORD FirstSeenMs(uintptr_t body, DWORD now)
{
    for (int i = 0; i < s_nSeen; ++i)
        if (s_seen[i].body == body) return s_seen[i].ms;
    if (s_nSeen < kMaxView) {
        s_seen[s_nSeen].body = body;
        s_seen[s_nSeen].ms = now;
        ++s_nSeen;
    }
    return now;
}

// Забываем тех, кого больше нет: иначе таблица «когда впервые увидели»
// со временем начнёт отвечать про давно умерших.
static void ForgetMissing()
{
    int w = 0;
    for (int i = 0; i < s_nSeen; ++i) {
        bool alive = false;
        for (int k = 0; k < s_nView; ++k)
            if (s_view[k].body == s_seen[i].body) { alive = true; break; }
        if (!alive) continue;
        if (w != i) s_seen[w] = s_seen[i];
        ++w;
    }
    s_nSeen = w;
}

void Init()
{
    s_enabled = config.getBool("monsterAI", "enabled", false);
    s_nView = 0;
    s_nSeen = 0;
    lstrcpynA(s_status, s_enabled ? "monster director: watching"
                                  : "monster director: off", sizeof(s_status));
    logFile << "MonsterAI: director " << (s_enabled ? "enabled" : "disabled")
            << " (observer only, no policies yet)" << std::endl;
}

void Shutdown()
{
    s_nView = 0;
    s_nSeen = 0;
}

bool Enabled() { return s_enabled; }

void SetEnabled(bool on)
{
    if (s_enabled == on) return;
    s_enabled = on;
    if (!on) {
        // Режиссёр уходит — снимаем всё, что он успел назначить. Иначе
        // монстры остались бы с его коэффициентами без хозяина.
        Runtime::Tempo::ClearAllOverrides();
        s_nView = 0;
        lstrcpynA(s_status, "monster director: off", sizeof(s_status));
    } else {
        lstrcpynA(s_status, "monster director: watching", sizeof(s_status));
    }
    logFile << "MonsterAI: director " << (on ? "enabled" : "disabled") << std::endl;
}

// ---------------------------------------------------------------------------
// Сбор картины боя.
//
// Источники только общие: список актёров из скана мира и шина. Своих
// чтений по «плавающим» оффсетам здесь нет и быть не должно — слот
// +0x2B98 у гоблина держит uPlayer в покое и uCmc в агро, и любой
// захардкоженный оффсет однажды попадёт не в тот объект (см. EnemyTuner.h).
// ---------------------------------------------------------------------------
static void BuildView()
{
    const DWORD now = MsNow();
    float ax = 0, ay = 0, az = 0;
    const bool haveArisen = Runtime::GetArisenWorldPos(&ax, &ay, &az);

    // ЧИТАЕМ ШИНУ, А НЕ ВНУТРЕННОСТИ СКАНА.
    //
    // Это не формальность. Пешки уже живут на этих же данных; если
    // режиссёр монстров пойдёт своей дорогой в g_act, у двух сторон
    // разойдётся картина мира — и разбирать «почему пешка видит трёх, а
    // режиссёр четырёх» придётся в бою, а не в коде.
    const WorldReport w = CombatBus::Instance().LastWorld();

    s_nView = 0;
    for (int i = 0; i < w.count && s_nView < kMaxView; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr) continue;
        if (!u.kind || !Runtime::KindIsEnemy(u.kind)) continue;

        MonsterView& v = s_view[s_nView];
        memset(&v, 0, sizeof(v));
        v.body = u.ptr;
        v.dead = false;                 // трупы в units[] не попадают
        lstrcpynA(v.kind, u.kind, sizeof(v.kind));
        lstrcpynA(v.act, u.actName, sizeof(v.act));

        // Тот же источник истины, что у примитива темпа: таблица действий.
        // Две стороны не должны по-разному отвечать на вопрос «это атака?».
        v.attacking = ActMap::NameIsAttack(v.act);

        v.distM = -1.0f;
        if (haveArisen) {
            const float dx = u.x - ax, dy = u.y - ay, dz = u.z - az;
            v.distM = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
        }

        v.locoFactor = v.atkFactor = 1.0f;
        Runtime::Tempo::GetFactors(u.ptr, &v.locoFactor, &v.atkFactor);
        v.seenMs = (uint32_t)FirstSeenMs(u.ptr, now);

        ++s_nView;
    }
    ForgetMissing();
}

// ---------------------------------------------------------------------------
// ЗДЕСЬ БУДУТ ПОЛИТИКИ.
//
// Каждая политика — отдельная функция с одним вопросом и одним ответом
// через примитив. Пример будущей «эскалации»:
//
//     static void PolicyEscalation(const MonsterView& v, DWORD now)
//     {
//         if (v.dead) return;
//         const DWORD fighting = now - v.seenMs;
//         if (fighting < 12000) return;              // бой ещё молодой
//         if (v.distM > 15.0f) return;               // не про нас
//         Runtime::Tempo::SetOverride(v.body, 1.05f, 1.30f, 5000);
//     }
//
// Правила для любой политики:
//   1. Решение принимается по картине боя, а не по одному кадру.
//   2. Выход — только через примитив, с ttl. Бессрочные переопределения
//      оставляем состояниям («ранен»), а не всплескам.
//   3. Всплеск обязан быть заметен игроку: если правила боя изменились,
//      игрок должен это ПОНЯТЬ, иначе он решит, что его просто убили.
//   4. Никакой политики без отката.
//
// Пока функция пуста намеренно: сначала убеждаемся, что картина боя
// собирается верно.
// ---------------------------------------------------------------------------
static void ApplyPolicies()
{
    // Политик нет. Режиссёр наблюдает.
}

void Tick()
{
    if (!s_enabled) return;

    // Своя частота: 150 мс достаточно для решений уровня «характер боя».
    // Покадровая работа — дело примитивов, не режиссёра.
    const DWORD now = MsNow();
    if (s_lastTick && now - s_lastTick < 150) return;
    s_lastTick = now;

    // Проверка «мы в игре, а не в меню» уже сделана вызывающим
    // (UpdatePawnAI -> IsInActiveGameplay), второй раз лезть в память
    // незачем. Шина при выгрузке мира просто перестаёт обновляться, а
    // BuildView отфильтрует пустой отчёт сам.

    BuildView();
    ApplyPolicies();

    const WorldReport w = CombatBus::Instance().LastWorld();
    int attacking = 0;
    for (int i = 0; i < s_nView; ++i) if (s_view[i].attacking) ++attacking;
    sprintf_s(s_status, "monster director: %d enemies, %d attacking, bus sees %d",
              s_nView, attacking, w.enemyCount);
}

int ViewCount() { return s_nView; }

const MonsterView* ViewAt(int i)
{
    return (i >= 0 && i < s_nView) ? &s_view[i] : 0;
}

const char* Status() { return s_status; }

void DumpSnapshot()
{
    const WorldReport w = CombatBus::Instance().LastWorld();
    const CombatReport r = CombatBus::Instance().LastReport();
    logFile << "MonsterAI: === director snapshot ===" << std::endl;
    {
        char l[200];
        sprintf_s(l, "  bus: world enemies %d (combat %d, dead %d), hits: player %d"
                     " pawn %d, inCombat %d",
                  w.enemyCount, w.enemyCombatCount, w.deadCount,
                  r.playerHits, r.pawnHits, r.inCombat ? 1 : 0);
        logFile << l << std::endl;
    }
    const DWORD now = MsNow();
    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        char l[240];
        sprintf_s(l, "  0x%08X %-8s %-28s %s dist %5.1f m  loco x%.2f atk x%.2f"
                     "  seen %.1f s",
                  (unsigned)v.body, v.kind, v.act[0] ? v.act : "?",
                  v.dead ? "DEAD" : (v.attacking ? "ATK " : "    "),
                  v.distM, v.locoFactor, v.atkFactor,
                  (float)(now - v.seenMs) / 1000.0f);
        logFile << l << std::endl;
    }
    if (!s_nView) logFile << "  (no enemies in view)" << std::endl;
}

} // namespace MonsterAI

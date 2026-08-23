// PawnAI::Haste — рывок пешки в бою. См. PawnHaste.h.

#include "stdafx.h"
#include "PawnHaste.h"
#include "../CombatBus.h"
#include "../runtime/Runtime.h"
#include "../runtime/MonsterTempo.h"

namespace PawnAI {
namespace Haste {

static bool  s_enabled = false;
static float s_factor  = 1.20f;
static float s_minDist = 5.0f;    // метры: ближе этого рывок не нужен
static float s_maxDist = 40.0f;   // дальше — это уже не бой, а догонялки
static bool  s_active  = false;
static int   s_applied = 0;
static float s_lastDist = -1.0f;
static char  s_why[64] = "off";
static bool  s_animCouple = false;   // ускорять ли заодно анимацию бега
// СОСТОЯНИЕ НА КАЖДУЮ ПЕШКУ, А НЕ ОДНО НА ВСЕХ.
//
// До 75.1 модуль знал ровно одно тело. Наёмные пешки — такие же `uCmc`,
// и им нужна та же компенсация: монстров мы разогнали всем сразу. Но
// решение принимается ПО КАЖДОЙ отдельно: у каждой своя дистанция до
// врага, своё состояние тела и свой гистерезис. Общий счётчик тут дал бы
// ровно тот дребезг, который мы лечили в 74.5.
//
// Граница из HIRED_PAWNS_SCOPE.md соблюдается по построению: мы трогаем
// только рантайм-множители, к записи персонажа чужой пешки не
// прикасаемся вовсе.
struct Slot {
    uintptr_t body;
    bool      active;
    bool      isMain;
    int       offStreak;
    float     lastDist;
    float     lastUsed;
    char      act[48];
    char      why[64];
};
static const int kMaxPawns = 4;
static Slot s_slot[kMaxPawns];
static int  s_nSlots = 0;

static Slot* SlotFor(uintptr_t body, bool isMain)
{
    for (int i = 0; i < s_nSlots; ++i)
        if (s_slot[i].body == body) { s_slot[i].isMain = isMain; return &s_slot[i]; }
    if (s_nSlots >= kMaxPawns) return 0;
    Slot& S = s_slot[s_nSlots++];
    memset(&S, 0, sizeof(S));
    S.body = body;
    S.isMain = isMain;
    S.lastDist = -1.0f;
    S.lastUsed = 1.0f;
    lstrcpynA(S.why, "new", sizeof(S.why));
    return &S;
}

static void ReleaseSlot(Slot& S, const char* why)
{
    S.offStreak = 0;
    if (S.active && S.body) Runtime::Tempo::ClearOverride(S.body);
    S.active = false;
    lstrcpynA(S.why, why, sizeof(S.why));
}

static void ReleaseAll(const char* why)
{
    for (int i = 0; i < s_nSlots; ++i) ReleaseSlot(s_slot[i], why);
    lstrcpynA(s_why, why, sizeof(s_why));
    s_active = false;
}

static bool  s_matchTempo = false;   // брать множитель у самых быстрых врагов
static float s_lastUsed = 1.0f;      // что применили в последний раз
static bool  s_requireWeapon = true; // требовать признак боя у самой пешки
static char  s_lastAct[48] = {};     // что делала пешка в момент включения
static int   s_actLogged = 0;
// Чем именно подтвердился бой: своим признаком пешки или детектором.
// Нужно, чтобы классификатор «оружие обнажено» можно было оценить
// числом, а не впечатлением: в замере 74.5 половина включений прошла
// как «weapon not detected» — пешка бежала с убранным оружием посреди
// боя, и гейт спасло только ИЛИ с детектором.
static int   s_burstsWeapon = 0;
static int   s_burstsDetector = 0;

// Жёсткий предел ровно тот же, что у примитива: 1.30. Выше начинается
// проскальзывание стоп, и подделка становится заметной.
static const float kMaxFactor = 1.30f;

// Короткий ttl — страховка от «мод перестал тикать, а пешка осталась
// быстрой». Тик идёт каждые 150 мс, поэтому 600 мс с запасом.
// TTL поднят с 600 до 1200 мс. Причина в логе 74.4: строка «anim restored
// (override ended)» повторялась сотню раз — переопределение снималось и
// ставилось заново каждые 150 мс, и ускорение шло рывками. Короткий ttl
// делал любой пропущенный тик обрывом эффекта.
static const uint32_t kTtlMs = 1200;

// Сколько тиков подряд условие должно быть ЛОЖНЫМ, прежде чем снимать
// ускорение. Тик 150 мс, значит 4 тика ≈ 600 мс терпения. Гистерезис
// нужен потому, что дистанция до врага дышит вокруг границы окна, и без
// него модуль включается-выключается на каждом шаге пешки.
static const int kOffTicks = 4;

// Расширение окна на выход. Вошли по 5…40 м, выходим только за 3…45 м:
// одна и та же граница на вход и на выход — это и есть дребезг.
static const float kExitSlackNear = 2.0f;
static const float kExitSlackFar  = 5.0f;

void Init()
{
    memset(s_slot, 0, sizeof(s_slot));
    s_nSlots = 0;
    s_active = false;
    s_applied = 0;
    s_actLogged = 0;
    s_burstsWeapon = 0;
    s_burstsDetector = 0;

    s_enabled = config.getBool("pawnHaste", "enabled", false);
    s_factor  = config.getFloat("pawnHaste", "factor", 1.20f);
    s_minDist = config.getFloat("pawnHaste", "minDistanceM", 5.0f);
    s_maxDist = config.getFloat("pawnHaste", "maxDistanceM", 40.0f);
    if (s_factor < 1.0f) s_factor = 1.0f;
    if (s_factor > kMaxFactor) s_factor = kMaxFactor;
    if (s_minDist < 1.0f) s_minDist = 1.0f;
    // ПО УМОЛЧАНИЮ СВЯЗКА ВКЛЮЧЕНА (74.6).
    //
    // Пока связка была экспериментом, её место было в положении «выкл».
    // Теперь ряд подтверждён на живой пешке, запись сужена до одного поля
    // и до состояний передвижения, а исходные значения возвращаются.
    // Оставлять «выкл» по умолчанию — значит по умолчанию отдавать
    // заведомо худший вариант: замер 74.5 прошёл именно так, и первая же
    // строка лога это сказала.
    s_animCouple = config.getBool("pawnHaste", "animCouple", true);
    s_matchTempo = config.getBool("pawnHaste", "matchMonsterTempo", true);
    s_requireWeapon = config.getBool("pawnHaste", "requireWeaponDrawn", true);

    char l[220];
    sprintf_s(l, "PawnHaste: %s, factor %.2f, window %.0f..%.0f m, anim coupling %s",
              s_enabled ? "enabled" : "disabled", s_factor, s_minDist, s_maxDist,
              s_animCouple ? "ON (run animation scales too)"
                           : "off (movement only - feet will slide)");
    // Флаг живёт в двух местах: здесь (решение слоя) и в примитиве
    // (разрешение писать). Раньше их связывал только ini, и рассинхрон
    // был вопросом времени.
    Runtime::Tempo::SetAnimForOverrides(s_animCouple);

    logFile << l << std::endl;
    sprintf_s(l, "PawnHaste: factor source %s",
              s_matchTempo ? "MATCH MONSTERS (compensation for the tempo we added)"
                           : "fixed number from the ini");
    logFile << l << std::endl;
}

void Shutdown()
{
    ReleaseAll("shutdown");
    if (s_applied > 0) {
        char l[180];
        const int details = s_actLogged > 3 ? 3 : s_actLogged;
        sprintf_s(l, "PawnHaste: session summary bursts=%d weapon=%d detector=%d details=%d",
                  s_applied, s_burstsWeapon, s_burstsDetector, details);
        logFile << l << std::endl;
    }
}

void SetEnabled(bool on)
{
    s_enabled = on;
    if (!on) ReleaseAll("off");
}

void SetFactor(float f)
{
    if (f < 1.0f) f = 1.0f;
    if (f > kMaxFactor) f = kMaxFactor;
    s_factor = f;
}

void SetRequireWeapon(bool on) { s_requireWeapon = on; }
void SetMatchTempo(bool on)     { s_matchTempo = on; }
void SetAnimCouple(bool on)
{
    s_animCouple = on;
    Runtime::Tempo::SetAnimForOverrides(on);
}

// Решение по ОДНОЙ пешке. Всё, что раньше было телом Tick(), переехало
// сюда без изменений по существу: те же окна, тот же гистерезис, тот же
// гейт по оружию и по движению. Изменился только адресат: теперь их
// столько, сколько пешек в партии.
static void TickPawn(Slot& S, const WorldReport& w, const CombatReport& rep)
{
    const uintptr_t pawn = S.body;

    // Позиция тела: +0x40/+0x44/+0x48 (SOURCE_OF_TRUTH §2). У главной
    // пешки её же отдаёт продуктовый слой, но наёмным нужен общий путь.
    float p[3] = {};
    if (!Runtime::ReadSafe(pawn + 0x40, p, sizeof(p))) {
        ReleaseSlot(S, "no position");
        return;
    }

    // Ближайший враг к ЭТОЙ пешке. Попутно смотрим, насколько разогнаны
    // те, кто рядом: это и есть мера компенсации.
    float best = 1.0e9f;
    float fastest = 1.0f;
    for (int i = 0; i < w.count; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
        const float dx = u.x - p[0], dy = u.y - p[1], dz = u.z - p[2];
        const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
        if (d < best) best = d;
        if (d <= s_maxDist) {
            float lo = 1.0f, at = 1.0f;
            if (Runtime::Tempo::GetFactors(u.ptr, &lo, &at) && lo > fastest)
                fastest = lo;
        }
    }
    if (best > 1.0e8f) { S.lastDist = -1.0f; ReleaseSlot(S, "no enemy position"); return; }
    S.lastDist = best;

    // Окно применения с гистерезисом: вошли по 5…40 м, выходим за 3…45 м,
    // и только после нескольких неудачных тиков подряд. Одна и та же
    // граница на вход и на выход — это и есть дребезг (74.5).
    const float nearGate = S.active ? (s_minDist - kExitSlackNear) : s_minDist;
    const float farGate  = S.active ? (s_maxDist + kExitSlackFar)  : s_maxDist;
    if (best < nearGate || best > farGate) {
        if (++S.offStreak >= kOffTicks)
            ReleaseSlot(S, best < nearGate ? "in contact" : "too far - vanilla dash applies");
        return;
    }

    char act[48] = {};
    Runtime::ReadLiveAct(pawn, act, sizeof(act));
    lstrcpynA(S.act, act, sizeof(S.act));

    // Не ускорять сбитую с ног (поймано на `cPlActDmgCrumbleDead`).
    if (act[0] && (!strncmp(act, "cPlActDmg", 9)
                || !strncmp(act, "cPlActDead", 10)
                || !strncmp(act, "cPlActNeardeath", 15)
                || !strncmp(act, "cPlActLifted", 12))) {
        if (++S.offStreak >= kOffTicks) ReleaseSlot(S, "knocked down");
        return;
    }

    // Ускорять только того, кто идёт. Состояния переноски входят сюда
    // намеренно: Утилитарианец с метательным хламом занимает позицию.
    const bool moving = act[0]
        && (!strcmp(act, "cPlActRun") || !strcmp(act, "cPlActWalk")
            || !strcmp(act, "cPlActRunEnd") || !strcmp(act, "cPlActHoldWeaponMv")
            || !strncmp(act, "cPlActDash", 10)
            || !strcmp(act, "cPlActLiftRun") || !strcmp(act, "cPlActLiftWalk")
            || !strcmp(act, "cPlActLiftJump"));

    // Признак боя у самой пешки: обнажённое оружие ИЛИ детектор.
    const bool weaponOut = act[0]
        && (!strncmp(act, "cPlActHoldWeapon", 16) || !strncmp(act, "cPlActWpn", 9));
    if (s_requireWeapon) {
        const bool fighting = weaponOut || rep.inCombat || w.enemyCombatCount > 0
                            || w.pawnEngaged;
        if (!fighting) {
            if (++S.offStreak >= kOffTicks) ReleaseSlot(S, "weapon sheathed - not fighting");
            return;
        }
    }
    S.offStreak = 0;

    // Проба ряда анимации — один раз на тело, только чтение.
    if (!S.active) {
        char label[40];
        sprintf_s(label, "uCmc (%s)", S.isMain ? "main pawn" : "hired pawn");
        Runtime::Tempo::AnimRowProbe(pawn, label);
    }

    // Компенсация, а не бафф: число берём у самого быстрого врага рядом.
    float use = s_factor;
    if (s_matchTempo) {
        use = fastest;
        if (use < 1.0f) use = 1.0f;
        if (use > kMaxFactor) use = kMaxFactor;
        if (use <= 1.0f) {
            ReleaseSlot(S, "monsters run vanilla - nothing to compensate");
            return;
        }
    }

    // Дорожка не сносится, когда пешка остановилась: неподвижной выдаём
    // нейтральное число (множитель передвижения у стоящего и так ничего
    // не делает — он умножает покадровое смещение).
    const float locoFactor = moving ? use : 1.0f;
    const float animFactor = s_animCouple ? use : 1.0f;
    Runtime::Tempo::SetOverride(pawn, locoFactor, animFactor, kTtlMs);
    S.lastUsed = locoFactor;
    lstrcpynA(S.why, moving ? "compensating" : "holding (not moving)", sizeof(S.why));

    if (!S.active) {
        ++s_applied;
        if (weaponOut) ++s_burstsWeapon; else ++s_burstsDetector;
        if (s_actLogged < 3) {
            ++s_actLogged;
            const Runtime::Tempo::Status ts = Runtime::Tempo::GetStatus();
            char l[280];
            sprintf_s(l, "PawnHaste: burst on %s pawn at %.1f m, x%.2f, act %s"
                         " (weapon %s), anim coupling %s, anim writes so far %u (#%d)",
                      S.isMain ? "MAIN" : "hired", best, use, act[0] ? act : "?",
                      weaponOut ? "OUT" : "not detected",
                      Runtime::Tempo::GetAnimForOverrides() ? "ON" : "OFF",
                      ts.animOurWrites, s_actLogged);
            logFile << l << std::endl;
        } else if (s_actLogged == 3) {
            ++s_actLogged;
            logFile << "PawnHaste: burst detail limit reached; further bursts are"
                       " counted silently (panel and shutdown summary)" << std::endl;
        }
    }
    S.active = true;

    // Сводка для панели берётся у главной пешки: она в фокусе внимания.
    if (S.isMain) {
        s_active = true;
        s_lastDist = best;
        s_lastUsed = locoFactor;
        lstrcpynA(s_lastAct, act, sizeof(s_lastAct));
        lstrcpynA(s_why, S.why, sizeof(s_why));
    }
}

void Tick()
{
    if (!s_enabled) { ReleaseAll("off"); return; }

    const WorldReport w = CombatBus::Instance().LastWorld();
    if (w.enemyCount <= 0) { s_lastDist = -1.0f; ReleaseAll("no enemies"); return; }

    const int n = Runtime::PawnBodyCount();
    if (n <= 0) { ReleaseAll("no pawn bodies"); return; }

    const CombatReport rep = CombatBus::Instance().LastReport();

    // Сводку главной пешки собираем заново каждый тик: если её слот в
    // этом тике не обновится, панель не должна показывать вчерашнее.
    s_active = false;

    bool seen[kMaxPawns] = {};
    for (int i = 0; i < n && i < kMaxPawns; ++i) {
        bool isMain = false;
        const uintptr_t body = Runtime::PawnBodyAt(i, &isMain);
        if (!body) continue;
        Slot* S = SlotFor(body, isMain);
        if (!S) continue;
        seen[(int)(S - s_slot)] = true;
        TickPawn(*S, w, rep);
    }

    // Пешку могли уволить или она умерла — слот больше не подтверждается
    // разбором партии. Указатель хранить нельзя, но и бросать живое
    // переопределение на ttl нельзя: чужое тело могло уже пересесть на
    // тот же адрес. Снимаем явно, потом забываем слот.
    for (int i = 0; i < s_nSlots; ) {
        if (seen[i]) { ++i; continue; }
        ReleaseSlot(s_slot[i], "pawn left party");
        for (int k = i + 1; k < s_nSlots; ++k) s_slot[k - 1] = s_slot[k];
        --s_nSlots;
    }
}


Status Get()
{
    Status s;
    s.enabled = s_enabled;
    s.active  = s_active;
    s.factor  = s_factor;
    s.distM   = s_lastDist;
    s.applied = s_applied;
    s.used    = s_lastUsed;
    s.matchTempo = s_matchTempo;
    s.requireWeapon = s_requireWeapon;
    s.animCouple = Runtime::Tempo::GetAnimForOverrides();
    s.burstsWeapon = s_burstsWeapon;
    s.burstsDetector = s_burstsDetector;
    s.pawnsTracked = s_nSlots;
    int act = 0;
    for (int i = 0; i < s_nSlots; ++i) if (s_slot[i].active) ++act;
    s.pawnsActive = act;
    lstrcpynA(s.act, s_lastAct, sizeof(s.act));
    lstrcpynA(s.why, s_why, sizeof(s.why));
    return s;
}

} // namespace Haste
} // namespace PawnAI

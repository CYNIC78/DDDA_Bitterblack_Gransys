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
static uintptr_t s_body = 0;      // кому применили, чтобы было что снять
static bool  s_animCouple = false;   // ускорять ли заодно анимацию бега
static bool  s_probed = false;       // ряд множителей у пешки уже проверен
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
static int  s_offStreak = 0;

// Расширение окна на выход. Вошли по 5…40 м, выходим только за 3…45 м:
// одна и та же граница на вход и на выход — это и есть дребезг.
static const float kExitSlackNear = 2.0f;
static const float kExitSlackFar  = 5.0f;

void Init()
{
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

static void Release(const char* why)
{
    s_offStreak = 0;
    if (s_body) {
        Runtime::Tempo::ClearOverride(s_body);
        s_body = 0;
    }
    s_active = false;
    lstrcpynA(s_why, why, sizeof(s_why));
}

void Shutdown() { Release("shutdown"); }

void SetEnabled(bool on)
{
    s_enabled = on;
    if (!on) Release("off");
}

void SetFactor(float f)
{
    if (f < 1.0f) f = 1.0f;
    if (f > kMaxFactor) f = kMaxFactor;
    s_factor = f;
}

void SetRequireWeapon(bool on) { s_requireWeapon = on; }

void Tick()
{
    if (!s_enabled) { if (s_active) Release("off"); return; }

    // Тело главной пешки и её позиция — из продуктового слоя.
    float px = 0, py = 0, pz = 0;
    if (!Runtime::GetMainPawnWorldPos(&px, &py, &pz)) {
        Release("no pawn position");
        return;
    }
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!pawn) { Release("no pawn body"); return; }

    // Бой определяем по присутствию врагов, а не по факту удара: пешке
    // надо разгоняться ДО первого размена, иначе смысл теряется.
    const WorldReport w = CombatBus::Instance().LastWorld();
    if (w.enemyCount <= 0) { s_lastDist = -1.0f; Release("no enemies"); return; }

    // Ближайший враг к ПЕШКЕ, а не к Аризену: рывок нужен ей.
    // Попутно смотрим, НАСКОЛЬКО разогнаны те, кто рядом: это и есть
    // мера компенсации (см. ниже).
    float best = 1.0e9f;
    float fastest = 1.0f;
    for (int i = 0; i < w.count; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
        const float dx = u.x - px, dy = u.y - py, dz = u.z - pz;
        const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
        if (d < best) best = d;
        if (d <= s_maxDist) {
            float lo = 1.0f, at = 1.0f;
            if (Runtime::Tempo::GetFactors(u.ptr, &lo, &at) && lo > fastest)
                fastest = lo;
        }
    }
    if (best > 1.0e8f) { s_lastDist = -1.0f; Release("no enemy position"); return; }
    s_lastDist = best;

    // Окно применения.
    //
    // Ближняя граница: вплотную рывок не нужен и выглядит дёрганьем —
    // пешка и так на месте. Дальняя: если враг за сорок метров, это уже
    // не бой, а переход, и там пешка спринтует сама (цель Follow).
    // Гистерезис: пока ускорение УЖЕ работает, окно шире, и снимаем его
    // только после нескольких неудачных тиков подряд.
    const float nearGate = s_active ? (s_minDist - kExitSlackNear) : s_minDist;
    const float farGate  = s_active ? (s_maxDist + kExitSlackFar)  : s_maxDist;
    if (best < nearGate || best > farGate) {
        if (++s_offStreak >= kOffTicks)
            Release(best < nearGate ? "in contact" : "too far - vanilla dash applies");
        return;
    }

    // ОБНАЖЕНО ЛИ ОРУЖИЕ (предложение тестера, и оно точнее нашего).
    //
    // «Враг в радиусе» — грубая метка: враги за стеной, которых партия не
    // видит, включали ускорение. У пешки же есть собственный признак боя,
    // который не врёт: состояния с обнажённым оружием. В логе они видны
    // прямо: `cPlActHoldWeaponMv` (движение с оружием в руках) против
    // `cPlActRun` (оружие убрано), плюс всё семейство `cPlActWpn*` —
    // это уже удары.
    //
    // Признак используется как ГЕЙТ, а не как единственный сигнал:
    // требуется обнажённое оружие ИЛИ подтверждение продуктового
    // детектора боя. Так пешка, идущая с оружием наголо по пустой
    // дороге, ускорения не получит, а застигнутая врасплох — получит.
    char act[48] = {};
    Runtime::ReadLiveAct(pawn, act, sizeof(act));

    // НЕ УСКОРЯТЬ СБИТУЮ С НОГ ПЕШКУ.
    //
    // В замере 74.6 включение поймано на `cPlActDmgCrumbleDead`: пешку
    // складывает ударом, а мы в этот момент решаем, что ей надо быстрее
    // бежать. Вреда мало (в этих состояниях анимацию мы не трогаем), но
    // это неверное решение слоя, а неверные решения надо убирать, пока
    // они не смешались с верными в статистике.
    if (act[0] && (!strncmp(act, "cPlActDmg", 9)
                || !strncmp(act, "cPlActDead", 10)
                || !strncmp(act, "cPlActNeardeath", 15)
                || !strncmp(act, "cPlActLifted", 12))) {
        if (++s_offStreak >= kOffTicks) Release("knocked down");
        return;
    }

    // УСКОРЯТЬ ТОЛЬКО ТОГО, КТО ИДЁТ.
    //
    // Замер 74.6 поймал включения на `cPlActWait` (пешка стоит),
    // `cPlActJump` и посреди приёма кинжалами. Анимации это не касалось —
    // туда множитель и не заходит, — но множитель ПЕРЕДВИЖЕНИЯ применялся
    // всё равно, а компенсация задумана ровно про перемещение.
    //
    // Теперь состояние тела — часть условия, а не только область записи:
    // нет шага — нет и компенсации.
    const bool moving = act[0]
        && (!strcmp(act, "cPlActRun") || !strcmp(act, "cPlActWalk")
            || !strcmp(act, "cPlActRunEnd") || !strcmp(act, "cPlActHoldWeaponMv")
            || !strncmp(act, "cPlActDash", 10)
            // Утилитарианец с метательным хламом: подобрала (LiftBeginSmallItem),
            // побежала занимать позицию (LiftRun/LiftWalk), метнула. В замере
            // 74.8 эта цепочка видна целиком, и это ровно то перемещение под
            // давлением, ради которого модуль существует.
            || !strcmp(act, "cPlActLiftRun") || !strcmp(act, "cPlActLiftWalk")
            || !strcmp(act, "cPlActLiftJump"));

    const bool weaponOut = act[0]
        && (!strncmp(act, "cPlActHoldWeapon", 16) || !strncmp(act, "cPlActWpn", 9));
    if (s_requireWeapon) {
        const CombatReport rep = CombatBus::Instance().LastReport();
        const bool fighting = weaponOut || rep.inCombat || w.enemyCombatCount > 0
                            || w.pawnEngaged;
        if (!fighting) {
            if (++s_offStreak >= kOffTicks) Release("weapon sheathed - not fighting");
            return;
        }
    }
    s_offStreak = 0;

    // СВЯЗКА АНИМАЦИИ (замечание тестера, и оно верное).
    //
    // Раньше здесь стояла жёсткая единица: «двигаем тушку, анимацию не
    // трогаем». Отсюда и проскальзывание стоп, которое мы честно
    // называли подделкой. Но у монстров ускорение анимации у нас уже
    // работает — рядом множителей воспроизведения. Если тот же ряд есть
    // в теле пешки, подделка превращается в нормальный быстрый бег.
    //
    // Порядок осторожный:
    //   1. один раз ЧИТАЕМ ряд у пешки и печатаем вердикт (без записи);
    //   2. писать разрешено только при `[pawnHaste] animCouple = on`
    //      И только если все пять полей были ровно 1.0;
    //   3. при снятии рывка примитив возвращает исходные значения сам.
    if (!s_probed) {
        s_probed = true;
        Runtime::Tempo::AnimRowProbe(pawn, "uCmc (main pawn)");
    }
    // КОМПЕНСАЦИЯ, А НЕ БАФФ.
    //
    // Формулировка тестера меняет смысл модуля: мы ускорили монстров —
    // мы и обязаны вернуть пешке возможность передислоцироваться под их
    // напором. Значит правильное число берётся не с потолка, а у самих
    // монстров: насколько разогнан самый быстрый враг поблизости,
    // настолько и компенсируем. Ни больше.
    //
    // При `matchMonsterTempo = off` работает прежнее фиксированное число.
    float use = s_factor;
    if (s_matchTempo) {
        use = fastest;
        if (use < 1.0f) use = 1.0f;
        if (use > kMaxFactor) use = kMaxFactor;
        if (use <= 1.0f) { Release("monsters run vanilla - nothing to compensate"); return; }
    }

    // ПЕРЕОПРЕДЕЛЕНИЕ НЕ СНИМАЕТСЯ, КОГДА ПЕШКА ОСТАНОВИЛАСЬ.
    //
    // В 74.8 «не движется» приводило к Release(), то есть дорожка анимации
    // сносилась и ставилась заново на каждом размене: в логе 74.8 счётчик
    // снятий дошёл до предела и дальше считался молча. Это лишняя возня в
    // чужой памяти на ровном месте.
    //
    // Правильнее оставить дорожку жить, а на время неподвижности выдать
    // нейтральное число. Множитель ПЕРЕДВИЖЕНИЯ при этом безопасен сам по
    // себе: он умножает покадровое смещение, а у стоящего оно нулевое.
    // Анимацию же гасит свой гейт внутри примитива (`move=0` в логе
    // наблюдателя), поэтому в замахе ничего не ускорится.
    const float locoFactor = moving ? use : 1.0f;
    const float animFactor = s_animCouple ? use : 1.0f;
    Runtime::Tempo::SetOverride(pawn, locoFactor, animFactor, kTtlMs);
    s_lastUsed = locoFactor;
    lstrcpynA(s_lastAct, act, sizeof(s_lastAct));
    // Классификатор «оружие обнажено» проверяется тем же способом, что и
    // всё остальное в проекте: первые срабатывания печатаются с именем
    // состояния, чтобы метку можно было опровергнуть, а не поверить ей.
    if (!s_active) { if (weaponOut) ++s_burstsWeapon; else ++s_burstsDetector; }
    if (!s_active && s_actLogged < 8) {
        ++s_actLogged;
        char l[240];
        // Счётчик наших записей в ряд — прямо в строке включения.
        // Иначе «связка включена» и «связка что-то делает» опять окажутся
        // разными утверждениями, которые нечем развести.
        const Runtime::Tempo::Status ts = Runtime::Tempo::GetStatus();
        sprintf_s(l, "PawnHaste: burst at %.1f m, x%.2f, pawn act %s (weapon %s),"
                     " anim coupling %s, anim writes so far %u",
                  best, use, act[0] ? act : "?", weaponOut ? "OUT" : "not detected",
                  Runtime::Tempo::GetAnimForOverrides() ? "ON" : "OFF -> feet will slide",
                  ts.animOurWrites);
        logFile << l << std::endl;
    }
    if (!s_active) ++s_applied;
    s_active = true;
    s_body = pawn;
    sprintf_s(s_why, "closing %.0f m", best);
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
    lstrcpynA(s.act, s_lastAct, sizeof(s.act));
    lstrcpynA(s.why, s_why, sizeof(s.why));
    return s;
}

} // namespace Haste
} // namespace PawnAI

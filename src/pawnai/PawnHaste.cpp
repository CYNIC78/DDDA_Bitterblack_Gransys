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

// Жёсткий предел ровно тот же, что у примитива: 1.30. Выше начинается
// проскальзывание стоп, и подделка становится заметной.
static const float kMaxFactor = 1.30f;

// Короткий ttl — страховка от «мод перестал тикать, а пешка осталась
// быстрой». Тик идёт каждые 150 мс, поэтому 600 мс с запасом.
static const uint32_t kTtlMs = 600;

void Init()
{
    s_enabled = config.getBool("pawnHaste", "enabled", false);
    s_factor  = config.getFloat("pawnHaste", "factor", 1.20f);
    s_minDist = config.getFloat("pawnHaste", "minDistanceM", 5.0f);
    s_maxDist = config.getFloat("pawnHaste", "maxDistanceM", 40.0f);
    if (s_factor < 1.0f) s_factor = 1.0f;
    if (s_factor > kMaxFactor) s_factor = kMaxFactor;
    if (s_minDist < 1.0f) s_minDist = 1.0f;

    char l[200];
    sprintf_s(l, "PawnHaste: %s, factor %.2f, window %.0f..%.0f m",
              s_enabled ? "enabled" : "disabled", s_factor, s_minDist, s_maxDist);
    logFile << l << std::endl;
}

static void Release(const char* why)
{
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
    float best = 1.0e9f;
    for (int i = 0; i < w.count; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
        const float dx = u.x - px, dy = u.y - py, dz = u.z - pz;
        const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
        if (d < best) best = d;
    }
    if (best > 1.0e8f) { s_lastDist = -1.0f; Release("no enemy position"); return; }
    s_lastDist = best;

    // Окно применения.
    //
    // Ближняя граница: вплотную рывок не нужен и выглядит дёрганьем —
    // пешка и так на месте. Дальняя: если враг за сорок метров, это уже
    // не бой, а переход, и там пешка спринтует сама (цель Follow).
    if (best < s_minDist) { Release("in contact"); return; }
    if (best > s_maxDist) { Release("too far - vanilla dash applies"); return; }

    Runtime::Tempo::SetOverride(pawn, s_factor, 1.0f, kTtlMs);
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
    lstrcpynA(s.why, s_why, sizeof(s.why));
    return s;
}

} // namespace Haste
} // namespace PawnAI

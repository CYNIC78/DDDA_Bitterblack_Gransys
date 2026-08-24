#pragma once
// PackObserve — ночной observe-прибор exact uEm0100.
//
// Только чтение. Ни Tempo, ни Aggro, ни Director actuator не трогает.
// Лидер не назначается по scale: корона доказывается рогом / ChargeCommand
// и бегством стаи после исчезновения кандидата. Обычные 3–4 гоблина —
// rabble; большая ночная стая — led. Роль — на встречу, не на вид.
// Новых галок F12 нет: тик всегда, лог — только переходы.

#include <stdint.h>

struct WorldReport;

namespace MonsterAI {

enum PackRole {
    PACK_ROLE_UNKNOWN   = 0,
    PACK_ROLE_IDLE      = 1,
    PACK_ROLE_MOVE      = 2,
    PACK_ROLE_MELEE     = 3,
    PACK_ROLE_THROW     = 4,
    PACK_ROLE_SHIELD    = 5,
    PACK_ROLE_GUARD     = 6,
    PACK_ROLE_CALLER    = 7,
    PACK_ROLE_COMMAND   = 8,
    PACK_ROLE_FLEE      = 9,
    PACK_ROLE_RESTRAINT = 10,
    PACK_ROLE_INSUB     = 11
};

enum PackComposition {
    PACK_NONE   = 0,
    PACK_RABBLE = 1,
    PACK_LED    = 2
};

int ClassifyGoblinAct(const char* act);
const char* PackRoleName(int role);
const char* PackCompositionName(int composition);

void PackObserveInit();
void PackObserveShutdown();
void PackObserveTick();
void PackObserveIngest(const WorldReport& world, uint32_t nowMs);
void PackObserveDump();
const char* PackObserveStatus();
int PackObserveCount();
int PackObserveComposition();

} // namespace MonsterAI

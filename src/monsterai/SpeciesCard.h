#pragma once
// SpeciesCard — словарь допуска по ТОЧНОМУ DTI-имени.
//
// ЗАЧЕМ. Волк (uEm0200) пишет Tempo+Aggro. Гоблин (uEm0100) пишет только
// Aggro на GrabStart/Hagaijime (оппортунист, не стая) и Tempo rage не получает.
// Префикс uEm010* и компоненты uEm0100_0/_2/_3 сюда не входят. Следующий
// вид — отдельная строка, не копирование волчьих чисел.

#include <stdint.h>
#include <string.h>

namespace MonsterAI {

struct SpeciesCard {
    const char* kind;       // точное DTI-имя, strcmp, не prefix
    uint32_t    bodySize;   // TypeAtlas size (uEm0100 = 29632 = 0x73C0)
    bool        observe;    // PackObserve имеет право следить
    bool        tempoRage;  // AdmitDirectorMobilization
    bool        aggroWrite; // DirectorFocusSet
    // Rage-профиль (84.21): per-body детерминированный roll живёт в этих
    // диапазонах при мобилизации (std-rush). Баланс вида — здесь:
    //   волк  — сбалансированный (проверенный профиль, без изменений);
    //   гоблин — атака быстрее локомоции (малый быстрый боец).
    // Диапазоны обязаны оставаться в пределах species-safe clamp
    // (loco 0.75..1.30, anim 0.70..1.40) и выше потолка стабильного
    // профиля (1.20 / 1.15), иначе admission reject.
    float       rageLocoLo, rageLocoHi;
    float       rageAnimLo, rageAnimHi;
};

static const SpeciesCard kSpeciesCards[] = {
    { "uEm0200", 29888u, true, true,  true,
      1.20f, 1.25f, 1.20f, 1.26f },
    // 84.20: приказ = экстренная ситуация — goblin-lease получает std-rush
    // (те же правила мобилизации, что и волк). Aggro-часть lease не менялась:
    // pin + fakehit, без suppress.
    { "uEm0100", 29632u, true, true,  true,
      1.21f, 1.24f, 1.32f, 1.40f },
    // 84.26: hobgoblin. Same body size as goblin; +0xEE4 CONFIRMED (log 6).
    // Grab = goblin recipe. PackMark = wolf weakest-HP. Rage ceiling below goblin.
    { "uEm0101", 29632u, true, true,  true,
      1.21f, 1.23f, 1.24f, 1.32f },
    // 84.29: ordinary Saurian. Live Devilfire-adjacent pack: DTI uEm0400,
    // Tempo ACCEPTED, cEm0400* (PoisonGun/Fours/JumpDown), Fluffy em0400.
    // PackMark weakest-HP only. NO grab cue. Rage just above live stable
    // (loco≤1.189 anim≤1.115); 1.05..1.15 is already baseline scatter.
    { "uEm0400", 29568u, true, true,  true,
      1.20f, 1.22f, 1.20f, 1.23f },
};

inline int SpeciesCardCount()
{
    return (int)(sizeof(kSpeciesCards) / sizeof(kSpeciesCards[0]));
}

inline const SpeciesCard* FindSpeciesCard(const char* kind)
{
    if (!kind || !kind[0]) return 0;
    for (int i = 0; i < SpeciesCardCount(); ++i) {
        if (!strcmp(kSpeciesCards[i].kind, kind))
            return &kSpeciesCards[i];
    }
    return 0;
}

inline bool SpeciesExactKind(const char* kind, const char* expect)
{
    return kind && expect && kind[0] && !strcmp(kind, expect);
}

inline bool SpeciesIsObserveOnly(const SpeciesCard* card)
{
    return card && card->observe && !card->tempoRage && !card->aggroWrite;
}

} // namespace MonsterAI

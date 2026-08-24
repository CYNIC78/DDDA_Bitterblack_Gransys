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
};

static const SpeciesCard kSpeciesCards[] = {
    { "uEm0200", 29888u, true, true,  true  },
    { "uEm0100", 29632u, true, false, true  },
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

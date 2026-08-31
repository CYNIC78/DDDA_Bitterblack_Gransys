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
    // Генетический коридор масштаба (84.44):
    float       scaleMin, scaleMax;     // безопасный коридор размера рядовых (0.93..1.15)
    float       leaderScaleThreshold;   // порог детекции ванильного альфы/лидера
    float       scaleJitter;            // неуниформность комплекции (W/D vs H)
};

static const SpeciesCard kSpeciesCards[] = {
    // Wolf: сбалансированный, альфа-вожак Capcom >= 1.10 сохраняется
    { "uEm0200", 29888u, true, true,  true,
      1.20f, 1.25f, 1.20f, 1.26f,
      0.94f, 1.10f, 1.10f, 0.05f },
    // Goblin: атака быстрее локомоции (малый быстрый боец), коридор 0.93..1.15
    { "uEm0100", 29632u, true, true,  true,
      1.15f, 1.20f, 1.15f, 1.24f,
      0.93f, 1.15f, 1.12f, 0.08f },
    // Hobgoblin: тяжелый бронированный гоблин, естественный темп замаха
    { "uEm0101", 29632u, true, true,  true,
      1.15f, 1.20f, 1.10f, 1.18f,
      0.95f, 1.14f, 1.12f, 0.06f },
    // Saurian: обычный ящер
    { "uEm0400", 29568u, true, true,  true,
      1.20f, 1.22f, 1.20f, 1.23f,
      0.95f, 1.12f, 1.12f, 0.05f },
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

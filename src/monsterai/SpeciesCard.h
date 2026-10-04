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
    // (loco 0.75..1.30, anim 0.70..1.40). Для допуска достаточно, чтобы ролл
    // ярости начинался не ниже старта стабильного и заканчивался не ниже его
    // потолка: rageLocoLo >= [monsterTempo] factorMin и rageLocoHi >= factorMax
    // (то же для anim). При пакетном ini это 1.05 и 1.20 / 1.15 — нынешние
    // числа всех четырёх видов условие выполняют.
    //
    // ЧЕГО НЕЛЬЗЯ ДЕЛАТЬ (найдено 2026-09-25, стоило трёх билдов): стабильный
    // множитель поднимается ПОСЛЕ ролла компенсацией частоты шага у тел со
    // scale < 1.0 (`MonsterTempo.cpp` FactorFor). Если такой механизм поднимет
    // стабильное значение выше ролла ярости, допуск отобьёт тело молча
    // (`director-mobilization-baseline-outside-profile`). Поэтому пол
    // scaleMin ниже держим на 1.0 — вместе с тем это и решение владельца:
    // масштаб это ручка опасности, задохликов не делаем.
    float       rageLocoLo, rageLocoHi;
    float       rageAnimLo, rageAnimHi;
    // Генетический коридор масштаба (84.44):
    float       scaleMin, scaleMax;     // коридор размера рядовых; пол = 1.00
    float       leaderScaleThreshold;   // порог детекции ванильного альфы/лидера
    float       scaleJitter;            // неуниформность комплекции (W/D vs H)
};

static const SpeciesCard kSpeciesCards[] = {
    // Wolf: сбалансированный, альфа-вожак Capcom >= 1.10 сохраняется
    { "uEm0200", 29888u, true, true,  true,
      1.20f, 1.25f, 1.20f, 1.26f,
      1.00f, 1.10f, 1.10f, 0.05f },
    // Goblin: атака быстрее локомоции (малый быстрый боец), коридор 1.00..1.15
    { "uEm0100", 29632u, true, true,  true,
      1.15f, 1.20f, 1.15f, 1.24f,
      1.00f, 1.15f, 1.12f, 0.08f },
    // Hobgoblin: тяжелый бронированный гоблин, естественный темп замаха.
    //
    // КОРИДОР ПРИГЛУШЁН (85.86). Поле 85.85 впервые дало настоящий ванильный
    // рост хоба: 1.411 / 1.424 / 1.450 / 1.514 / 1.550 / 1.569 / 1.590 / 1.596
    // — то есть 1.41..1.60, сам по себе разброс 13 %. Коридор 1.00..1.14
    // поверх такой базы давал до 1.75 и хобы выглядели великанами (жалоба
    // владельца). У гоблина база около 1.02..1.17, поэтому там те же 14 %
    // смотрятся нормально — вид крупный, прибавка к нему и должна быть
    // скромнее. Берём 1.00..1.05: максимум по пачке поднимается с 1.596 до
    // ~1.68, и это заметно, но не карикатурно.
    // 85.94: ТЕМП БЕГА ПРИГЛУШЁН 1.15..1.20 -> 1.10..1.15. Решение владельца:
    // «хобы всё-таки побольше гоблинов и должны быть визуально помедленнее».
    // Верх полосы и есть потолок вида, а он теперь режет и базовый бросок
    // (см. SpeciesLocoCeiling в MonsterTempo.cpp), поэтому разогнанный крик
    // ярости перестаёт выглядеть комично. Замах не трогаем: 1.10..1.18
    // читается нормально и держит хоба опасным.
    { "uEm0101", 29632u, true, true,  true,
      1.10f, 1.15f, 1.10f, 1.18f,
      1.00f, 1.05f, 1.12f, 0.03f },
    // Saurian: обычный ящер
    { "uEm0400", 29568u, true, true,  true,
      1.20f, 1.22f, 1.20f, 1.23f,
      1.00f, 1.12f, 1.12f, 0.05f },
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

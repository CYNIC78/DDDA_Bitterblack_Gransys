#pragma once
// SpeciesTuning — потолки разгона вида в ddda_ai_overhaul.ini (85.36).
//
// ЗАЧЕМ. Числа вида — докуда особь разгоняется по приказу директора — жили в
// SpeciesCard.h, то есть в КОДЕ: подобрать их можно было только пересборкой в
// студии. Теперь они читаются из ini секцией вида: [species.uEm0100] и т.д.
// Отсутствующий ключ = число из карточки, поэтому старый ini ведёт себя ровно
// как раньше и ничего не ломает.
//
// ЗАЧЕМ ЗАЖИМ, А НЕ ПРОСТО ЧТЕНИЕ. Тело получает разгон ТОЛЬКО если потолок
// выше его стабильного темпа. Стабильный ролл и ролл разгона берутся от ОДНОГО
// хеша адреса тела, поэтому условие сводится к простому: низ потолка не ниже
// низа базового диапазона, верх — не ниже верха (MonsterTempo.cpp,
// "director-mobilization-baseline-outside-profile"). Впиши владелец число ниже —
// приказ будет молча отбит на каждом теле, и это ровно та ловушка, что стоила
// трёх билдов. Поэтому небезопасное число здесь поднимается до безопасного, а в
// лог уходит строка о том, что именно подняли.

#include "SpeciesCard.h"

namespace MonsterAI {

// То, что модулю нужно от ini. Отдельный интерфейс, а не iniConfig, — чтобы
// числа вида проверялись и в рантайм-фикстуре с подменённым конфигом.
struct SpeciesIniReader {
    virtual ~SpeciesIniReader() {}
    virtual float Float(const char* section, const char* key, float defValue) = 0;
    virtual bool  Bool (const char* section, const char* key, bool  defValue) = 0;
};

struct SpeciesTempoNumbers {
    bool  rageEnabled;              // tempoRage: 0 = вид не разгоняется вообще
    float rageLocoMin, rageLocoMax; // значение, которое приказ требует от бега
    float rageAnimMin, rageAnimMax; // и от темпа замаха
    bool  sanitized;                // число пришлось поднять или починить
    char  note[200];                // что именно поправлено — для лога
};

// Секция: [species.<kind>]; ключи: rageLocoMin, rageLocoMax, rageAnimMin,
// rageAnimMax, tempoRage. baseLocoMin/Max и baseAnimMin/Max — действующие
// границы [monsterTempo] (factorMin/factorMax, animFactorMin/animFactorMax).
SpeciesTempoNumbers SpeciesTempoFromIni(SpeciesIniReader& ini,
                                        const SpeciesCard& card,
                                        float baseLocoMin, float baseLocoMax,
                                        float baseAnimMin, float baseAnimMax);

} // namespace MonsterAI

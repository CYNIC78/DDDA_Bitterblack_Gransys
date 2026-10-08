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
// хеша адреса тела, поэтому условие сводится к простому: потолок не ниже
// базового диапазона (MonsterTempo.cpp,
// "director-mobilization-baseline-outside-profile"). Впиши владелец число ниже —
// приказ будет молча отбит на каждом теле, и это ровно та ловушка, что стоила
// трёх билдов. Поэтому небезопасное число здесь поднимается до безопасного, а в
// лог уходит строка о том, что именно подняли.
//
// 85.96: «базовый диапазон» в этом правиле — база ВИДА (SpeciesBaseRangeFor),
// а не общий [monsterTempo]. Пока сверяли с общим, вид с намеренно низкой базой
// (хоб: спокойно 1.02..1.15) получал «rageLocoMax raised(1.15->1.20)», то есть
// решение вида молча не применялось. Вызывающий обязан передать базу вида —
// директор это делает, и гейт (шаг 2i) проверяет, что поставочные карточки
// проходят зажим без правок.

#include "SpeciesCard.h"

namespace MonsterAI {

// То, что модулю нужно от ini. Отдельный интерфейс, а не iniConfig, — чтобы
// числа вида проверялись и в рантайм-фикстуре с подменённым конфигом.
struct SpeciesIniReader {
    virtual ~SpeciesIniReader() {}
    virtual float Float(const char* section, const char* key, float defValue) = 0;
    virtual bool  Bool (const char* section, const char* key, bool  defValue) = 0;
    // 86.03: управление автодопиской недостающих ключей. Нужно шагу D: зонд
    // «записан ли ключ?» обязан читать, НЕ дописывая в файл (иначе в ini
    // уедет NaN), а второе чтение — наоборот, дописать число карточки.
    // Чистые виртуальные намеренно: реализация, которая про них забыла, не
    // соберётся, а не будет молча писать мусор.
    virtual bool AutoBackfill() const = 0;
    virtual void SetAutoBackfill(bool on) = 0;
};

// Читатель, у которого автодописка управляема. Именно его принимает
// SpeciesBaseRangeEffective — чтобы нельзя было передать ридер без неё.
struct BackfillingIniReader : SpeciesIniReader {};

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
// 85.96: базовый (СПОКОЙНЫЙ) темп вида.
//
// ЧИСЛА ВИДА ЖИВУТ В КАРТОЧКЕ, ОБЩИЙ РЫЧАГ ИХ СДВИГАЕТ. Карточка задаёт, каким
// вид рождён относительно прочих (хоб неторопливее гоблина), а [monsterTempo]
// factorMin/Max остаётся ЖИВОЙ ручкой владельца над всей игрой: если её
// сдвинуть, база вида сдвигается на ту же величину, а не игнорирует ручку.
//
// Почему сдвиг, а не «карточка побеждает». Первый вариант этой правки брал
// просто пару из карточки — и общий рычаг молча переставал действовать на
// четыре вида с карточками: SetRange(0.80, 0.90) не замедлял волка вовсе.
// Владелец этим рычагом сравнивает сборки и гасит «слишком быстро», терять его
// нельзя (тот же принцип, что у силы: attackMult вида умножается на общий
// CombatStats, а не заменяет его).
//
// Почему сдвиг, а не пропорция. Сдвиг сохраняет РАЗБРОС внутри вида в
// абсолютных единицах (у хоба 0.13 по бегу) и даёт точные числа при ручке в
// поставочном положении: shipped 1.05..1.20 == kGlobalRef, сдвиг 0, числа
// карточки проходят как написаны.
//
// Отсчёт — от НИЖНЕЙ границы, не от середины. Владелец читает ручку как «пол
// темпа»: опустил factorMin с 1.05 до 0.80 — значит весь темп поехал вниз на
// 0.25, и волк (1.05..1.20) обязан стать 0.80..0.90, а не 0.775..0.925.
// Середина дала бы второе: сдвиг считался от 1.125 к 0.85, и числа перестали бы
// совпадать с тем, что написано в ini. Разброс при этом не меняется: ручка
// «шире/уже» (factorMax отдельно от factorMin) на полосу вида не влияет —
// ширина берётся из карточки.
//
// card == 0 допустим и означает «вид без карточки» — тогда глобальный диапазон
// как есть (так живёт 87 видов из 91, у которых карточки нет).
const float kGlobalLocoRefLo = 1.05f;   // поставочный [monsterTempo] factorMin
const float kGlobalLocoRefHi = 1.20f;   // поставочный [monsterTempo] factorMax
const float kGlobalAnimRefLo = 1.05f;   // поставочный animFactorMin
const float kGlobalAnimRefHi = 1.15f;   // поставочный animFactorMax

inline void SpeciesBaseRangeFor(const SpeciesCard* card,
                                float globalLocoLo, float globalLocoHi,
                                float globalAnimLo, float globalAnimHi,
                                float* locoLo, float* locoHi,
                                float* animLo, float* animHi)
{
    // Перевёрнутая или нулевая пара в карточке — не повод дать телу мусорный
    // диапазон: откат на глобальный, как у вида без карточки.
    const bool has = card && card->locoHi > card->locoLo
                           && card->animHi > card->animLo;
    if (!has) {
        if (locoLo) *locoLo = globalLocoLo;
        if (locoHi) *locoHi = globalLocoHi;
        if (animLo) *animLo = globalAnimLo;
        if (animHi) *animHi = globalAnimHi;
        return;
    }
    const float dLoco = globalLocoLo - kGlobalLocoRefLo;
    const float dAnim = globalAnimLo - kGlobalAnimRefLo;
    if (locoLo) *locoLo = card->locoLo + dLoco;
    if (locoHi) *locoHi = card->locoHi + dLoco;
    if (animLo) *animLo = card->animLo + dAnim;
    if (animHi) *animHi = card->animHi + dAnim;
}

// 86.03, шаг D: БАЗА ТЕМПА ВИДА КАК КЛЮЧ INI.
//
// Зачем. Карточка вида лежит в КОДЕ (`SpeciesCard.h`): чтобы поправить
// спокойный темп хоба, нужна пересборка и переупаковка. Владелец просил
// «карточку, которую можно твикнуть в любой момент» — этот шаг её даёт, не
// вынося в ini всё подряд: четыре числа на вид, и только у четырёх видов, у
// которых карточка вообще есть.
//
// Правило, которое здесь обязательно сохранить: **если ключ вида записан, он
// АБСОЛЮТЕН — общий сдвиг [monsterTempo] к нему уже не применяется.** Иначе
// число получило бы два рычага сразу (карточка/ключ + общая ручка), и
// предсказать результат в голове стало бы нельзя. Именно поэтому ключи
// дописываются в ini числом КАРТОЧКИ, а не сдвинутым значением: записанный
// ключ означает «вид живёт здесь», а не «вид сдвинут на столько-то».
//
// Следствие, которое надо знать: после первого запуска ключи уже в файле, и
// общая ручка [monsterTempo] factorMin/animFactorMin на эти четыре вида больше
// не действует (на остальные 87 видов без карточки — действует как раньше).
// Чтобы вернуть виду подчинение общей ручке, ключ его секции надо удалить.
// При ненулевом общем сдвиге об этом уходит строка в лог, чтобы не было
// молчаливого сюрприза.
//
// Ключи (секция [species.<kind>]): baseLocoMin, baseLocoMax, baseAnimMin,
// baseAnimMax. Отсутствующий ключ = прежнее поведение (карточка + общий
// сдвиг). Ключи независимы: можно вписать только baseAnimMax.
// Мусор и перевёрнутая пара чинятся тем же зажимом, что и ярость
// (loco 0.75..1.30, anim 0.70..1.40).
void SpeciesBaseRangeEffective(BackfillingIniReader& ini, const SpeciesCard* card,
                               float globalLocoLo, float globalLocoHi,
                               float globalAnimLo, float globalAnimHi,
                               float* locoLo, float* locoHi,
                               float* animLo, float* animHi);

SpeciesTempoNumbers SpeciesTempoFromIni(SpeciesIniReader& ini,
                                        const SpeciesCard& card,
                                        float baseLocoMin, float baseLocoMax,
                                        float baseAnimMin, float baseAnimMax);

} // namespace MonsterAI

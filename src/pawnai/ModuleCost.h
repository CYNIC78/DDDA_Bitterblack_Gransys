#pragma once
// 86.15: УЧЁТ ВРЕМЕНИ ЦЕПОЧКИ МОДУЛЕЙ UpdatePawnAI.
//
// ЗАЧЕМ. Три поля подряд боевой рекорд такта не разбирается: 86.10 — 5.16 с,
// 86.11 — 1.48 с, 86.14 — 1.11 с при 14 актёрах. Каждый новый прибор показывал,
// что виноват НЕ он: в поле 86.14 на такте в 1125 мс было scanUs=358 мкс и
// pollKb=256, то есть 99.97 % такта уходит в цепочку из двадцати модулей, а
// смотреть там нечем. Этот счётчик закрывает ровно эту дыру.
//
// ПОЧЕМУ НЕ RAII. В UpdatePawnAI есть __try, а объект с деструктором в такой
// функции — ошибка MSVC C2712. Поэтому учёт явный: ModEnterAt() закрывает
// предыдущий модуль и открывает следующий, вызов с kModNone закрывает
// последний. Если модуль бросил исключение и его перехватил SEH, время до
// следующего входа останется за ним — честнее, чем потерять замер.
//
// ПОЧЕМУ ХЕДЕР БЕЗ ПЛАТФОРМЫ. Логика отделена от часов: продукт передаёт
// микросекунды с QueryPerformanceCounter, фикстура — свои числа, которые можно
// двигать руками. Иначе накопление и сортировку нельзя было бы исполнить в
// тесте, а непроверенный счётчик стоит ровно столько же, сколько его отсутствие.
#include <stdint.h>
#include <stdio.h>

namespace PawnAI {

enum {
    kModNone = -1,
    kModWorldScan = 0, kModPackObserve, kModCardRecon, kModPartyStatus,
    kModPossession,  kModCombatIntel,  kModInclSelf,  kModEntityCfg,
    kModEnemyTuner,  kModMonsterAI,    kModAggro,     kModHaste,
    kModDashWatch,   kModWandRange,    kModErrata,    kModGuardian,
    kModNexus,       kModRescue,       kModOrderWatch, kModOrchestrator,
    kModCount
};

struct ModCost { const char* name; uint64_t us; uint32_t calls; uint32_t worstUs; };

inline ModCost* ModTable()
{
    static ModCost t[kModCount] = {
        { "WorldScan",    0, 0, 0 }, { "PackObserve", 0, 0, 0 },
        { "CardRecon",    0, 0, 0 }, { "PartyStatus", 0, 0, 0 },
        { "Possession",   0, 0, 0 }, { "CombatIntel", 0, 0, 0 },
        { "InclSelf",     0, 0, 0 }, { "EntityCfg",   0, 0, 0 },
        { "EnemyTuner",   0, 0, 0 }, { "MonsterAI",   0, 0, 0 },
        { "Aggro",        0, 0, 0 }, { "Haste",       0, 0, 0 },
        { "DashWatch",    0, 0, 0 }, { "WandRange",   0, 0, 0 },
        { "Errata",       0, 0, 0 }, { "Guardian",    0, 0, 0 },
        { "Nexus",        0, 0, 0 }, { "Rescue",      0, 0, 0 },
        { "OrderWatch",   0, 0, 0 }, { "Orchestr",    0, 0, 0 }
    };
    return t;
}

inline int& ModCur()  { static int c = kModNone; return c; }
inline uint64_t& ModT0() { static uint64_t t = 0; return t; }

inline void ModuleCostReset()
{
    ModCost* t = ModTable();
    for (int i = 0; i < kModCount; ++i) { t[i].us = 0; t[i].calls = 0; t[i].worstUs = 0; }
    ModCur() = kModNone;
    ModT0() = 0;
}

// idx = kModNone закрывает текущий модуль и больше ничего не открывает.
inline void ModEnterAt(int idx, uint64_t nowUs)
{
    ModCost* t = ModTable();
    const int cur = ModCur();
    if (cur >= 0 && cur < kModCount && nowUs >= ModT0()) {
        const uint32_t d = (uint32_t)(nowUs - ModT0());
        t[cur].us += d;
        if (d > t[cur].worstUs) t[cur].worstUs = d;
    }
    if (idx >= 0 && idx < kModCount) ++t[idx].calls;
    ModCur() = idx;
    ModT0() = nowUs;
}

// Топ-4 по СУММАРНОМУ времени: на вопрос «кто съедает такт» отвечает сумма, а
// не худший вызов. Худший печатаем рядом — по нему видно, один это всплеск или
// модуль тяжёл всегда. Возвращает число записанных байт.
inline int ModuleCostFormat(char* out, int cap)
{
    if (!out || cap <= 0) return 0;
    ModCost* t = ModTable();
    int order[kModCount];
    int n = 0;
    for (int i = 0; i < kModCount; ++i) if (t[i].us) order[n++] = i;
    for (int a = 1; a < n; ++a) {
        const int v = order[a];
        const uint64_t k = t[v].us;
        int j = a - 1;
        while (j >= 0 && t[order[j]].us < k) { order[j + 1] = order[j]; --j; }
        order[j + 1] = v;
    }
    int used = snprintf(out, (size_t)cap, "PawnAI: module-cost top:");
    if (used < 0) return 0;
    const int show = (n < 4) ? n : 4;
    for (int i = 0; i < show && used < cap - 1; ++i) {
        const ModCost& m = t[order[i]];
        const int r = snprintf(out + used, (size_t)(cap - used),
                               " %s=%llums(worst %ums n=%u)", m.name,
                               (unsigned long long)(m.us / 1000ull),
                               m.worstUs / 1000u, m.calls);
        if (r <= 0 || r >= cap - used) break;
        used += r;
    }
    return used;
}

} // namespace PawnAI

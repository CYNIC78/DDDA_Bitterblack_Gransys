// -*- 85.65: ФИКСТУРА ФИЛЬТРОВ ВИДА (живность и части составных врагов).
//
// ЧТО ПРОВЕРЯЕМ. Два списка из WorldScan.cpp — мирная живность и детали
// составных врагов. Ошибка здесь тихая и дорогая: лишний вид в списке живности
// перестаёт быть угрозой (пешки не отреагируют), а забытый вид живности проходит
// как враг (охота на оленя считается боем, счётчики врут). Поэтому проверяем
// границы списков по номерам из переписи видов:
//   живность: 8000, 8200, 8201, 8300, 8500, 8501, 8600, 8601, 8602, 8700;
//   детали:   8100, 7002;
// (85.65: олень/лань/вол переехали из «деталей» в «живность» — числа карточек
//  показали, что em8200/em8201/em8300 это Deer/Doe/Ox, а не части дракона.)
//   враги:    0100/0101 (гоблины), 0200 (волк), 0400 (зауриан), 0600 (харпия),
//             5800/5900 (драконы), 7000/7001 (Даймон), uHumanEnemy.
// Плюс варианты вида с подчёркиванием (uEm8500_00): в поле 19.08 в одной стае
// жили три записи гоблина, и точное сравнение имени теряло две трети стаи.
//
// ПОЧЕМУ ОТДЕЛЬНАЯ ПРОГРАММА. Проверка должна быть поведенческой, а не грепом:
// списки — это код, который решает, кого считать угрозой. Собираем WorldScan.cpp
// с тем же шимом, что и синтаксическая проверка, и вызываем сами функции.
// Лишние функции выкидывает линкер (--gc-sections), поэтому неиспользуемые
// внутренности файла тянуть за собой не нужно.
#ifndef DDDA_TEMPO_PORTABLE_FIXTURE
#define DDDA_TEMPO_PORTABLE_FIXTURE
#endif
#include "tempo_stdafx.h"
#include "../../src/runtime/WorldScan.cpp"

#include <assert.h>
#include <cstdio>
#include <cstring>
#include <iostream>

BYTE* codeBase = 0;
BYTE* codeEnd = 0;
DWORD g_tempoTestNow = 0;
IniConfigStub config;
std::ofstream logFile("/tmp/kindfilter_test.log");

static bool IsHarmless(const char* k)   { return Runtime::KindIsHarmless(k); }
static bool IsStructural(const char* k) { return Runtime::KindIsStructural(k); }
static bool IsEnemy(const char* k)      { return Runtime::KindIsEnemy(k); }

int main()
{
    // ── живность: не угроза, но по-прежнему существо ───────────────────────
    const char* harmless[] = { "uEm8000", "uEm8200", "uEm8201", "uEm8300",
                               "uEm8500", "uEm8501", "uEm8600", "uEm8601",
                               "uEm8602", "uEm8700" };
    for (unsigned i = 0; i < sizeof(harmless) / sizeof(harmless[0]); ++i) {
        assert(IsHarmless(harmless[i]));
        assert(!IsEnemy(harmless[i]));
        assert(Runtime::KindIsCreature(harmless[i]));   // масштаб/запись остаются
    }
    // варианты вида ловятся, соседние номера — нет
    assert(IsHarmless("uEm8500_00"));
    assert(IsHarmless("uEm8200_00"));
    assert(!IsHarmless("uEm850"));
    assert(!IsHarmless("uEm85000"));
    assert(!IsHarmless("uEm8600x"));
    // олень/вол — живность, а НЕ деталь: раньше было наоборот
    assert(IsHarmless("uEm8200") && !IsStructural("uEm8200"));
    assert(IsHarmless("uEm8300") && !IsStructural("uEm8300"));
    std::cout << "  wildlife list: ok (10 видов + варианты)" << std::endl;

    // ── детали составных врагов ────────────────────────────────────────────
    const char* structural[] = { "uEm8100", "uEm7002" };
    for (unsigned i = 0; i < sizeof(structural) / sizeof(structural[0]); ++i) {
        assert(IsStructural(structural[i]));
        assert(!IsEnemy(structural[i]));
    }
    assert(IsStructural("uEm8100_00"));
    assert(IsStructural("uEm7002_00"));
    // Соседи по номеру деталями НЕ стали: 8100 — деталь, 8000 — живность;
    // 7002 — деталь Даймона, 7000/7001 — сам Даймон (ниже, среди врагов).
    assert(!IsStructural("uEm8000") && IsHarmless("uEm8000"));
    assert(!IsStructural("uEm7000") && !IsStructural("uEm7001"));
    std::cout << "  structural list: ok (2 вида)" << std::endl;

    // ── настоящие враги не задеты ──────────────────────────────────────────
    const char* enemies[] = { "uEm0100", "uEm0100_0", "uEm0101", "uEm0200",
                              "uEm0400", "uEm0600", "uEm5800", "uEm5900",
                              "uEm7000", "uEm7001", "uHumanEnemy" };
    for (unsigned i = 0; i < sizeof(enemies) / sizeof(enemies[0]); ++i) {
        assert(IsEnemy(enemies[i]));
        assert(!IsHarmless(enemies[i]));
        assert(!IsStructural(enemies[i]));
    }
    // Соседние номера не должны перепутаться: 7000 — Даймон, 7002 — его деталь;
    // 5800 — настоящий Григори (в бестиарии он под 0x54, и «ловушка зайцев»
    // в CombatIntel ловится по имени класса, а не по gid).
    assert(IsEnemy("uEm7000") && IsStructural("uEm7002"));
    assert(IsEnemy("uEm5800") && !IsHarmless("uEm5800"));
    std::cout << "  real enemies untouched: ok" << std::endl;

    // ── «не существо» остаётся не-существом ────────────────────────────────
    assert(!IsEnemy("uNpc") && !IsEnemy(0) && !IsHarmless("uNpc"));
    std::cout << "  non-creatures: ok" << std::endl;

    std::printf("kind filters: PASS (wildlife 10, structural 2)\n");
    return 0;
}

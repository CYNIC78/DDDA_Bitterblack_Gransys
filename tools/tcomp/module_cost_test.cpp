// 86.15: УЧЁТ ВРЕМЕНИ ЦЕПОЧКИ МОДУЛЕЙ — исполняемая проверка.
//
// ЗАЧЕМ ИСПОЛНЯЕМАЯ, А НЕ ГРЕП. Счётчик ставится под третий неразобранный
// максимум подряд (86.10 — 5.16 с, 86.11 — 1.48 с, 86.14 — 1.11 с при scanUs=358
// мкс). Прибор, который врёт, стоит ровно столько же, сколько его отсутствие, —
// а решения по нему принимаются так же, как по гистограмме состава в 86.10.
//
// КАК. Логика учёта вынесена в src/pawnai/ModuleCost.h и не зависит от
// платформы: продукт передаёт микросекунды с QueryPerformanceCounter, здесь —
// свои числа, которые можно двигать руками. Проверяется настоящее накопление,
// настоящий худший вызов и настоящая сортировка топа.
#include "../../src/pawnai/ModuleCost.h"

#include <assert.h>
#include <string.h>
#include <iostream>

using namespace PawnAI;

static void TestAccumulatesAndCloses()
{
    ModuleCostReset();
    ModEnterAt(kModWorldScan, 0);
    ModEnterAt(kModMonsterAI, 358);      // WorldScan отработал 358 мкс
    ModEnterAt(kModNone, 1125358);       // MonsterAI съел остаток такта
    assert(ModTable()[kModWorldScan].us == 358);
    assert(ModTable()[kModWorldScan].calls == 1);
    assert(ModTable()[kModMonsterAI].us == 1125000);
    assert(ModTable()[kModMonsterAI].worstUs == 1125000);
    // После kModNone время не копится никуда: следующий вход — новый такт.
    ModEnterAt(kModNone, 2000000);
    assert(ModTable()[kModMonsterAI].us == 1125000);
    std::cout << "  accumulation: время уходит своему модулю и закрывается\n";
}

static void TestWorstIsNotTotal()
{
    ModuleCostReset();
    ModEnterAt(kModEnemyTuner, 0);
    ModEnterAt(kModNone, 1000);          // 1000 мкс
    ModEnterAt(kModEnemyTuner, 2000);
    ModEnterAt(kModNone, 7000);          // 5000 мкс
    ModEnterAt(kModEnemyTuner, 8000);
    ModEnterAt(kModNone, 8300);          // 300 мкс
    assert(ModTable()[kModEnemyTuner].calls == 3);
    assert(ModTable()[kModEnemyTuner].us == 6300);
    assert(ModTable()[kModEnemyTuner].worstUs == 5000);
    std::cout << "  worst: худший вызов виден отдельно от суммы\n";
}

static void TestBackwardsClockIsIgnored()
{
    ModuleCostReset();
    ModEnterAt(kModAggro, 5000);
    ModEnterAt(kModNone, 4000);          // часы пошли назад — мусора быть не должно
    assert(ModTable()[kModAggro].us == 0);
    assert(ModTable()[kModAggro].worstUs == 0);
    std::cout << "  clock: откат времени не даёт гигантского замера\n";
}

static void TestTopIsSortedByTotal()
{
    ModuleCostReset();
    // Нарочно не по порядку и не по алфавиту: сортировка обязана быть в топе.
    // Часы идут одним нарастающим потоком, как в настоящем такте.
    uint64_t t = 0;
    ModEnterAt(kModHaste, t);       t += 10000;   ModEnterAt(kModNone, t);
    ModEnterAt(kModMonsterAI, t);   t += 890000;  ModEnterAt(kModNone, t);
    ModEnterAt(kModAggro, t);       t += 100000;  ModEnterAt(kModNone, t);
    ModEnterAt(kModEnemyTuner, t);  t += 20000;   ModEnterAt(kModNone, t);
    ModEnterAt(kModCombatIntel, t); t += 5000;    ModEnterAt(kModNone, t);
    char l[240] = {};
    const int n = ModuleCostFormat(l, (int)sizeof(l));
    assert(n > 0);
    const char* a = strstr(l, " MonsterAI=890ms");
    const char* b = strstr(l, " Aggro=100ms");
    const char* c = strstr(l, " EnemyTuner=20ms");
    const char* d = strstr(l, " Haste=10ms");
    assert(a && b && c && d);
    assert(a < b && b < c && c < d);            // топ по убыванию суммы
    assert(!strstr(l, "CombatIntel"));           // пятый в топ-4 не попадает
    std::cout << "  top: " << l << "\n";
}

int main()
{
    TestAccumulatesAndCloses();
    TestWorstIsNotTotal();
    TestBackwardsClockIsIgnored();
    TestTopIsSortedByTotal();
    std::cout << "module cost: PASS (накопление, худший вызов, откат часов, "
                 "сортировка топа)\n";
    return 0;
}

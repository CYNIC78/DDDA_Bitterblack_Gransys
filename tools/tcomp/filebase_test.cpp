// tools/tcomp/filebase_test.cpp — поведенческая проверка таблицы баз вида
// (85.60: таблица из файлов игры; 85.64: политика по полям вместо отсева вида).
//
// Зачем. База боевых статов теперь берётся из файлов игры и умножается — значит
// ошибка в таблице сразу меняет бой у ВСЕХ видов, а не у одного. Владелец поля
// не увидит: он увидит только «стало легко/тяжело». Поэтому проверяем таблицу
// без игры: числа на месте, гоблин совпадает с полем, отсев небоевых работает,
// поиск по чужим видам возвращает пусто (а не «первую попавшуюся»).
//
// Фикстура включает продукт НАПРЯМУЮ, поэтому шимы (если понадобятся) должны
// стоять ДО include. Заголовку таблицы шимы не нужны: там только числа.
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../../src/runtime/EnemyFileBase.h"

int main()
{
    // 1. Гоблин: числа проверены полем — в логе 85.57 из памяти гоблина
    //    прочитаны ровно 250 / 75 / 80 / 75.
    const SpeciesFileBase* g = FindSpeciesFileBase(100);      // uEm0100 -> emId 100
    assert(g != 0);
    assert(g->atk == 250.0f && g->defC == 75.0f && g->mAtk == 80.0f && g->mDefC == 75.0f);
    assert(SpeciesFileBaseUsable(g));
    // У обычного вида политика пустая: в логе ничего не меняется.
    {
        char pol[80];
        SpeciesPolicyText(g, pol, sizeof(pol));
        assert(pol[0] == 0);
    }

    // 2. Волк и харпия — следующие в очереди видов; числа из файлов игры.
    const SpeciesFileBase* w = FindSpeciesFileBase(200);
    assert(w && w->atk == 220.0f && w->defC == 60.0f);
    const SpeciesFileBase* h = FindSpeciesFileBase(600);
    assert(h && h->atk == 350.0f && h->defC == 90.0f);

    // 3. Хобгоблин: отдельный вид, база вчетверо выше гоблина — если бы таблица
    //    «съехала» на один вид, тут было бы 250.
    const SpeciesFileBase* hob = FindSpeciesFileBase(101);
    assert(hob && hob->atk == 410.0f);

    // 4. Чужой/несуществующий вид: поиск обязан вернуть пусто, а не что попало.
    assert(FindSpeciesFileBase(65000) == 0);
    assert(FindSpeciesFileBase(0) == 0);

    // 5. 85.64: ПОЛИТИКА ПО ПОЛЯМ. Раньше вид с «небоевым» полем выбрасывался
    //    целиком и уходил на аварийный путь (память -> оценка). Теперь карточка
    //    принимается, а поля получают режим: immune (маркер 9000+) или absent
    //    (ноль). Проверяем три вида, из-за которых это и делалось.
    {
        const SpeciesFileBase* golem = FindSpeciesFileBase(5100);   // Golem
        assert(golem && golem->mDefC == 10000.0f);
        assert(SpeciesFileBaseUsable(golem));
        char pol[80];
        SpeciesPolicyText(golem, pol, sizeof(pol));
        assert(strcmp(pol, "mdef:immune") == 0);
        // Иммунитет остаётся иммунитетом: множитель к нему не применяется.
        assert(SpeciesFieldModeOf(golem->mDefC) == kSpeciesImmune);
        assert(SpeciesFieldModeOf(golem->atk) == kSpeciesWrite);
    }
    {
        const SpeciesFileBase* metal = FindSpeciesFileBase(5101);   // Metal Golem
        assert(metal && SpeciesFileBaseUsable(metal));
        char pol[80];
        SpeciesPolicyText(metal, pol, sizeof(pol));
        assert(strcmp(pol, "def:immune mdef:immune") == 0);
    }
    {
        const SpeciesFileBase* death = FindSpeciesFileBase(6003);   // Death
        assert(death && death->atk == 0.0f && death->mAtk == 0.0f);
        assert(death->defC == 666.0f && death->mDefC == 666.0f);
        assert(SpeciesFileBaseUsable(death));
        char pol[80];
        SpeciesPolicyText(death, pol, sizeof(pol));
        assert(strcmp(pol, "atk:absent matk:absent") == 0);
        // Ноль — это «нет поля», а не «слабое поле»: писать туда нельзя.
        assert(SpeciesFieldModeOf(death->atk) == kSpeciesAbsent);
        assert(SpeciesFieldModeOf(death->defC) == kSpeciesWrite);
    }
    {
        // Не-боец с нулями: тоже принимается (защита 75 в файле настоящая),
        // но атаки/магии у него нет.
        const SpeciesFileBase* scare = FindSpeciesFileBase(1200);
        assert(scare && SpeciesFileBaseUsable(scare));
        assert(SpeciesFieldModeOf(scare->atk) == kSpeciesAbsent);
    }

    // 6. Настоящие крупные бойцы не задеты: у em7001 атака 8500 — реальная,
    //    и режим у неё обычный (потолок «иммунитета» начинается с 9000).
    assert(SpeciesFileBaseUsable(FindSpeciesFileBase(7001)));
    assert(SpeciesFieldModeOf(FindSpeciesFileBase(7001)->atk) == kSpeciesWrite);
    assert(SpeciesFieldModeOf(FindSpeciesFileBase(7001)->defC) == kSpeciesWrite);

    // 7. Таблица непустая и без дублей emId (дубль = неоднозначный вид).
    assert(kSpeciesFileBaseCount > 80);
    for (int i = 0; i < kSpeciesFileBaseCount; ++i)
        for (int j = i + 1; j < kSpeciesFileBaseCount; ++j)
            assert(kSpeciesFileBase[i].emId != kSpeciesFileBase[j].emId);

    std::printf("  filebase: goblin 250/75/80/75, hob 410, wolf 220, harpy 350,"
                " политика полей (golem mdef:immune, death atk:absent),"
                " дублей нет (%d видов)\n",
                kSpeciesFileBaseCount);
    return 0;
}

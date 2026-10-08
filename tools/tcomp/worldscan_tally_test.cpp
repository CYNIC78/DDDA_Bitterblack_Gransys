// 86.10: СОСТАВ ТАБЛИЦЫ АКТЁРОВ — исполняемая проверка гистограммы.
//
// ЗАЧЕМ ФИКСТУРА, А НЕ ГРЕП. Поле 86.09 показало «actor table FULL at 80» в
// ванильной сессии, где врагов было 23. Поднимать kMaxAct дальше вслепую
// нельзя — 86.08 научил, что граница, поднятая без знания того, что она
// ограничивает, роняет игру. Поэтому в обход добавлена гистограмма: она
// отвечает на вопрос «чем заняты 80 слотов» одной строкой лога. Строка
// неверная = решение будет принято по неверным данным, поэтому считаем её
// здесь, на живом вызове, а не на совпадении текста в файле.
//
// КАК. WorldScan.cpp включается целиком (тот же приём, что в шаге 1j2 гейта),
// поэтому static-функции гистограммы видны этой единице трансляции напрямую.
// Недостижимые из main куски (сам обход и его ссылки на рантайм) вырезает
// линкер через --gc-sections — как в ranks_test.cpp.
#include "../../src/runtime/WorldScan.cpp"

#include <assert.h>
#include <string.h>
#include <iostream>

// 86.12: RewalkNeeded() читает два рантайм-глобала. В продукте их определяют
// другие единицы трансляции, здесь — свои: --gc-sections вырезает обход, и без
// этих двух строк проверка условия не слинкуется.
namespace Runtime {
int g_nAct = 0;
int g_nParty = 0;
ActorDump g_act[kMaxAct];
}

using Runtime::TallyReset;
using Runtime::TallyAdd;
using Runtime::TallyComposition;

static void TestCompositionOrder()
{
    char buf[400] = {};
    TallyReset();
    // Порядок вставки нарочно НЕ по убыванию: сортировка обязана быть в
    // гистограмме, а не в обходе.
    TallyAdd("uNpc");
    TallyAdd("uEm0101");
    TallyAdd("uNpc");
    TallyAdd("uEm0100");
    TallyAdd("uEm0101");
    TallyAdd("uPlayer");
    TallyAdd("uCmc");
    TallyAdd("uNpc");
    TallyAdd("uEm0101");
    TallyAdd("uEm0101");      // uEm0101 = 4, uNpc = 3: счёт обязан решить порядок
    TallyComposition(buf, sizeof(buf));
    std::cout << "  composition: " << buf << std::endl;
    assert(!strncmp(buf, "composition uEm0101=4", 21));  // самые частые впереди
    assert(strstr(buf, " uNpc=3") != 0);
    assert(strstr(buf, " uEm0100=1") != 0);
    assert(strstr(buf, " uPlayer=1") != 0);
    assert(strstr(buf, " uCmc=1") != 0);
    assert(strstr(buf, "more kinds") == 0);             // видов меньше восьми
    assert(strstr(buf, "tally full") == 0);
    // Порядок видов с РАВНЫМ счётом не гарантируем и не проверяем: он зависит
    // от порядка обхода, а он от состава мира.
}

static void TestEmptyKindAndReset()
{
    char buf[400] = {};
    TallyReset();
    TallyAdd("");        // пустое имя не должно ни упасть, ни дать пустой ключ
    TallyAdd(0);
    TallyAdd("uEm0100");
    TallyComposition(buf, sizeof(buf));
    assert(strstr(buf, "?=2") != 0);
    assert(strstr(buf, "uEm0100=1") != 0);
    TallyReset();
    TallyAdd("uEm0200");
    TallyComposition(buf, sizeof(buf));
    assert(strstr(buf, "uEm0200=1") != 0);
    assert(strstr(buf, "?=") == 0);      // сброс чистит и счётчики, и имена
}

static void TestMoreKindsThanShown()
{
    char buf[400] = {};
    TallyReset();
    const char* kinds[10] = { "uEm0100", "uEm0101", "uEm0200", "uEm0300",
                              "uEm0400", "uEm0500", "uEm0600", "uEm0700",
                              "uEm0800", "uEm0900" };
    for (int i = 0; i < 10; ++i) TallyAdd(kinds[i]);
    TallyComposition(buf, sizeof(buf));
    std::cout << "  composition (10 kinds): " << buf << std::endl;
    assert(strstr(buf, "+2 more kinds") != 0);   // восемь показано, два скрыто
}

static void TestTallyCap()
{
    char buf[400] = {};
    TallyReset();
    char name[16];
    for (int i = 0; i < Runtime::kKindTallyCap + 3; ++i) {
        snprintf(name, sizeof(name), "uEm%04d", 100 + i);
        TallyAdd(name);
    }
    TallyComposition(buf, sizeof(buf));
    std::cout << "  composition (cap): " << buf << std::endl;
    assert(strstr(buf, "(tally full at 32)") != 0);  // переполнение видно, не молчит
    assert(strstr(buf, "+24 more kinds") != 0);
    // 86.11: тела, которым не хватило ячейки, обязаны быть посчитаны. В поле
    // 86.10 гистограмма на 16 ячеек потеряла хвост молча: показанные восемь
    // видов дали 105 тел при 162 в проходе, и по такому составу решение
    // принимать было нельзя.
    assert(strstr(buf, " other=3") != 0);
}

// 86.12: КТО ЗАНИМАЕТ СЛОТ ТАБЛИЦЫ — белый список, а не чёрный.
//
// Поле 86.10: uOmObj11000=32 uOmSwingInstancing=32 uOmObj7515=11 против
// uEm0101=8, таблица полна на 94 % проходов. 86.11 отсек семейство «uO»
// (worldObjects=86660, droppedWorst 112 -> 40) — и на его место встало
// следующее: uFmSwingBase=46 uStageSplitMdl=25 uSkyGrass=5 uStageLowMdl=4,
// таблица снова полна на 1143 проходах из 1214. Чёрный список с движком не
// работает, поэтому проверяем именно белый.
static void TestActorTableAdmission()
{
    // Существа — остаются (и враги, и мирная живность: её видят те же потребители).
    assert(Runtime::KindBelongsInActorTable("uEm0100"));
    assert(Runtime::KindBelongsInActorTable("uEm0101_00"));   // вариант вида
    assert(Runtime::KindBelongsInActorTable("uEm5200_00"));   // голова химеры
    assert(Runtime::KindBelongsInActorTable("uEm8000"));      // лагерная мелочь
    assert(Runtime::KindBelongsInActorTable("uHumanEnemy"));  // бандиты/солдаты
    // Партия — остаётся: её ищут в снимке мира подписчики шины.
    assert(Runtime::KindBelongsInActorTable("uPlayer"));
    assert(Runtime::KindBelongsInActorTable("uCmc"));
    assert(Runtime::KindBelongsInActorTable("uNpc"));
    // Декорации обоих поколений — не остаются.
    assert(!Runtime::KindBelongsInActorTable("uOmObj11000"));
    assert(!Runtime::KindBelongsInActorTable("uOmSwingInstancing"));
    assert(!Runtime::KindBelongsInActorTable("uObjModel"));
    assert(!Runtime::KindBelongsInActorTable("uFmSwingBase"));
    assert(!Runtime::KindBelongsInActorTable("uStageSplitMdl"));
    assert(!Runtime::KindBelongsInActorTable("uStageLowMdl"));
    assert(!Runtime::KindBelongsInActorTable("uSkyGrass"));
    // Безымянное не остаётся: KindIsEnemy его и так не принимает, слот простаивал.
    assert(!Runtime::KindBelongsInActorTable("?"));
    assert(!Runtime::KindBelongsInActorTable("u?84"));
    assert(!Runtime::KindBelongsInActorTable(""));
    assert(!Runtime::KindBelongsInActorTable(0));
    // Ложные uEm* отсекаются ещё раньше (KindIsLiveEnemyBody), но белый список
    // обязан держать и эту границу сам: база и деталь — не тело.
    assert(Runtime::KindBelongsInActorTable("uEmDragonBase"));  // существо по имени
    std::cout << "  actor-table admission: существа и партия, декорации и «?» нет\n";
}

// 86.12: КОГДА ОБХОД ОБЯЗАН ИДТИ. Поле 86.12: белый список очистил таблицу от
// декораций, а семена обхода набираются из таблицы — и обход встал. Вторая
// половина ловушки: тяжёлый поллинг включается только при ПУСТОЙ партии, а
// партия была известна. За сессию passes=4, maxActors=0, directorWrites=0 — мод
// не видел ни одного монстра всю драку. Условие «таблица ИЛИ партия» и есть то,
// что не даёт этому повториться.
static void TestRewalkNeeded()
{
    Runtime::g_nAct = 0; Runtime::g_nParty = 0;
    assert(!Runtime::RewalkNeeded());   // нечего обходить: семян нет вовсе
    Runtime::g_nAct = 3; Runtime::g_nParty = 0;
    assert(Runtime::RewalkNeeded());    // есть актёры — есть семена
    Runtime::g_nAct = 0; Runtime::g_nParty = 4;
    assert(Runtime::RewalkNeeded());    // пустая таблица, но партия известна
    Runtime::g_nAct = 0; Runtime::g_nParty = 0;
    std::cout << "  rewalk gate: обход идёт и от одной партии\n";
}

// 86.14: КОГДА ПОЛЛИНГ ПАМЯТИ ОБЯЗАН ИСКАТЬ. Поле 86.13: обход починен
// (passes=1858, blindTicks=0), но за всю драку maxActors=4 — одна партия, и
// directorWrites=0. Поллинг останавливался по признаку «таблица не пуста», а
// партия в таблице есть всегда; лёгкий срез 256 КБ раз в 2 с покрывает кольцо
// 128 МБ за 17 минут, то есть за сессию в 6.6 минуты не нашёл бы ничего и за
// половину кольца. Признак обязан быть про существо, а не про пустоту.
static void TestSearchingForActors()
{
    memset(Runtime::g_act, 0, sizeof(Runtime::g_act));
    Runtime::g_nAct = 0; Runtime::g_nParty = 0;
    assert(Runtime::SearchingForActors());      // вообще ничего — ищем

    Runtime::g_nAct = 4; Runtime::g_nParty = 4;
    Runtime::g_act[0].ptr = 0x1000u; Runtime::g_act[0].kind = "uPlayer";
    Runtime::g_act[1].ptr = 0x2000u; Runtime::g_act[1].kind = "uCmc";
    Runtime::g_act[2].ptr = 0x3000u; Runtime::g_act[2].kind = "uCmc";
    Runtime::g_act[3].ptr = 0x4000u; Runtime::g_act[3].kind = "uNpc";
    assert(Runtime::SearchingForActors());      // одна партия — всё ещё ищем

    Runtime::g_nAct = 5;
    Runtime::g_act[4].ptr = 0x5000u; Runtime::g_act[4].kind = "uEm0100";
    assert(!Runtime::SearchingForActors());     // нашлось существо — поиск окончен

    Runtime::g_act[4].kind = "uHumanEnemy";
    assert(!Runtime::SearchingForActors());     // бандиты считаются тоже
    Runtime::g_act[4].kind = "uEm0101_00";
    assert(!Runtime::SearchingForActors());     // вариант вида тоже

    Runtime::g_nAct = 0; Runtime::g_nParty = 0;
    std::cout << "  search gate: ищем, пока нет ни одного существа\n";
}

int main()
{
    TestCompositionOrder();
    TestEmptyKindAndReset();
    TestMoreKindsThanShown();
    TestTallyCap();
    TestActorTableAdmission();
    TestRewalkNeeded();
    TestSearchingForActors();
    std::cout << "actor table tally: PASS (порядок, пустое имя, сброс, "
                 "скрытые виды, потолок, белый список актёров,\n"
                 "                  ворота обхода, ворота поиска)\n";
    return 0;
}

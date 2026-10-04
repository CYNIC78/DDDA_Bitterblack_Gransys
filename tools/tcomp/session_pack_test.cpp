// -*- 85.63: ФИКСТУРА ПОЛЕВОГО ПАКЕТА.
//
// ЧТО ПРОВЕРЯЕМ И ПОЧЕМУ ИМЕННО ЭТО. Пакет — механизм простой, но у него есть
// три свойства, которые ломаются молча:
//   1) пустая строка не создаёт «дырку» в блоке (сводка, которой не было, не
//      должна оставлять пустую строку — по ней потом гадаешь, что потерялось);
//   2) строки идут в порядке поступления (порядок выгрузки модулей) и счётчик
//      совпадает с числом строк;
//   3) при переполнении буфера строка НЕ режется пополам: попадает только
//      целиком, а факт усечения отмечается флагом. Обрезанная сводка в поле
//      хуже отсутствующей — её видно, но читается она как настоящая.
// Последнее проверить в игре нельзя (для этого надо 8 КБ сводок), а цена ошибки
// — неверный разбор сессии. Поэтому проверяем здесь.
#include "logmem_stdafx.h"
#include "../../src/runtime/LogMemSession.h"
#include "../../src/runtime/LogMem.cpp"

#include <assert.h>
#include <string.h>
#include <iostream>
#include <string>

// Доступ к буферу — только для фикстуры (см. LogMem.cpp, тот же макрос).
namespace LogMem {
const char* SessionPackForTests(int* linesOut, bool* cutOut);
// 85.64: подраздел откатов — тот же фикстурный доступ, что и к сводкам.
const char* RollbackPackForTests(int* linesOut, bool* cutOut);
}

static void TestEmptyIgnored()
{
    LogMem::SessionNote("");
    LogMem::SessionNote(0);
    int lines = -1; bool cut = true;
    const char* p = LogMem::SessionPackForTests(&lines, &cut);
    assert(lines == 0);
    assert(!cut);
    assert(p[0] == 0);
    std::cout << "  empty note ignored: ok" << std::endl;
}

static void TestOrderAndCount()
{
    LogMem::SessionNote("Tempo: rank summary total=15 uEm0100:5 7 2 1 0");
    LogMem::SessionNote("PackObserve: burn summary bodies=0 starts=0 survived=0 died=0 left=0");
    LogMem::SessionNote("LogMem: session fault-handling summary faults=1498 dumps=3 sites=3");
    int lines = -1; bool cut = true;
    const char* p = LogMem::SessionPackForTests(&lines, &cut);
    assert(lines == 3);
    assert(!cut);
    assert(strncmp(p, "Tempo: rank summary total=15", 28) == 0);
    const char* a = strstr(p, "burn summary bodies=0");
    const char* b = strstr(p, "fault-handling summary faults=1498");
    assert(a && b && a < b);
    // каждая строка заканчивается переводом строки ровно один раз
    const std::string s(p);
    assert(s.size() >= 2 && s[s.size() - 1] == '\n');
    assert(s.find("\n\n") == std::string::npos);
    std::cout << "  order and count: ok" << std::endl;
}

static void TestOverflowWholeLines()
{
    const int before = 3;
    char big[900];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    for (int i = 0; i < 20; ++i) LogMem::SessionNote(big);

    int lines = -1; bool cut = false;
    const char* p = LogMem::SessionPackForTests(&lines, &cut);
    assert(cut);
    assert(lines > before);
    assert(strlen(p) < 8192);
    assert(p[strlen(p) - 1] == '\n');

    // ни одной обрезанной строки: делим блок по '\n' и проверяем длины
    int n = 0;
    for (const char* q = p; *q; ++q) if (*q == '\n') ++n;
    assert(n == lines);
    const char* s = p;
    int bigCount = 0;
    while (*s) {
        const char* e = strchr(s, '\n');
        assert(e && "строка в блоке обязана заканчиваться переводом строки");
        const size_t len = (size_t)(e - s);
        assert(len == 899 || len < 200);   // либо целая большая, либо сводка
        if (len == 899) ++bigCount;
        s = e + 1;
    }
    assert(bigCount >= 5 && bigCount < 20);  // часть влезла, часть отброшена
    std::cout << "  overflow keeps whole lines: ok (" << lines << " lines, cut)" << std::endl;
}

static void TestRollbacks()
{
    // Пустая строка и nullptr не создают записей (как и у сводок).
    LogMem::SessionNoteRollback("");
    LogMem::SessionNoteRollback(0);
    int lines = -1; bool cut = true;
    const char* p = LogMem::RollbackPackForTests(&lines, &cut);
    assert(lines == 0 && !cut && p[0] == 0);

    LogMem::SessionNoteRollback("Errata[dagger-ban]: rolled back to vanilla (x2)");
    LogMem::SessionNoteRollback("PartyRecon: priority profile restored reason=DLL detach");
    LogMem::SessionNoteRollback("WandRange: restored (shutdown)");
    p = LogMem::RollbackPackForTests(&lines, &cut);
    assert(lines == 3 && !cut);
    assert(strstr(p, "Errata[dagger-ban]: rolled back") != 0);
    assert(strstr(p, "restored reason=DLL detach") != 0);
    assert(strstr(p, "WandRange: restored (shutdown)") != 0);
    const std::string s(p);
    assert(s[s.size() - 1] == '\n');
    std::cout << "  rollbacks gathered: ok" << std::endl;
}

static void TestRollbackOverflow()
{
    char big[400];
    memset(big, 'r', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    for (int i = 0; i < 12; ++i) LogMem::SessionNoteRollback(big);
    int lines = -1; bool cut = false;
    const char* p = LogMem::RollbackPackForTests(&lines, &cut);
    assert(cut);                       // не влезло — сказано прямо
    assert(strlen(p) < 2048);
    assert(p[strlen(p) - 1] == '\n');
    int n = 0;
    for (const char* q = p; *q; ++q) if (*q == '\n') ++n;
    assert(n == lines);
    std::cout << "  rollback overflow keeps whole lines: ok" << std::endl;
}

int main()
{
    TestEmptyIgnored();
    TestOrderAndCount();
    TestOverflowWholeLines();
    TestRollbacks();
    TestRollbackOverflow();
    // SessionFlush в фикстуре не зовём: он печатает в буфер лога, а под шимом
    // запись на диск не производится. Существование и вызов SessionFlush
    // проверяют отдельные шаги гейта (структурные проверки).
    std::cout << "session packet: PASS" << std::endl;
    return 0;
}

// 84.17: гейт живых карт гоблина (offline unit-тест).
//
// Лог 24 дал два паттерна: флаг +0x08 — (константа_карты) | младший_байт
// (1149272065 = 0x44808001, 1065353217 = 0x3F800001, ...) и чистые 0/1 на
// картах с нулевой константой. Функция гейта — чистая, тестируем её
// напрямую; остальной AggroWatch подлинкован на пустом мире.
#include "aggro_t.cpp"
#include <assert.h>

std::ofstream logFile("/tmp/goblin_fakehit_test.log");
BYTE** pBase = 0;
IniConfigStub config;

// 85.66: AggroWatch с 85.63 складывает сводки сессии через LogMem::SessionNote.
// LogMem живёт в ГЛОБАЛЬНОМ пространстве имён (см. LogMem.h) — стаб обязан быть
// там же, иначе линкуется Runtime::LogMem::SessionNote и ссылка остаётся висеть.
namespace LogMem {
void SessionNote(const char*) {}
}

namespace Runtime {
// Мир пуст: unit-тесты трогают только чистую функцию гейта.
int EnemyCount() { return 0; }
uintptr_t EnemyBodyAt(int, const char**) { return 0; }
uintptr_t ArisenBody() { return 0; }
bool PartyRecordInfo(int, int*, int*, uintptr_t*) { return false; }
int PartyRecordBodyClaimCount(int) { return 0; }
int PartyRecordPawnCount() { return 0; }
bool ReadLiveAct(uintptr_t, char*, int) { return false; }

namespace Mem {
bool InWorld() { return true; }
bool Rd(const void*, void*, size_t) { return false; }
bool RdPtr(const void*, uintptr_t*) { return false; }
bool WrSafe(void*, const void*, size_t) { return false; }
bool RegionOk(uintptr_t, size_t) { return false; }
bool LooksHeap(uintptr_t) { return false; }
bool NameOfLiveObject(uintptr_t, char*, int) { return false; }
}
}

int main()
{
    float cap = 0.0f, mx = 0.0f;
    // Живой гейт: младший бит флага, fC=4 восприятие (300), fC=5 бой (484).
    assert(Runtime::Aggro::LiveGoblinCardMode(0x44900001u, 4, &cap, &mx)
           && cap == 300.0f && mx == 320.0f);
    assert(Runtime::Aggro::LiveGoblinCardMode(0x44900001u, 5, &cap, &mx)
           && cap == 484.0f && mx == 520.0f);
    // Паттерны лога 24: константы карт + младший байт флага.
    assert(Runtime::Aggro::LiveGoblinCardMode(0x3F800001u, 5, &cap, &mx));
    assert(Runtime::Aggro::LiveGoblinCardMode(0xA8000001u, 4, &cap, &mx));
    assert(Runtime::Aggro::LiveGoblinCardMode(0xB6063701u, 4, &cap, &mx));
    // Нулевая константа — выглядит как волчий 0/1.
    assert(Runtime::Aggro::LiveGoblinCardMode(1u, 4, &cap, &mx) && cap == 300.0f);
    // Пустая / переходная / волчий боевой режим — fail-closed.
    assert(!Runtime::Aggro::LiveGoblinCardMode(0x44900000u, 5, &cap, &mx));
    assert(!Runtime::Aggro::LiveGoblinCardMode(0u, 0u, &cap, &mx));
    assert(!Runtime::Aggro::LiveGoblinCardMode(1u, 1u, &cap, &mx));
    assert(!Runtime::Aggro::LiveGoblinCardMode(1u, 2u, &cap, &mx));
    // ГЕЙТ ВИДА. 86.03: хоб ВЕРНУТ, и вот почему это важная проверка.
    //
    // В 86.02 хоб был отсюда исключён по выводу «гейт гоблинской карты у хоба
    // не проходит никогда» — его подсказали 20 аномалий «not live goblin head»
    // в логе 86.00, где «флаг» читался как float-координата. Вывод был
    // неправ: у тела несколько слотов ростера, гейт формы проверяется ПО СЛОТУ,
    // часть слотов отбивается, часть пишется. Прямое доказательство — 27 строк
    // «fakehit-signal ... first write @11125540», а 0x11125540 это uEm0101;
    // фейк-хит вызывается только после успешного пина. Исключение вида
    // выключило работающий механизм, то есть было регрессией.
    //
    // Проверка держит обе стороны: хоб обязан быть в гейте (иначе регрессия
    // вернётся), а шум от переходных слотов гасится охлаждением попыток, а не
    // исключением вида.
    assert(Runtime::Aggro::IsPinnableKind("uEm0200", false));  // волк: всегда
    assert(Runtime::Aggro::IsPinnableKind("uEm0200", true));
    assert(!Runtime::Aggro::IsPinnableKind("uEm0100", false)); // без директора — нет
    assert(Runtime::Aggro::IsPinnableKind("uEm0100", true));   // гоблин: проверен
    assert(Runtime::Aggro::IsPinnableKind("uEm0101", true));   // ХОБ: вернули (86.03)
    assert(!Runtime::Aggro::IsPinnableKind("uEm0101", false));
    assert(Runtime::Aggro::IsPinnableKind("uEm0400", true));   // ящер: 84.29
    assert(!Runtime::Aggro::IsPinnableKind(0, true));          // fail-closed
    assert(!Runtime::Aggro::IsPinnableKind("uEm5000", true));  // без карточки — нет
    fprintf(stderr, "Goblin card mode 84.17/86.03 fixture passed "
                    "(low-byte flag, fC4=300/fC5=484, fail-closed; "
                    "pinnable: wolf always, goblin/hob/saurian with director).\n");
    return 0;
}

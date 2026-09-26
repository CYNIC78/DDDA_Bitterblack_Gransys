// Syntax-check Runtime::PartyRecon, including Build 001 combat snapshot.
#include "director_stdafx.h"
// Локально держим только то, чего нет в shim/windows.h: PAGE_*/MEM_PRIVATE
// шим уже объявляет, а дубли роняли сборку под -Werror (redefinition).
#define __except(x) catch(...)
#define EXCEPTION_EXECUTE_HANDLER 1
#define VK_OEM_PLUS 0xBB
// InterlockedExchange/InterlockedCompareExchange больше не дублируем: они
// объявлены в shim/windows.h (там же, откуда их берут остальные фикстуры).
// Локальные копии давали redefinition и роняли syntax-gate на шаге 11 из 22,
// молча пропуская 11 следующих проверок (см. docs/PARKED.md, снято 85.23).
#include "../../src/runtime/PartyRecon.cpp"

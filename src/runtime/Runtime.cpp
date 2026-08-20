// Runtime — сборка продуктового слоя. См. Runtime.h.

#include "stdafx.h"
#include <stdlib.h>   // _set_invalid_parameter_handler
#include "Runtime.h"
#include "MemProbe.h"
#include "MonsterTempo.h"
#include "RuntimeInternal.h"

namespace Runtime {

// ---------------------------------------------------------------------------
// ЗАЩИТА ОТ САМОУБИЙСТВА CRT.
//
// Вылет 19.08 на глубоком обходе объектов пешки дал два урока. Второй
// такой: `sprintf_s` при переполнении буфера НЕ обрезает строку — он
// вызывает обработчик неверного параметра, а тот по умолчанию завершает
// процесс. То есть форматирование строки для лога способно уронить игру.
//
// В моде сотни вызовов sprintf_s с буферами фиксированного размера.
// Проверить глазами каждый нельзя, а цена промаха — вылет у игрока.
// Ставим свой обработчик: он считает случаи и молча возвращается, и
// тогда неудачное форматирование стоит испорченной строки в логе, а не
// закрытой игры.
static volatile LONG g_crtInvalidParams = 0;

static void __cdecl OnInvalidParameter(const wchar_t*, const wchar_t*,
                                       const wchar_t*, unsigned int, uintptr_t)
{
    InterlockedIncrement(&g_crtInvalidParams);
}

uint32_t CrtInvalidParamCount() { return (uint32_t)g_crtInvalidParams; }

void Init()
{
    // Первым делом — обработчик CRT: он должен стоять раньше любого
    // нашего форматирования строк.
    _set_invalid_parameter_handler(OnInvalidParameter);

    // Фундамент: база образа + границы секций. Всё остальное в рантайме
    // и в DevTools опирается на эти значения, поэтому строго первым.
    Mem::Init();

    // Priority-профили: sidecar-файл и его загрузка. Раньше это делал
    // Hooks::DevTools() под флагом [devtools] enabled — то есть у обычного
    // игрока профили не поднимались вообще.
    PartyPriorityProfileEnsureFile();
    PartyPriorityProfileLoadIfChanged();

    // Темп монстров: хуки движения. Ставятся только при однозначной
    // сигнатуре, иначе модуль остаётся выключенным.
    Tempo::Init();

    logFile << "Runtime: image base 0x" << std::hex << Mem::g_base
            << " size 0x" << Mem::g_imageSize << std::dec
            << "  exec sections " << Mem::g_nExec
            << "  rdata sections " << Mem::g_nRdata << std::endl;
}

void Shutdown()
{
    // Откат всех транзакционных правок правил. Только guarded rollback,
    // без ожиданий и join'ов — мы в DllMain.
    Tempo::Shutdown();
    PartyPriorityProfileRestoreAll("DLL detach");
    ErrataRestoreAll();
}

} // namespace Runtime

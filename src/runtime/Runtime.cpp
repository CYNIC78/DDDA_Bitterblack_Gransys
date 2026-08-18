// Runtime — сборка продуктового слоя. См. Runtime.h.

#include "stdafx.h"
#include "Runtime.h"
#include "MemProbe.h"
#include "RuntimeInternal.h"

namespace Runtime {

void Init()
{
    // Фундамент: база образа + границы секций. Всё остальное в рантайме
    // и в DevTools опирается на эти значения, поэтому строго первым.
    Mem::Init();

    // Priority-профили: sidecar-файл и его загрузка. Раньше это делал
    // Hooks::DevTools() под флагом [devtools] enabled — то есть у обычного
    // игрока профили не поднимались вообще.
    PartyPriorityProfileEnsureFile();
    PartyPriorityProfileLoadIfChanged();

    logFile << "Runtime: image base 0x" << std::hex << Mem::g_base
            << " size 0x" << Mem::g_imageSize << std::dec
            << "  exec sections " << Mem::g_nExec
            << "  rdata sections " << Mem::g_nRdata << std::endl;
}

void Shutdown()
{
    // Откат всех транзакционных правок правил. Только guarded rollback,
    // без ожиданий и join'ов — мы в DllMain.
    PartyPriorityProfileRestoreAll("DLL detach");
}

} // namespace Runtime

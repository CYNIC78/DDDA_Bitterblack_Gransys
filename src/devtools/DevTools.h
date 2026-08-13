#pragma once
/**
 * DevTools — глаза мода. Живёт внутри dinput8.dll, не отдельным инжектом.
 * SCAN ищет dword vtable в образе. DUMP разбирает sUnit / sSetManager.
 * HUNT снимает живой список. WorldScan_Tick дешёво обходит +0C и пишет WorldReport.
 * FactoryPointer из TSV — пустой слот.
 */
namespace TypeAtlas { struct Info; }

namespace Hooks {
    void DevTools();
}

namespace DevTools {
    uintptr_t ModuleBase();
    uintptr_t Rebase(uint32_t rva);                    // 0 если нет базы
    const TypeAtlas::Info* Identify(const void* ptr);  // по первому dword (vtable)
    void WorldScan_Tick();                             // 150 ms: rewalk +0C, PublishWorld
}

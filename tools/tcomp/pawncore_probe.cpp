// 85.24: проверка видимости вызовов, которые PawnAI.cpp делает из нового
// продуктового сброса (см. ProductWorldUnload + ворота записи в тике).
//
// ЗАЧЕМ ОТДЕЛЬНАЯ ПРОБА. PawnAI.cpp целиком под шим не собирается (в нём
// ImGui-панель и MSVC-специфичный stdafx), а смежные шаги syntax_check.sh
// проверяют только вырезанные UI-блоки. То есть новые вызовы в начале файла
// остались бы непроверенными, и опечатка в имени namespace стоила бы тестеру
// целой сборки. Здесь собраны РОВНО те заголовки и РОВНО те вызовы, что в
// PawnAI.cpp: если имя исчезнет или переедет, g++ скажет за секунду.
#include "shim/windows.h"

#include "../../src/EnemyTuner.h"
#include "../../src/runtime/AggroWatch.h"
#include "../../src/runtime/MemProbe.h"
#include "../../src/runtime/RuntimeInternal.h"
#include "../../src/pawnai/WandRange.h"

namespace PawnCoreProbe {

// Копия тела ProductWorldUnload — те же вызовы в том же порядке.
void ProductWorldUnload()
{
    EnemyTuner::OnWorldUnload();
    Runtime::Aggro::OnWorldUnload();
    Runtime::Mem::BlockWritesFor(1500);
    Runtime::Mem::BlockWritesFor(2500);
    (void)Runtime::Mem::WritesOpen();
    (void)Runtime::Mem::BlockedWrites();
    (void)Runtime::Mem::BlockedWindows();
    Runtime::WorldUnloadHooks hooks = {};
    hooks.onWorldUnload = 0;
    Runtime::SetWorldUnloadHooks(hooks);
    const uint32_t scanUs = Runtime::ScanGetStats().maxUs;
    (void)scanUs;
    PawnAI::WandRange::Restore("world unload");
}

} // namespace PawnCoreProbe

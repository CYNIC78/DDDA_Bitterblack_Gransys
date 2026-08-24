// Синтаксическая проверка MonsterDirector.cpp без MSVC.
#include "director_stdafx.h"
#include "../../src/monsterai/MonsterDirector.cpp"

// Wolf fixtures do not link PackObserve.cpp. Empty stubs keep Init/Shutdown
// / DumpSnapshot resolving without touching the 012 write path.
#ifndef DDDA_PACKOBSERVE_LINKED
namespace MonsterAI {
void PackObserveInit() {}
void PackObserveShutdown() {}
void PackObserveDump() {}
}
#endif

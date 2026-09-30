// Синтаксическая проверка MonsterDirector.cpp без MSVC.
#include "director_stdafx.h"

// 85.56: сторож живого чтения [ranks] спрашивает время изменения файла — тем же
// способом, что EntityConfig. Объявления держим ЗДЕСЬ, а не в общем шиме: они
// нужны ровно этому модулю (тот же приём и та же причина, что у entityconfig_t).
struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD    nFileSizeHigh, nFileSizeLow;
};
#define GetFileExInfoStandard 0
inline int GetFileAttributesExA(const char*, int, WIN32_FILE_ATTRIBUTE_DATA*) { return 0; }
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

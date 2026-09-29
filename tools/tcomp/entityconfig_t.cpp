// Синтаксическая проверка трёхуровневого конфига врагов (EntityConfig.cpp).
//
// ЗАЧЕМ. До 85.34 модуль не входил в гейт: его не покрывал ни один шаг, и
// ошибка в разборе нового ключа стоила бы владельцу целой итерации «сборка в
// VS — запуск игры — лог». Здесь он компилируется вместе с остальными.
//
// Недостающие объявления держим ЗДЕСЬ, а не в общем шиме: они нужны ровно
// одному модулю, а общий шим растёт осторожно (см. docs/PARKED.md, урок 85.23
// про «redefinition» от локальных дублей).
#include "director_stdafx.h"     // logFile, общий шим, iniConfig-заглушка
#include <string>
using std::string;               // в боевом stdafx это есть

typedef char CHAR;
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFFu)
#define GENERIC_WRITE 0x40000000u
#define CREATE_NEW    1u
inline DWORD  GetFileAttributesA(const char*) { return INVALID_FILE_ATTRIBUTES; }
inline int    WriteFile(void*, const void*, DWORD, DWORD*, void*) { return 0; }
struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
};
#define GetFileExInfoStandard 0u
inline int GetFileAttributesExA(const char*, int, WIN32_FILE_ATTRIBUTE_DATA*) { return 0; }

#include "iniConfig.h"           // настоящий класс из корня репозитория
#include "../../src/EntityConfig.cpp"

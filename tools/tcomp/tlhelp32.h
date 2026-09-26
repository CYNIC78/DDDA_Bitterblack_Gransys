#pragma once
// Заглушка <tlhelp32.h> для проверок под g++.
//
// ЗАЧЕМ. В 85.25 обработчик сбоев (LogMem.cpp) начал читать таблицу
// загруженных модулей, чтобы печатать «имя модуля + смещение» вместо
// бесполезного «outside known modules». Для этого понадобился Toolhelp32 —
// и файл перестал проходить проверку вовсе: заголовка в шиме нет, а MSVC
// ошибку не покажет только потому, что у него настоящий SDK.
//
// Здесь минимум, который нужен ровно для разбора файла: имена и поля те же,
// что в SDK. Поведение не моделируется — заглушка нужна компилятору, а не
// рантайму (настоящий код исполняется только в игре).

#include <windows.h>

#define TH32CS_SNAPMODULE   0x00000008
#define TH32CS_SNAPMODULE32 0x00000010
#define MAX_MODULE_NAME32   255

typedef struct tagMODULEENTRY32 {
    DWORD   dwSize;
    DWORD   th32ModuleID;
    DWORD   th32ProcessID;
    DWORD   GlblcntUsage;
    DWORD   ProccntUsage;
    BYTE*   modBaseAddr;
    DWORD   modBaseSize;
    HMODULE hModule;
    char    szModule[MAX_MODULE_NAME32 + 1];
    char    szExePath[MAX_PATH];
} MODULEENTRY32;
typedef MODULEENTRY32* LPMODULEENTRY32;

// Заглушки: снимок «пустой», ни одного модуля не вернут. Для проверки
// линковки и разбора типов этого достаточно.
inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return INVALID_HANDLE_VALUE; }
inline BOOL   Module32First(HANDLE, LPMODULEENTRY32) { return 0; }
inline BOOL   Module32Next(HANDLE, LPMODULEENTRY32) { return 0; }

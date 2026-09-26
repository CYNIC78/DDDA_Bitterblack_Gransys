#pragma once
// Заглушка MinHook для проверок под g++ (настоящий заголовок намеренно
// отказывается собираться вне x86/x64: "#error MinHook supports only x86 and x64").
//
// ЗАЧЕМ. Из-за этого #error ни один файл, включающий MinHook, нельзя было даже
// разобрать компилятором. Здесь только типы и объявления; ни одного тела —
// проверяем имена и синтаксис, а не поведение.

#include <windows.h>

typedef enum MH_STATUS {
    MH_UNKNOWN = -1,
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,
    MH_ERROR_DISABLED,
    MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,
    MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;

typedef void (WINAPI *MINHOOK_ORIG)(void);

MH_STATUS WINAPI MH_Initialize(void);
MH_STATUS WINAPI MH_Uninitialize(void);
MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*);
MH_STATUS WINAPI MH_EnableHook(LPVOID);
MH_STATUS WINAPI MH_DisableHook(LPVOID);
MH_STATUS WINAPI MH_RemoveHook(LPVOID);

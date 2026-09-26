#pragma once
// Шим stdafx для проверки src/runtime/LogMem.cpp под g++.
//
// ЗАЧЕМ. 25.09.2026 сборка MSVC упала на линковке:
//   error LNK2019: ссылка на неразрешенный внешний символ
//   "void __cdecl LogMem::SetWorkerThreadId(unsigned long)"
// Причина: определение попало в АНОНИМНОЕ пространство имён (файл его видел,
// другие файлы — нет). Компиляция при этом проходила: объявление в LogMem.h
// на месте, значит синтаксическая проверка такое не ловит В ПРИНЦИПЕ.
//
// Поэтому здесь не только разбор, но и ЛИНКОВКА (см. logmem_call.cpp): две
// отдельные единицы трансляции, как в жизни — LogMem.cpp даёт определение,
// вызывающий файл на него ссылается. Ошибка вида «объявление есть, тела нет»
// или «тело в анонимном пространстве» падает за секунду вместо сборки у
// владельца.
//
// logFile НЕ объявляем: его даёт настоящий runtime/LogMem.h (там std::ostream
// на своём буфере, а не файл). Именно из-за этого расхождения проверка не
// могла просто взять probe_stdafx.h.

#include <windows.h>
#include <fstream>
#include <string>
#include <math.h>
#include <cstring>
#include <tlhelp32.h>

// ---- недостающие имена Win32 (в SDK есть, в общем шиме нет) ----
#ifndef GENERIC_WRITE
#define GENERIC_WRITE 0x40000000
#endif
#ifndef CREATE_ALWAYS
#define CREATE_ALWAYS 2
#endif
#ifndef FILE_SHARE_WRITE
#define FILE_SHARE_WRITE 0x2
#endif
#ifndef EXCEPTION_CONTINUE_SEARCH
#define EXCEPTION_CONTINUE_SEARCH 0
#endif
#ifndef EXCEPTION_EXECUTE_HANDLER
#define EXCEPTION_EXECUTE_HANDLER 1
#endif

inline BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION* cs, DWORD)
{ cs->p = 0; return 1; }

inline BOOL WriteFile(HANDLE, const void*, DWORD, LPDWORD written, void*)
{ if (written) *written = 0; return 1; }

// ---- SEH/VEH: типы и константы, которых нет в общем шиме ----
#ifndef PVOID
#define PVOID void*
#endif
#ifndef ULONG_PTR
#define ULONG_PTR unsigned long
#endif
#ifndef EXCEPTION_ACCESS_VIOLATION
#define EXCEPTION_ACCESS_VIOLATION   0xC0000005
#endif
#ifndef EXCEPTION_ILLEGAL_INSTRUCTION
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001D
#endif
#ifndef EXCEPTION_STACK_OVERFLOW
#define EXCEPTION_STACK_OVERFLOW     0xC00000FD
#endif
#ifndef EXCEPTION_DATATYPE_MISALIGNMENT
#define EXCEPTION_DATATYPE_MISALIGNMENT 0x80000002
#endif
#ifndef EXCEPTION_INT_DIVIDE_BY_ZERO
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094
#endif

typedef struct _EXCEPTION_RECORD {
    DWORD    ExceptionCode;
    DWORD    ExceptionFlags;
    struct _EXCEPTION_RECORD* ExceptionRecord;
    PVOID    ExceptionAddress;
    DWORD    NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD;

typedef struct _CONTEXT {
    DWORD Eip, Eax, Ebx, Ecx, Edx, Esi, Edi, Ebp, Esp;
} CONTEXT;

typedef struct _EXCEPTION_POINTERS {
    EXCEPTION_RECORD* ExceptionRecord;
    CONTEXT*          ContextRecord;
} EXCEPTION_POINTERS;

typedef LONG (WINAPI *PTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS*);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;
typedef LONG (WINAPI *PVECTORED_EXCEPTION_HANDLER)(EXCEPTION_POINTERS*);

inline LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER)
{ return 0; }
inline PVOID AddVectoredExceptionHandler(ULONG, PVECTORED_EXCEPTION_HANDLER)
{ return 0; }
inline ULONG RemoveVectoredExceptionHandler(PVOID) { return 0; }

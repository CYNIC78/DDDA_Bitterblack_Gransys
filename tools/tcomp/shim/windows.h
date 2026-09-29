// Минимальный шим Win32 для синтаксической проверки под g++.
// Только то, что реально используется в проверяемых файлах.
// 84.96a: макросы min/max из настоящего windows.h в шим НЕ добавляем —
// они рвут сами headers libstdc++ (в отличие от MSVC-STL). Вместо них
// test_audio_redirect.sh статически проверяет, что std::max(/std::min(
// в коде НЕТ — MSVC с этими макросами такой код не компилирует (C2589).
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;
typedef unsigned short UINT16;
typedef unsigned int   UINT;
typedef int            BOOL;
typedef int            INT;
typedef long           LONG;
typedef unsigned long  ULONG;
typedef char*          LPSTR;
typedef const char*    LPCSTR;
typedef void*          LPVOID;
typedef const void*    LPCVOID;
typedef void*          HANDLE;
typedef void*          HMODULE;
typedef void*          HINSTANCE;
typedef void*          HWND;
typedef size_t         SIZE_T;
typedef intptr_t       LONG_PTR;
typedef uintptr_t      ULONG_PTR;
typedef long long      LONGLONG;
typedef unsigned long long ULONGLONG;
typedef wchar_t        WCHAR;
// 85.34: EntityConfig.cpp читает mtime конфига — FILETIME в шиме не было,
// поэтому весь модуль стоял вне синтаксического гейта.
typedef struct _FILETIME_ { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef const wchar_t* LPCWSTR;
typedef DWORD*         LPDWORD;

#define WINAPI
#define CALLBACK
#define TRUE  1
#define FALSE 0
#define MAX_PATH 260
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000

typedef struct _MEMORY_BASIC_INFORMATION {
    void*  BaseAddress;
    void*  AllocationBase;
    DWORD  AllocationProtect;
    SIZE_T RegionSize;
    DWORD  State;
    DWORD  Protect;
    DWORD  Type;
} MEMORY_BASIC_INFORMATION;

typedef struct _SYSTEM_INFO {
    DWORD dwPageSize;
    void* lpMinimumApplicationAddress;   // 84.95: нужен сканеру AudioRedirect
    void* lpMaximumApplicationAddress;
} SYSTEM_INFO;
inline void GetSystemInfo(SYSTEM_INFO*) {}
typedef union _LARGE_INTEGER { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct _SRWLOCK { void* p; } SRWLOCK;

inline void AcquireSRWLockExclusive(SRWLOCK*) {}
inline void ReleaseSRWLockExclusive(SRWLOCK*) {}
inline void AcquireSRWLockShared(SRWLOCK*) {}
inline void ReleaseSRWLockShared(SRWLOCK*) {}
#define SRWLOCK_INIT {0}

inline LPSTR lstrcpynA(LPSTR d, LPCSTR s, int n)
{ if (n <= 0) return d; strncpy(d, s, (size_t)n - 1); d[n - 1] = 0; return d; }
inline int   lstrlenA(LPCSTR s) { return (int)strlen(s); }
inline BOOL  IsBadReadPtr(LPCVOID, UINT) { return 0; }
inline SIZE_T VirtualQuery(LPCVOID, MEMORY_BASIC_INFORMATION*, SIZE_T) { return 0; }
inline BOOL  VirtualProtect(LPVOID, SIZE_T, DWORD, LPDWORD) { return 1; }
inline void* VirtualAlloc(void*, SIZE_T, DWORD, DWORD) { return 0; }
inline BOOL  VirtualFree(void*, SIZE_T, DWORD) { return 1; }
inline DWORD GetTickCount() { return 0; }
inline void  Sleep(DWORD) {}
// 85.34: заглушка возвращала 0 и НЕ форматировала строку. Для синтаксической
// проверки это было незаметно, но рантайм-фикстура конфига на этом молча теряла
// имена секций ([em0100] превращался в пустую строку) — тест «падал» на верном
// коде. Теперь форматирование настоящее.
template <class... A>
inline int   wsprintfA(LPSTR buf, LPCSTR fmt, A... a)
{
    return snprintf(buf, 256, fmt, a...);
}
inline BOOL  CreateDirectoryA(LPCSTR, void*) { return 1; }
inline DWORD GetModuleFileNameA(HMODULE, LPSTR, DWORD) { return 0; }

typedef short SHORT;
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
inline SHORT GetAsyncKeyState(int) { return 0; }

#ifndef sprintf_s
template <size_t N, typename... A>
inline int sprintf_s(char (&buf)[N], const char* fmt, A... a) { return snprintf(buf, N, fmt, a...); }
template <typename... A>
inline int sprintf_s(char* buf, size_t n, const char* fmt, A... a) { return snprintf(buf, n, fmt, a...); }
#endif

// ---- 84.95: дополнения для синтаксической проверки AudioRedirect.cpp ----
typedef void* LPSECURITY_ATTRIBUTES;
typedef void VOID;
typedef struct _CRITICAL_SECTION { void* p; } CRITICAL_SECTION;
inline void InitializeCriticalSection(CRITICAL_SECTION* cs) { cs->p = 0; }
inline void DeleteCriticalSection(CRITICAL_SECTION*) {}
inline void EnterCriticalSection(CRITICAL_SECTION*) {}
inline void LeaveCriticalSection(CRITICAL_SECTION*) {}
inline LONG InterlockedCompareExchange(volatile LONG* target, LONG value, LONG comparand)
{ LONG old = *target; if (*target == comparand) *target = value; return old; }
inline LONG InterlockedExchange(volatile LONG* target, LONG value)
{ LONG old = *target; *target = value; return old; }

#define MEM_PRIVATE 0x20000
#define MEM_MAPPED 0x40000
#define MEM_IMAGE 0x1000000
#define PAGE_READONLY 0x2
#define PAGE_READWRITE 0x4
#define PAGE_WRITECOPY 0x8
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define INVALID_FILE_SIZE ((DWORD)-1)
#define GENERIC_READ 0x80000000
#define FILE_SHARE_READ 0x1
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_BEGIN 0
#define CP_UTF8 65001

inline HANDLE CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) { return INVALID_HANDLE_VALUE; }
inline HANDLE CreateFileW(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) { return INVALID_HANDLE_VALUE; }
inline BOOL ReadFile(HANDLE, void*, DWORD, LPDWORD, void*) { return 0; }
inline DWORD GetFileSize(HANDLE, LPDWORD) { return 0; }
inline LONG SetFilePointer(HANDLE, LONG, LPDWORD, DWORD) { return 0; }
inline BOOL CloseHandle(HANDLE) { return 1; }
inline DWORD GetLastError() { return 0; }
inline DWORD GetCurrentThreadId() { return 0; }
inline DWORD GetCurrentProcessId() { return 0; }
inline HMODULE GetModuleHandleA(LPCSTR) { return (HMODULE)0; }
inline void* GetProcAddress(HMODULE, LPCSTR) { return 0; }
inline int WideCharToMultiByte(UINT, DWORD, const WCHAR*, int, char*, int, const char*, LPDWORD) { return 0; }
inline int MultiByteToWideChar(UINT, DWORD, const char*, int, WCHAR*, int) { return 0; }

// SEH под g++: g++ резервирует __try/__except лексически (макросы не
// работают) — преобразование __try->if(true), __except(..)->else делает
// tools/test_audio_redirect.sh (sed над временной копией, до компиляции).
#ifndef _BitScanForward
#define _BitScanForward(index, value) (*(index) = (unsigned long)__builtin_ctz(value))
#endif

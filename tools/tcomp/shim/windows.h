// Минимальный шим Win32 для синтаксической проверки под g++.
// Только то, что реально используется в проверяемых файлах.
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
typedef const wchar_t* LPCWSTR;
typedef DWORD*         LPDWORD;

#define WINAPI
#define CALLBACK
#define TRUE  1
#define FALSE 0
#define MAX_PATH 260
#define MEM_COMMIT 0x1000

typedef struct _MEMORY_BASIC_INFORMATION {
    void*  BaseAddress;
    void*  AllocationBase;
    DWORD  AllocationProtect;
    SIZE_T RegionSize;
    DWORD  State;
    DWORD  Protect;
    DWORD  Type;
} MEMORY_BASIC_INFORMATION;

typedef struct _SYSTEM_INFO { DWORD dwPageSize; } SYSTEM_INFO;
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
inline DWORD GetTickCount() { return 0; }
inline void  Sleep(DWORD) {}
inline int   wsprintfA(LPSTR, LPCSTR, ...) { return 0; }
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

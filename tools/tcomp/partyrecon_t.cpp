// Syntax-check Runtime::PartyRecon, including Build 001 combat snapshot.
#include "director_stdafx.h"
#define __except(x) catch(...)
#define EXCEPTION_EXECUTE_HANDLER 1
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_GUARD 0x100
#define MEM_PRIVATE 0x20000
#define VK_OEM_PLUS 0xBB
inline LONG InterlockedExchange(volatile LONG* p, LONG v) { LONG o = *p; *p = v; return o; }
inline LONG InterlockedCompareExchange(volatile LONG* p, LONG v, LONG c)
{ LONG o = *p; if (o == c) *p = v; return o; }
#include "../../src/runtime/PartyRecon.cpp"

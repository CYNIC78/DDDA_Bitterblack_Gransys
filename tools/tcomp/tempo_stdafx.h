#pragma once
// Portable MonsterTempo syntax/logic fixture precompiled-header shim.
#include <windows.h>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <math.h>

extern std::ofstream logFile;
extern BYTE** pBase;
extern DWORD g_tempoTestNow;
inline DWORD MsNow() { return g_tempoTestNow; }

typedef BYTE* LPBYTE;
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* v)
{ if (v) v->QuadPart = (LONGLONG)g_tempoTestNow * 1000; return TRUE; }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* v)
{ if (v) v->QuadPart = 1000000; return TRUE; }

struct IniConfigStub {
    bool  getBool(const char*, const char*, bool d) { return d; }
    float getFloat(const char*, const char*, float d) { return d; }
    int   getInt(const char*, const char*, int d) { return d; }
};
extern IniConfigStub config;

namespace Hooks {
inline void CreateHook(const char*, LPBYTE, void (*)(), LPVOID*, bool) {}
inline void SwitchHook(const char*, LPBYTE, bool) {}
}

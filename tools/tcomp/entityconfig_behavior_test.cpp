// Рантайм-проверка трёхуровневого конфига врагов (85.34).
//
// ЗАЧЕМ. Ключи боевых статов (в том числе новый adrenalineAtk — всплеск силы
// атаки по приказу директора) живут в ddda_entities.ini, а у этого файла до сих
// пор НЕ БЫЛО ни одного рантайм-теста: наследование [default] -> [class.*] ->
// [emXXXX] и зажим границ проверялись только глазами в поле. Здесь они
// проверяются на числах, без игры.
//
// КАК. Настоящий iniConfig тянет WinAPI-функции профилей, которых в песочнице
// нет, поэтому подменяем его простым словарём «секция|ключ» -> строка. Всё
// остальное — боевой src/EntityConfig.cpp: он включается как есть.
#include "director_stdafx.h"
#include <map>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cassert>
using std::string;

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

#include "iniConfig.h"

// logFile обязан существовать: модуль пишет в него подмены и отказы.
std::ofstream logFile("/tmp/entityconfig_behavior_test.log");

// ---------------------------------------------------------------- двойник ini ---
static std::map<string, string> g_ini;

static void IniSet(const char* section, const char* key, const char* value)
{
    g_ini[string(section) + "|" + key] = value;
}
static void IniClear() { g_ini.clear(); }

iniConfig::iniConfig(LPCSTR fileName) : fileName(fileName) {}

float iniConfig::getFloat(LPCSTR section, LPCSTR key, float defValue)
{
    std::map<string, string>::const_iterator it =
        g_ini.find(string(section) + "|" + key);
    if (it == g_ini.end()) return defValue;
    return (float)atof(it->second.c_str());
}
int iniConfig::getInt(LPCSTR section, LPCSTR key, int defValue)
{
    std::map<string, string>::const_iterator it =
        g_ini.find(string(section) + "|" + key);
    if (it == g_ini.end()) return defValue;
    return atoi(it->second.c_str());
}
bool iniConfig::getBool(LPCSTR section, LPCSTR key, bool defValue)
{
    std::map<string, string>::const_iterator it =
        g_ini.find(string(section) + "|" + key);
    if (it == g_ini.end()) return defValue;
    const string v = it->second;
    return v == "on" || v == "true" || v == "1";
}

#include "../../src/EntityConfig.cpp"

// ----------------------------------------------------------------------- тест ---
static bool Near(float a, float b) { return fabsf(a - b) < 0.00001f; }

int main()
{
    using namespace EntityCfg;

    // 1) Файл без адреналина: ключа нет -> наследуется 1.0 -> всплеска нет.
    IniClear();
    IniSet("global", "enabled", "on");
    IniSet("global", "allowWrites", "on");
    Load();
    assert(Near(For(100).adrenalineAtk, 1.0f));
    assert(Near(For(200).adrenalineMagick, 1.0f));
    assert(!AnyAdrenalineConfigured());

    // 2) [default]: значение достаётся ВСЕМ видам (гоблину и волку тоже).
    IniSet("default", "adrenalineAtk", "1.25");
    IniSet("default", "adrenalineMagick", "1.10");
    Load();
    assert(Near(For(100).adrenalineAtk, 1.25f));
    assert(Near(For(200).adrenalineAtk, 1.25f));
    assert(Near(For(200).adrenalineMagick, 1.10f));
    assert(AnyAdrenalineConfigured());

    // 3) [em0100] переопределяет только гоблина; волк остаётся на [default].
    IniSet("em0100", "adrenalineAtk", "1.45");
    Load();
    assert(Near(For(100).adrenalineAtk, 1.45f));
    assert(Near(For(200).adrenalineAtk, 1.25f));

    // 4) [class.small] между уровнями: 500 (огр) — группа large, её не касается.
    IniSet("class.small", "adrenalineAtk", "1.15");
    Load();
    assert(Near(For(200).adrenalineAtk, 1.15f));   // волк — small
    assert(Near(For(100).adrenalineAtk, 1.45f));   // вид сильнее группы
    assert(Near(For(500).adrenalineAtk, 1.25f));   // large: только [default]

    // 5) Границы. Выше потолка — зажимается (а не отбрасывается), ниже 1.0 —
    //    пол: ванильная сила атаки остаётся НИЖНИМ порогом.
    IniSet("em0100", "adrenalineAtk", "9.0");
    Load();
    assert(Near(For(100).adrenalineAtk, EntityCfg::kAdrenalineMax));
    IniSet("em0100", "adrenalineAtk", "0.5");
    Load();
    assert(Near(For(100).adrenalineAtk, 1.0f));

    // 6) Боевые множители (старые ключи) не затронуты правкой: 0.5..3.0 как были.
    IniSet("em0100", "attackMult", "2.0");
    Load();
    assert(Near(For(100).attackMult, 2.0f));
    IniSet("em0100", "attackMult", "7.0");
    Load();
    assert(Near(For(100).attackMult, 3.0f));

    printf("EntityConfig Build 85.34 adrenaline: PASS "
           "(наследование default/class/em, потолок 1.6, пол 1.0)\n");
    return 0;
}

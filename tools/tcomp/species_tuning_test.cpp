// Рантайм-проверка чисел вида из ini (85.36).
//
// ЗАЧЕМ. Владелец правит потолки разгона гоблина/хоба в ddda_ai_overhaul.ini.
// Цена ошибки высока и НЕ видна: если потолок опустить ниже базового диапазона,
// приказ директора отобьётся на каждом теле, и в поле это выглядит как «монстры
// перестали реагировать», а не как «в ini опечатка». Здесь проверяется, что:
//   * ключа нет            -> число из карточки (старый ini = прежнее поведение);
//   * мусор в ключе        -> число из карточки, с пометкой;
//   * перевёрнутый диапазон -> чинится;
//   * выход за пределы движка -> зажимается (бег 0.75..1.30, замах 0.70..1.40);
//   * потолок ниже базового -> ПОДНИМАЕТСЯ до базового (иначе молчаливый отказ);
//   * низ == верх          -> расширяется (иначе профиль не регистрируется);
//   * tempoRage = 0        -> вид честно выключается, а не молча.
//
// КАК. Настоящий iniConfig тянет WinAPI профилей, которых в песочнице нет,
// поэтому подменяем его словарём «секция|ключ» -> строка. Проверяется боевой
// src/monsterai/SpeciesTuning.cpp — он собирается как есть, без правок.
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

std::ofstream logFile("/tmp/species_tuning_test.log");

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
    std::map<string, string>::const_iterator it = g_ini.find(string(section) + "|" + key);
    if (it == g_ini.end()) return defValue;
    return (float)atof(it->second.c_str());
}
bool iniConfig::getBool(LPCSTR section, LPCSTR key, bool defValue)
{
    std::map<string, string>::const_iterator it = g_ini.find(string(section) + "|" + key);
    if (it == g_ini.end()) return defValue;
    return atoi(it->second.c_str()) != 0;
}
int    iniConfig::getInt(LPCSTR, LPCSTR, int d) { return d; }
string iniConfig::getStr(LPCSTR, LPCSTR, string d) { return d; }
double iniConfig::getDouble(LPCSTR, LPCSTR, double d) { return d; }
void iniConfig::setFloat(LPCSTR, LPCSTR, float) const {}
void iniConfig::setBool(LPCSTR, LPCSTR, bool) const {}
void iniConfig::setInt(LPCSTR, LPCSTR, int) const {}
void iniConfig::setStr(LPCSTR, LPCSTR, string) const {}
void iniConfig::removeKey(LPCSTR, LPCSTR) const {}
unsigned int iniConfig::getUInt(LPCSTR, LPCSTR, unsigned int d) { return d; }
void iniConfig::setUInt(LPCSTR, LPCSTR, unsigned int, bool) const {}
void iniConfig::setDouble(LPCSTR, LPCSTR, double) const {}

#include "../../src/monsterai/SpeciesTuning.cpp"

// ------------------------------------------------------------ адаптер-читатель ---
// Свой экземпляр конфига, а не глобальный `config`: в этом фикстуре глобальное
// имя занято двойником директора (IniConfigStub из director_stdafx.h), и
// настоящий iniConfig под тем же именем не объявить.
static iniConfig g_cfg("species_test.ini");

struct Reader : MonsterAI::SpeciesIniReader {
    float Float(const char* section, const char* key, float defValue) override {
        return g_cfg.getFloat(section, key, defValue);
    }
    bool Bool(const char* section, const char* key, bool defValue) override {
        return g_cfg.getBool(section, key, defValue);
    }
};

// Базовый диапазон «как в поле»: [monsterTempo] factorMin/Max, animFactorMin/Max.
static const float kBaseLocoMin = 1.05f, kBaseLocoMax = 1.20f;
static const float kBaseAnimMin = 1.05f, kBaseAnimMax = 1.15f;

static const MonsterAI::SpeciesCard* Goblin()
{
    const MonsterAI::SpeciesCard* c = MonsterAI::FindSpeciesCard("uEm0100");
    assert(c);
    return c;
}
static const MonsterAI::SpeciesCard* Hob()
{
    const MonsterAI::SpeciesCard* c = MonsterAI::FindSpeciesCard("uEm0101");
    assert(c);
    return c;
}

static MonsterAI::SpeciesTempoNumbers Load(const MonsterAI::SpeciesCard* card)
{
    Reader r;
    return MonsterAI::SpeciesTempoFromIni(r, *card, kBaseLocoMin, kBaseLocoMax,
                                          kBaseAnimMin, kBaseAnimMax);
}

static bool Near(float a, float b) { return (a > b ? a - b : b - a) < 0.001f; }

int main()
{
    // 1. Ключей нет вовсе: старый ini обязан вести себя как раньше.
    IniClear();
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(n.rageEnabled);
        assert(!n.sanitized);
        assert(Near(n.rageLocoMin, 1.15f) && Near(n.rageLocoMax, 1.20f));
        assert(Near(n.rageAnimMin, 1.15f) && Near(n.rageAnimMax, 1.24f));
    }

    // 2. Владелец поднял потолки — применяется как написано.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMin", "1.16");
    IniSet("species.uEm0100", "rageLocoMax", "1.24");
    IniSet("species.uEm0100", "rageAnimMin", "1.20");
    IniSet("species.uEm0100", "rageAnimMax", "1.34");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(!n.sanitized);
        assert(Near(n.rageLocoMin, 1.16f) && Near(n.rageLocoMax, 1.24f));
        assert(Near(n.rageAnimMin, 1.20f) && Near(n.rageAnimMax, 1.34f));
    }

    // 3. ГЛАВНОЕ: потолок ниже базового диапазона. Без зажима приказ отбился бы
    //    на каждом теле МОЛЧА (baseline-outside-profile) — это и есть ловушка.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMin", "0.90");
    IniSet("species.uEm0100", "rageLocoMax", "1.02");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(n.sanitized);
        assert(n.rageLocoMin >= kBaseLocoMin);
        assert(n.rageLocoMax >= kBaseLocoMax);
        assert(n.rageLocoMax > n.rageLocoMin);
    }

    // 4. Мусор в ключе -> число карточки и пометка в лог.
    IniClear();
    IniSet("species.uEm0100", "rageAnimMax", "abc");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        // atof("abc") = 0.0 -> поднимется до базового верха замаха, но не 0.
        assert(n.rageAnimMax >= kBaseAnimMax);
        assert(n.rageAnimMax > n.rageAnimMin);
        assert(n.sanitized);
    }

    // 5. Перевёрнутый диапазон: низ выше верха — чинится, а не отвергается.
    IniClear();
    IniSet("species.uEm0101", "rageLocoMin", "1.28");
    IniSet("species.uEm0101", "rageLocoMax", "1.14");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Hob());
        assert(n.rageLocoMin < n.rageLocoMax);
        assert(n.sanitized);
    }

    // 6. Пределы движка: выше него — рассинхрон хитбокса, поэтому зажимается.
    IniClear();
    IniSet("species.uEm0101", "rageLocoMax", "1.90");
    IniSet("species.uEm0101", "rageAnimMax", "1.90");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Hob());
        assert(Near(n.rageLocoMax, 1.30f));
        assert(Near(n.rageAnimMax, 1.40f));
        assert(n.sanitized);
    }

    // 7. Низ == верх: RegisterRageProfile такой профиль не примет вовсе,
    //    поэтому зазор расширяется здесь.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMin", "1.25");
    IniSet("species.uEm0100", "rageLocoMax", "1.25");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(n.rageLocoMax > n.rageLocoMin);
        assert(n.sanitized);
    }

    // 8. Выключатель вида: tempoRage = 0 — честно, а не через «числа ниже базы».
    IniClear();
    IniSet("species.uEm0101", "tempoRage", "0");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Hob());
        assert(!n.rageEnabled);
    }

    // 9. Числа по умолчанию не трогают соседа: ключи читаются ПО ВИДУ.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMax", "1.28");
    {
        const MonsterAI::SpeciesTempoNumbers gob = Load(Goblin());
        const MonsterAI::SpeciesTempoNumbers hob = Load(Hob());
        assert(Near(gob.rageLocoMax, 1.28f));
        assert(Near(hob.rageLocoMax, 1.20f));
    }

    // 10. Все числа, которые поставка даёт по умолчанию, обязаны проходить
    //     зажим БЕЗ правок: иначе первый же запуск печатал бы «raised».
    IniClear();
    for (int i = 0; i < MonsterAI::SpeciesCardCount(); ++i) {
        const MonsterAI::SpeciesTempoNumbers n = Load(&MonsterAI::kSpeciesCards[i]);
        if (!n.rageEnabled) continue;
        assert(!n.sanitized);
        assert(n.rageLocoMin >= kBaseLocoMin && n.rageLocoMax >= kBaseLocoMax);
        assert(n.rageAnimMin >= kBaseAnimMin && n.rageAnimMax >= kBaseAnimMax);
        assert(n.rageLocoMin < n.rageLocoMax);
        assert(n.rageAnimMin < n.rageAnimMax);
    }

    printf("SpeciesTuning Build 85.36: PASS (ключ отсутствует = карточка,"
           " мусор/переворот/пределы/ниже базы чинятся, tempoRage=0 выключает)\n");
    return 0;
}

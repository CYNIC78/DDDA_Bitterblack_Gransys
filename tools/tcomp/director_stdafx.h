#pragma once
// Шим stdafx для проверки продуктовых модулей верхнего слоя под g++.
#include <windows.h>
#include <fstream>
#include <string>
#include <map>   // 86.03: словарь ключей IniConfigStub
#include <vector>
#include <functional>
#include <math.h>
#include <string.h>
#include "probe_stdafx.h"   // logFile, MsNow


// Минимальный двойник iniConfig: нужен только набор геттеров/сеттеров,
// которыми пользуются модули.
struct IniConfigStub {
    // 86.03: у настоящего iniConfig есть этот переключатель (iniConfig.h:17),
    // и шаг D его дёргает. В фикстуре он просто хранится: дописки тут нет.
    bool autoBackfill = true;
    // 85.56: сторож живого чтения [ranks] спрашивает путь к ТОМУ ЖЕ файлу,
    // который читает мод. В фикстуре это просто имя.
    const char* Path() const { return "ddda_ai_overhaul.ini"; }
    // 85.33: пробам нужен ключ, включённый «как в поле» (parallelOrders). Сама
    // заглушка одна на весь фикстур, поэтому подмена разрешена РОВНО для одного
    // названного ключа — остальные продолжают возвращать своё умолчание.
    bool        forceBool = false;
    const char* forceKey = 0;
    bool        forceValue = true;
    // 86.03: forceBool рассчитан РОВНО на один ключ и сбрасывается сразу после
    // Init. Для состояния, которое обязано жить всё время работы фикстуры
    // (дефолт [monsterAI] enabled), нужен отдельный словарь: иначе FreshDirector
    // затирал бы подмену, которую тест выставил для parallelOrders.
    std::map<std::string, bool> boolKeys;
    bool  getBool(const char*, const char* key, bool d) {
        if (forceBool && forceKey && key && !strcmp(key, forceKey))
            return forceValue;
        if (key) {
            std::map<std::string, bool>::const_iterator it = boolKeys.find(key);
            if (it != boolKeys.end()) return it->second;
        }
        return d;
    }
    const char* forceFloatKey = 0;   // 85.34: как forceBool, но для float
    float       forceFloatValue = 1.0f;
    float getFloat(const char*, const char* key, float d) {
        if (forceFloatKey && key == forceFloatKey) return forceFloatValue;
        return d;
    }
    int   getInt(const char*, const char*, int d) { return d; }
    int   getEnum(const char*, const char*, int d,
                  std::pair<int, const char*>[], int) const { return d; }
    void  setBool(const char*, const char*, bool) const {}
    void  setFloat(const char*, const char*, float) const {}
    void  setInt(const char*, const char*, int) const {}
};
extern IniConfigStub config;

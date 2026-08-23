#pragma once
// Шим stdafx для проверки продуктовых модулей верхнего слоя под g++.
#include <windows.h>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <math.h>
#include "probe_stdafx.h"   // logFile, MsNow


// Минимальный двойник iniConfig: нужен только набор геттеров/сеттеров,
// которыми пользуются модули.
struct IniConfigStub {
    bool  getBool(const char*, const char*, bool d) { return d; }
    float getFloat(const char*, const char*, float d) { return d; }
    int   getInt(const char*, const char*, int d) { return d; }
    int   getEnum(const char*, const char*, int d,
                  std::pair<int, const char*>[], int) const { return d; }
    void  setBool(const char*, const char*, bool) const {}
    void  setFloat(const char*, const char*, float) const {}
    void  setInt(const char*, const char*, int) const {}
};
extern IniConfigStub config;

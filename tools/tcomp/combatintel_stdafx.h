#pragma once
// Шим stdafx для проверки src/CombatIntel.cpp под g++.
//
// ЗАЧЕМ. CombatIntel — это обработчик урона (кто по кому ударил) и первая
// линия отсева «свой или чужой». До сих пор файл не проверялся вообще: в нём
// панель на ImGui, а настоящий ImGui тянет DirectX. Ошибку в этом файле
// владелец ловил бы своей сборкой — так уже было с линковкой LogMem.
//
// Здесь только недостающие заглушки: ImGui (imgui.h) и MinHook (MinHook/).
// Всё остальное берётся из общего проbe-шима.
#include "probe_stdafx.h"
#include "imgui.h"

// Нужны объявления из dinput8.cpp; для разбора достаточно.
#ifndef LPBYTE
typedef BYTE* LPBYTE;
#endif

// В проекте сборка без UNICODE, поэтому зовут просто GetModuleHandle.
inline HMODULE GetModuleHandle(LPCSTR name) { return GetModuleHandleA(name); }

#ifndef VK_LBUTTON
#define VK_LBUTTON 0x01
#endif
#ifndef VK_RBUTTON
#define VK_RBUTTON 0x02
#endif

// Минимум из dinput8.h: поиск сигнатур и постановка хука. Объявления взяты
// оттуда дословно, включая шаблонную перегрузку (её и зовут — с массивом).
namespace Hooks {
    bool FindSignature(LPCSTR msg, BYTE* signature, size_t len, BYTE** offset);
    template <size_t len>
    bool FindSignature(LPCSTR msg, BYTE (&signature)[len], BYTE** offset)
    { return FindSignature(msg, signature, len, offset); }
    // ВНИМАНИЕ: здесь заглушка мягче, чем объявление в dinput8.h. MSVC спокойно
    // позволяет передать сюда имя функции (указатель на функцию -> void*), g++
    // так не умеет. Поэтому принимаем что угодно: цель проверки — имена и
    // синтаксис, а не контроль типов там, где компиляторы расходятся.
    inline void CreateHook(LPCSTR, ...) {}
}

// Регистрация панели (InGameUI.h). В проекте вызов идёт без префикса:
// в stdafx.h стоит using namespace InGameUI, поэтому и здесь без namespace.
inline void InGameUIAdd(void (*)()) {}

// Двойник iniConfig — те же геттеры/сеттеры, что в проекте (значения не важны).
struct IniConfigStub {
    // 86.03: у настоящего iniConfig есть этот переключатель (iniConfig.h:17),
    // шаг D его дёргает через SpeciesIniReader. В фикстуре просто хранится.
    bool autoBackfill = true;
    bool  getBool(const char*, const char*, bool d) { return d; }
    float getFloat(const char*, const char*, float d) { return d; }
    int   getInt(const char*, const char*, int d) { return d; }
    void  setBool(const char*, const char*, bool) const {}
    void  setFloat(const char*, const char*, float) const {}
    void  setInt(const char*, const char*, int) const {}
};
extern IniConfigStub config;

#pragma once
// Замена stdafx.h для синтаксической проверки под g++ (модуль AudioRedirect):
// настоящий stdafx тянет DirectX, Steam API и ImGui, которых в Linux-песочнице
// нет. logFile НЕ объявляем здесь — его даёт настоящий runtime/LogMem.h
// (std::ostream), который AudioRedirect.cpp включает сам.
#include <windows.h>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

using std::string;

namespace Hooks
{
// Подделка Hooks::CreateHook из dinput8.h (нужны только сигнатуры;
// template-перегрузка обязательна — вызовы передают (LPVOID*)&...).
inline void CreateHook(LPCSTR, LPVOID, LPVOID, LPVOID*, bool = true) {}
template <class T>
inline void CreateHook(LPCSTR, LPVOID, LPVOID, T**, bool = true) {}
}

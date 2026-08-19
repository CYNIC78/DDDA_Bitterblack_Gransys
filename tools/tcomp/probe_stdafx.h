#pragma once
// Замена stdafx.h для синтаксической проверки под g++: настоящий stdafx
// тянет DirectX, Steam API и ImGui, которых в Linux-песочнице нет.
#include <windows.h>
#include <fstream>
#include <string>
#include <math.h>
extern std::ofstream logFile;
inline DWORD MsNow() { return 0; }

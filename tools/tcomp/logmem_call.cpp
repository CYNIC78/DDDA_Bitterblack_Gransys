// Вызывающий файл для проверки ЛИНКОВКИ LogMem.
//
// Ровно те вызовы, что делают другие файлы проекта (имена и пространство имён
// один в один): dinput8.cpp (Init/FlushToDisk/VEH_Faults) и PawnAI.cpp
// (SetWorkerThreadId в старте потока, PeriodicFlush в тике). Если объявление
// в LogMem.h останется без определения — или определение уедет в анонимное
// пространство имён, как это и случилось 25.09.2026, — линковка упадёт здесь,
// а не в Visual Studio у владельца.
//
// DLL не собираем: линкуется обычная программа, цель — увидеть
// "undefined reference to `LogMem::SetWorkerThreadId(unsigned long)'".
#include "../../src/runtime/LogMem.h"

int main()
{
    LogMem::Init();
    LogMem::SetWorkerThreadId(GetCurrentThreadId());
    LogMem::PeriodicFlush(1500);
    const unsigned faults = LogMem::VEH_Faults();
    LogMem::FlushToDisk();
    return (int)(faults == 0u ? 0 : 0);
}

#pragma once
/**
 * Лог в оперативке с непрерывным фоновым сбросом на диск и защитой при краше.
 * std::endl больше не трогает диск (только перевод строки в буфер).
 * Писатели с тика пешек и с кадра F12 сериализуются критической секцией.
 */
#include <ostream>
#include <windows.h>

namespace LogMem {

void Init();
// Идемпотентно. Из DllMain detach и из фильтра краша безопасно:
// только CreateFile/WriteFile, без повторной кучи сверх уже собранного буфера.
void FlushToDisk();
// Периодический сброс в фоновом потоке, чтобы диск всегда содержал свежий лог.
void PeriodicFlush(DWORD intervalMs = 1500);

} // namespace LogMem

extern std::ostream logFile;

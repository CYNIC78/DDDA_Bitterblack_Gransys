#pragma once
/**
 * Лог в оперативке. На диск — один раз: выход из игры или необработанный краш.
 * std::endl больше не трогает диск (только перевод строки в буфер).
 * Писатели с тика пешек и с кадра F12 сериализуются критической секцией.
 */
#include <ostream>

namespace LogMem {

void Init();
// Идемпотентно. Из DllMain detach и из фильтра краша безопасно:
// только CreateFile/WriteFile, без повторной кучи сверх уже собранного буфера.
void FlushToDisk();

} // namespace LogMem

extern std::ostream logFile;

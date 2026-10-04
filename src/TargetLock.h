#pragma once
/**
 * TargetLock.h — автоприцел: в момент атаки доворачивает Восставшего по вектору
 * камеры (реализация — TargetLock.cpp, свои рабочие потоки).
 *
 * Жизненный цикл: TargetLock() поднимает хуки, TargetLockShutdown() останавливает
 * потоки и закрывает их ручки — иначе они переживут выгрузку мира.
 */

namespace Hooks
{
    void TargetLock();
    void TargetLockShutdown();
}

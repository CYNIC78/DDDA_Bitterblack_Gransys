#pragma once
/**
 * CameraPlus.h — тактическая камера (v2.2): тактический обзор, пауза кадра и
 * отключение авто-доворота камеры. Состояние и реализация — CameraPlus.cpp;
 * здесь только две точки жизненного цикла.
 *
 * Настройки — секция [camera] в ddda_ai_overhaul.ini, читаются на старте.
 */

namespace Hooks
{
    void CameraPlus();
    void CameraPlusShutdown();
}

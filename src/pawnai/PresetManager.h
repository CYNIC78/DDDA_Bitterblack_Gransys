#pragma once
#include "PawnAI_Common.h"
/**
 * PresetManager — отвечает за пресеты и плавные переходы
 * Слушает шину только если включен TacticalSwitch (он решает какой пресет активен)
 */
namespace PawnAI {

class PresetManager {
public:
    void Init(int presetIdx, float smooth);
    void ApplyInstant(float* incl, int idx);
    void ApplySmooth(float* incl, int idx);
    void OnTick(float* incl, int activePreset); // вызывается каждый кадр оркестратором

    int   presetIdx = 5;
    float smooth = 0.1f; // 0..1, в ApplySmooth используется (1 - smooth)
    bool  enabled = true;
    static const int COUNT = 6;
    static const InclPreset presets[COUNT];
};

}

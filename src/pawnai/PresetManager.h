#pragma once
#include "PawnAI_Common.h"

/**
 * PresetManager — отвечает за пресеты, пользовательские якоря и плавный лерп
 */
namespace PawnAI {

class PresetManager {
public:
    static const int COUNT = 7;
    static const InclPreset presets[COUNT];

    void Init(int presetIdx, float smooth);
    void LoadConfig();
    void SaveConfig();
    void CaptureLive(const float* liveIncl);
    void ResetDefaultAnchor();

    void ApplyInstant(float* incl, int idx);
    void ApplySmooth(float* incl, int idx);
    void OnTick(float* incl, int activePreset);

    int   presetIdx = 5;       // 0..6 (6 = Custom Anchor)
    float smooth = 0.10f;      // 0..1 (коэффициент плавности сглаживания)
    bool  enabled = true;

    // Пользовательские якоря (0..1000) для каждого наклона
    float customAnchor[I_COUNT] = { 750.0f, 400.0f, 500.0f, 700.0f, 750.0f, 350.0f, 350.0f, 400.0f, 250.0f, 700.0f };
};

}

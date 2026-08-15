#pragma once
#include "PawnAI_Common.h"

/**
 * PresetManager — якоря (ползунки) и пресеты-снапшоты.
 *
 * МОДЕЛЬ (v3.0): ползунки — истина. Пресет — кнопка «загрузить в ползунки».
 *
 *   anchor[]        — текущая цель (ползунки в UI). Всегда живая, всегда правда.
 *   presets[N]      — готовые профили. LoadPreset(idx) копирует их значения В anchor[].
 *   lastPresetIdx   — какой пресет загружен последним (для индикатора «Modified»).
 *
 * Никакого «режима Custom Anchor». Переключение пресета — это просто переезд
 * ползунков. Движение ползунка — это отклонение от пресета (modified).
 *
 * Модули (SmartUtil, TacticalSwitch) НЕ трогают anchor[] и НЕ трогают incl[].
 * Они возвращают delta[] в оркестратор, который складывает
 *   finalTarget = anchor + delta   (или override в кризисе)
 * и делает ОДИН лерп. Драки между модулями нет.
 */
namespace PawnAI {

class PresetManager {
public:
    static const int COUNT = 6;  // 0..5 настоящих профилей. Индекса 6 (Custom) больше нет.
    static const InclPreset presets[COUNT];

    void Init();
    void LoadPreset(int idx);       // пресет → anchor[] (переезд ползунков)
    void LoadConfig();              // anchor[] из ini (старые ключи [customAnchor])
    void SaveConfig();
    void CaptureLive(const float* liveIncl);
    void ResetDefaultAnchor();      // = LoadPreset(5) Balanced

    void ApplyInstant(float* incl);               // incl = anchor (кнопка «применить сейчас»)
    void ApplySmooth(float* incl, const float* target);  // единственный лерп оркестратора
    void GetBaseTarget(float* out) const;         // копия anchor[]
    bool IsModified() const;                      // ползунки ≠ последний пресет?

    float smooth = 0.10f;      // 0..1, плавность лерпа
    bool  enabled = true;      // мастер «использовать пресеты/якоря»

    // Текущая цель — ползунки. 0..1000.
    float anchor[I_COUNT] = { 700.0f, 500.0f, 500.0f, 600.0f, 800.0f, 450.0f, 450.0f, 400.0f, 400.0f, 600.0f };
    int   lastPresetIdx = 5;   // для индикатора Modified
};

}

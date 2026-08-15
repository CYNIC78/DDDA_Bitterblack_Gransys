#include "stdafx.h"
#include "PresetManager.h"

extern iniConfig config;

namespace PawnAI {

const InclPreset PresetManager::presets[COUNT] = {
    { "Boss Killer",   "Large monsters and bosses (Scather + Challenger)",  {900.0f, 350.0f, 450.0f, 700.0f, 800.0f, 300.0f, 300.0f, 300.0f, 250.0f, 850.0f} },
    { "Crowd Ctrl",    "Hordes of smaller enemies (Mitigator + Scather)",     {700.0f, 350.0f, 900.0f, 650.0f, 500.0f, 350.0f, 350.0f, 300.0f, 250.0f, 700.0f} },
    { "Tactical Sup",  "Support, healing and utility (Medicant + Util)",     {300.0f, 900.0f, 350.0f, 400.0f, 800.0f, 700.0f, 450.0f, 300.0f, 300.0f, 600.0f} },
    { "Ranged Hunter", "Mages and ranged snipers (Challenger + Util)",        {650.0f, 350.0f, 450.0f, 900.0f, 750.0f, 350.0f, 350.0f, 350.0f, 250.0f, 700.0f} },
    { "Explorer",      "Exploration and scouting out of combat (Pioneer)",   {300.0f, 300.0f, 300.0f, 300.0f, 550.0f, 350.0f, 400.0f, 850.0f, 800.0f, 400.0f} },
    { "Balanced",      "Versatile generalist profile",                       {700.0f, 500.0f, 500.0f, 600.0f, 800.0f, 450.0f, 450.0f, 400.0f, 400.0f, 600.0f} }
};

static const char* s_iniKeys[I_COUNT] = {
    "scather", "medicant", "mitigator", "challenger", "utilitarian",
    "guardian", "nexus", "pioneer", "acquisitor", "skillUse"
};

void PresetManager::Init() {
    // По умолчанию — Balanced в ползунки. LoadConfig() поверх прочитает ini.
    LoadPreset(5);
    LoadConfig();
}

// Пресет = снапшот. Копируем значения в ползунки — ползунки всегда правда.
void PresetManager::LoadPreset(int idx) {
    if (idx < 0 || idx >= COUNT) idx = 5;
    lastPresetIdx = idx;
    for (int i = 0; i < I_COUNT; i++) {
        anchor[i] = presets[idx].v[i];
    }
    SaveConfig();
}

void PresetManager::LoadConfig() {
    // Старые ключи [customAnchor] остаются читаемыми — это и есть якоря.
    for (int i = 0; i < I_COUNT; i++) {
        anchor[i] = config.getFloat("customAnchor", s_iniKeys[i], anchor[i]);
        if (anchor[i] < 0.0f) anchor[i] = 0.0f;
        if (anchor[i] > 1000.0f) anchor[i] = 1000.0f;
    }
    lastPresetIdx = config.getInt("pawnAI", "lastPreset", 5);
    if (lastPresetIdx < 0 || lastPresetIdx >= COUNT) lastPresetIdx = 5;
}

void PresetManager::SaveConfig() {
    for (int i = 0; i < I_COUNT; i++) {
        config.setFloat("customAnchor", s_iniKeys[i], anchor[i]);
    }
    config.setInt("pawnAI", "lastPreset", lastPresetIdx);
    config.setFloat("pawnAI", "smooth", smooth);
}

void PresetManager::CaptureLive(const float* liveIncl) {
    if (!liveIncl) return;
    for (int i = 0; i < I_COUNT; i++) {
        anchor[i] = liveIncl[i];
        if (anchor[i] < 0.0f) anchor[i] = 0.0f;
        if (anchor[i] > 1000.0f) anchor[i] = 1000.0f;
    }
    SaveConfig();
}

void PresetManager::ResetDefaultAnchor() {
    LoadPreset(5);  // Balanced
}

void PresetManager::ApplyInstant(float* incl) {
    if (!incl) return;
    for (int i = 0; i < I_COUNT; i++) {
        incl[i] = anchor[i];
    }
}

void PresetManager::ApplySmooth(float* incl, const float* target) {
    if (!incl || !target) return;
    float factor = (smooth > 0.0f) ? smooth : 0.05f;
    if (factor > 1.0f) factor = 1.0f;
    for (int i = 0; i < I_COUNT; i++) {
        incl[i] += (target[i] - incl[i]) * factor;
        if (incl[i] < 0.0f) incl[i] = 0.0f;
        if (incl[i] > 1000.0f) incl[i] = 1000.0f;
    }
}

void PresetManager::GetBaseTarget(float* out) const {
    if (!out) return;
    for (int i = 0; i < I_COUNT; i++) out[i] = anchor[i];
}

bool PresetManager::IsModified() const {
    const float* base = presets[lastPresetIdx].v;
    for (int i = 0; i < I_COUNT; i++) {
        float d = anchor[i] - base[i];
        if (d < 0) d = -d;
        if (d > 0.5f) return true;   // больше половины пункта — считаем modified
    }
    return false;
}

}

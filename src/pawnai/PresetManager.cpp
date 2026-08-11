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
    { "Balanced",      "Versatile generalist profile",                       {700.0f, 500.0f, 500.0f, 600.0f, 800.0f, 450.0f, 450.0f, 400.0f, 400.0f, 600.0f} },
    { "Custom Anchor", "User-defined custom inclination anchors",            {750.0f, 400.0f, 500.0f, 700.0f, 750.0f, 350.0f, 350.0f, 400.0f, 250.0f, 700.0f} }
};

static const char* s_iniKeys[I_COUNT] = {
    "scather", "medicant", "mitigator", "challenger", "utilitarian",
    "guardian", "nexus", "pioneer", "acquisitor", "skillUse"
};

void PresetManager::Init(int idx, float sm) {
    presetIdx = (idx >= 0 && idx < COUNT) ? idx : 5;
    smooth = (sm > 0.0f && sm <= 1.0f) ? sm : 0.10f;
    LoadConfig();
}

void PresetManager::LoadConfig() {
    for (int i = 0; i < I_COUNT; i++) {
        customAnchor[i] = config.getFloat("customAnchor", s_iniKeys[i], presets[6].v[i]);
        if (customAnchor[i] < 0.0f) customAnchor[i] = 0.0f;
        if (customAnchor[i] > 1000.0f) customAnchor[i] = 1000.0f;
    }
}

void PresetManager::SaveConfig() {
    for (int i = 0; i < I_COUNT; i++) {
        config.setFloat("customAnchor", s_iniKeys[i], customAnchor[i]);
    }
    config.setInt("pawnAI", "preset", presetIdx);
    config.setFloat("pawnAI", "smooth", smooth);
}

void PresetManager::CaptureLive(const float* liveIncl) {
    if (!liveIncl) return;
    for (int i = 0; i < I_COUNT; i++) {
        customAnchor[i] = liveIncl[i];
        if (customAnchor[i] < 0.0f) customAnchor[i] = 0.0f;
        if (customAnchor[i] > 1000.0f) customAnchor[i] = 1000.0f;
    }
    presetIdx = 6; // Automatically activate custom anchor mode
    SaveConfig();
}

void PresetManager::ResetDefaultAnchor() {
    for (int i = 0; i < I_COUNT; i++) {
        customAnchor[i] = presets[5].v[i]; // Balanced defaults
    }
    presetIdx = 6;
    SaveConfig();
}

void PresetManager::ApplyInstant(float* incl, int idx) {
    if (!incl || idx < 0 || idx >= COUNT) return;
    const float* target = (idx == 6) ? customAnchor : presets[idx].v;
    for (int i = 0; i < I_COUNT; i++) {
        incl[i] = target[i];
    }
}

void PresetManager::ApplySmooth(float* incl, int idx) {
    if (!incl || idx < 0 || idx >= COUNT) return;
    const float* target = (idx == 6) ? customAnchor : presets[idx].v;
    float factor = (smooth > 0.0f) ? smooth : 0.05f;
    if (factor > 1.0f) factor = 1.0f;
    for (int i = 0; i < I_COUNT; i++) {
        incl[i] += (target[i] - incl[i]) * factor;
    }
}

void PresetManager::OnTick(float* incl, int activePreset) {
    if (!enabled || !incl) return;
    ApplySmooth(incl, activePreset);
}

}

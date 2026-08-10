#include "stdafx.h"
#include "PresetManager.h"
namespace PawnAI {
const InclPreset PresetManager::presets[COUNT] = {
    {"Boss Killer",  "Крупные враги",       {900,350,450,700,800, 300,300,300,250, 850}},
    {"Crowd Ctrl",   "Группы врагов",        {700,350,900,650,500, 350,350,300,250, 700}},
    {"Tactical Sup", "Поддержка + тактика",   {300,900,350,400,800, 700,450,300,300, 600}},
    {"Ranged Hunter","Охота на магов",        {650,350,450,900,750, 350,350,350,250, 700}},
    {"Explorer",     "Вне боя",              {300,300,300,300,550, 350,400,850,800, 400}},
    {"Balanced",     "Универсал",            {700,500,500,600,800, 450,450,400,400, 600}},
};
void PresetManager::Init(int idx,float sm){ presetIdx=idx; smooth=sm; }
void PresetManager::ApplyInstant(float* incl,int idx){ for(int i=0;i<I_COUNT;i++) incl[i]=presets[idx].v[i]; }
void PresetManager::ApplySmooth(float* incl,int idx){
    for(int i=0;i<I_COUNT;i++) incl[i] += (presets[idx].v[i]-incl[i]) * (1.0f - smooth);
}
void PresetManager::OnTick(float* incl,int activePreset){
    if(!enabled) return;
    ApplySmooth(incl, activePreset);
}
}

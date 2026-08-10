#pragma once
/**
 * PawnAI_BusOrchestrator — тонкий оркестратор. Весь старый монолит PawnAI.cpp теперь
 * просто собирает модули в один тик и пишет в память.
 * 
 * Как добавить новый модуль (магия для тебя → 3 строки для меня):
 * 1. Создай src/pawnai/MyModule.h/.cpp, скопировав SmartUtilitarian
 * 2. Добавь поле в Orchestrator, вызови Init()/Process()
 * 3. Всё, он сам слушает шину-тренера!
 */
#include "PresetManager.h"
#include "SanitaryCordon.h"
#include "SmartUtilitarian.h"
#include "TacticalSwitch.h"

namespace PawnAI {
struct Orchestrator {
    PresetManager      presets;
    SanitaryCordon     sanitary;
    SmartUtilitarian   smartUtil;
    TacticalSwitch     tactical;

    void Init(){
        presets.Init(5, 0.1f);
        sanitary.Init();
        smartUtil.Init();
        tactical.Init();
    }
    void Shutdown(){
        sanitary.Shutdown();
        smartUtil.Shutdown();
        tactical.Shutdown();
    }
    void Tick(float* incl){
        if(!incl) return;
        // Порядок ВАЖЕН: санитарим → умный утил → тактика выбирает пресет → плавный лерп пресета
        sanitary.Process(incl);
        smartUtil.Process(incl);
        // Skill Use clamp
        if(incl[I_SKILL_USE]<300) incl[I_SKILL_USE]+=1.f;
        if(incl[I_SKILL_USE]>900) incl[I_SKILL_USE]-=1.f;

        // Пресет
        if(presets.enabled){
            int active = tactical.GetActivePreset(presets.presetIdx);
            presets.OnTick(incl, active);
        }
    }
};
}

#pragma once
/**
 * PawnAI_BusOrchestrator — тонкий оркестратор. Новая модель весов (v3.0).
 *
 * ОДИН ЛЕРП. Модули не пишут в incl[] и не трогают ползунки:
 *
 *   1. override (кризис)  →  finalTarget = override (Emergency, макс. приоритет)
 *   2. иначе              →  base = anchor (ползунки игрока)
 *                            + delta от модулей (SmartUtil, TacticalSwitch,
 *                            AcquisitorManager)
 *   3. один лерп incl → finalTarget
 *
 * Build 55: SanitaryCordon заменён на AcquisitorManager. Guardian/Nexus —
 * доктрины (CAT_DOCTRINE), кордон их больше не зажимает.
 *
 * Никакой драки между модулями: каждый возвращает поправку, оркестратор
 * складывает. Падение модуля (SEH) отключает ТОЛЬКО его.
 *
 * РАСШИРЯЕМОСТЬ (таргеты): когда раскопаем target-сущностей, появится
 * ещё один источник delta (SitRep/TargetScan). Добавить = +1 вызов
 * GetDelta в Tick. Контракт уже готов.
 */
#include "PresetManager.h"
#include "VocationCordon.h"
#include "AcquisitorManager.h"
#include "SmartUtilitarian.h"
#include "TacticalSwitch.h"

namespace PawnAI {

#define SAFE_MODULE(name, call)                         \
    do {                                                \
        if (!name.enabled) break;                       \
        __try {                                         \
            name.call;                                  \
        } __except(EXCEPTION_EXECUTE_HANDLER) {         \
            name.enabled = false;                       \
        }                                               \
    } while(0)

struct Orchestrator {
    PresetManager      presets;
    AcquisitorManager  acquisitor;   // бывший SanitaryCordon: только Acquisitor
    VocationCordon     cordon;       // Guardian по вокации: мили — да, дальний — нет
    SmartUtilitarian   smartUtil;
    TacticalSwitch     tactical;

    // --- кризисный оверрайд (Emergency). Модуль SitRep будет его ставить. ---
    bool   overrideArmed = false;
    float  overrideTarget[I_COUNT] = {};
    void SetOverride(const float* t){ if(t){ for(int i=0;i<I_COUNT;i++) overrideTarget[i]=t[i]; overrideArmed=true; } }
    void ClearOverride(){ overrideArmed=false; }

    // Для UI: последняя дельта модулей — показываем «как система дышит».
    float lastDelta[I_COUNT] = {};

    void Init(){
        presets.Init();
        acquisitor.Init();
        cordon.Init();
        smartUtil.Init();
        tactical.Init();
    }
    void Shutdown(){
        acquisitor.Shutdown();
        smartUtil.Shutdown();
        tactical.Shutdown();
    }
    void Tick(float* incl){
        if(!incl) return;

        // 1) Кризис — override рулит всем, кордон молчит (Guardian нужен поднятым!)
        if (overrideArmed) {
            presets.ApplySmooth(incl, overrideTarget);  // быстрый smooth задаётся в профиле
            return;
        }

        // 2) База = ползунки игрока
        float target[I_COUNT];
        presets.GetBaseTarget(target);

        // 3) Дельта модулей (каждый в своём SEH)
        float delta[I_COUNT] = {};
        SAFE_MODULE(smartUtil, GetDelta(target, delta));
        SAFE_MODULE(tactical,  GetDelta(target, delta));
        SAFE_MODULE(acquisitor, GetDelta(target, delta)); // бывший кордон: только Acquisitor
        // Вокационный кордон идёт ПОСЛЕДНИМ: он ставит потолок Guardian,
        // и его слово должно быть поверх ситуативных надбавок.
        SAFE_MODULE(cordon,    GetDelta(target, delta));

        // 4) finalTarget = base + delta
        for (int i = 0; i < I_COUNT; i++) {
            target[i] += delta[i];
            if (target[i] < 0.0f) target[i] = 0.0f;
            if (target[i] > 1000.0f) target[i] = 1000.0f;
            lastDelta[i] = delta[i];
        }

        // 5) Один лерп к отфильтрованной цели
        if (presets.enabled) {
            presets.ApplySmooth(incl, target);
        }
    }
};

#undef SAFE_MODULE

}

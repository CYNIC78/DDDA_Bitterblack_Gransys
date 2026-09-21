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
#include "OrderWatch.h"

namespace PawnAI {

// Audit 2026-09-21 fix C2713/C2712: MSVC запрещает смешивать __try и C++ try
// в одной функции. Раньше SAFE_MODULE делал __try + try{log} в Tick() —
// компилятор падал C2713. Теперь SEH изолирован в отдельной функции без
// C++ объектов, а лог — в Tick() без SEH.
namespace Detail {
template<typename Mod>
inline bool CallSEH(Mod& mod, float* target, float* delta) {
    __try {
        mod.GetDelta(target, delta);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        mod.enabled = false;
        return false;
    }
}
inline void LogFall(const char* name) {
    logFile << "PawnAI BusOrchestrator: module " << name << " SEH fall, disabled" << std::endl;
}
} // namespace Detail

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

        // 3) Дельта модулей (каждый в своём SEH, лог вне SEH — фикс C2713)
        float delta[I_COUNT] = {};
        if (smartUtil.enabled) {
            if (!Detail::CallSEH(smartUtil, target, delta))
                Detail::LogFall("smartUtil");
        }
        if (tactical.enabled) {
            if (!Detail::CallSEH(tactical, target, delta))
                Detail::LogFall("tactical");
        }
        if (acquisitor.enabled) {
            if (!Detail::CallSEH(acquisitor, target, delta))
                Detail::LogFall("acquisitor");
        }
        OrderWatch::GetDelta(target, delta); // тактический импульс приказов (decay 6s)
        // Вокационный кордон идёт ПОСЛЕДНИМ: он ставит потолок Guardian,
        // и его слово должно быть поверх ситуативных надбавок.
        if (cordon.enabled) {
            if (!Detail::CallSEH(cordon, target, delta))
                Detail::LogFall("cordon");
        }

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

}

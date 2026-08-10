#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
/**
 * TacticalSwitch — фаза 1.6. Слушает шину и выбирает какой пресет сейчас активен.
 * Возвращает activePresetIdx для PresetManager.
 */
namespace PawnAI {
class TacticalSwitch {
public:
    void Init();
    void Shutdown();
    bool enabled = true;
    int GetActivePreset(int userPreset) const; // вызывается оркестратором
private:
    int busId = -1;
    int lastCategory = -1;
    void onReport(const ::CombatReport& r);
};
}

#pragma once

namespace Hooks {
    void CombatIntel();
}

void  CombatIntel_Tick();
float GetCombatUtilitarianConfidence();
bool  IsInCombat();             // true = в бою (активные враги в ring buffer < COMBAT_TIMEOUT_SEC)
int   GetCombatEnemyCategory(); // 0=small, 1=medium, 2=large, 3=flying, 4=mage, 5=boss, -1=нет

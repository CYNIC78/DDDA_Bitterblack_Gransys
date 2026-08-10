#pragma once

namespace Hooks {
    void CombatIntel();
}

float GetCombatUtilitarianConfidence();
bool  IsInCombat();             // true = в бою (ring buffer не пуст)
int   GetCombatEnemyCategory(); // 0=small, 1=medium, 2=large, 3=flying, 4=mage, -1=нет

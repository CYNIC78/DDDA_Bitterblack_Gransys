#pragma once

namespace Hooks {
    void CombatIntel();
}

void  CombatIntel_Tick();
float GetCombatUtilitarianConfidence();
bool  IsInCombat();             // true = в бою (Build 62: урон + боевые действия врагов + цель пешки)
int   GetCombatEnemyCategory(); // 0=small, 1=medium, 2=large, 3=flying, 4=mage, 5=boss, -1=нет

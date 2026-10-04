#pragma once
/**
 * CombatIntel.h — «тренер» боя: сам читает бой и публикует его в CombatBus.
 * Поведение не решает — это работа модулей пешек через оркестратор.
 *
 * CombatIntel_Tick() идёт из цепочки PawnAI: раз в 150 мс обновляет LastReport
 * (кто в бою, сколько разных врагов, средняя изученность, категория). Здесь же
 * геттеры для потребителей вне шины — панель и тактические решения.
 */

namespace Hooks {
    void CombatIntel();
}

void  CombatIntel_Tick();
float GetCombatUtilitarianConfidence();
bool  IsInCombat();             // true = в бою (Build 62: урон + боевые действия врагов + цель пешки)
int   GetCombatEnemyCategory(); // 0=small, 1=medium, 2=large, 3=flying, 4=mage, 5=boss, -1=нет

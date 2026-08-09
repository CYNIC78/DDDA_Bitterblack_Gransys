#pragma once

namespace Hooks {
    void CombatIntel();
}

// Вызывается из PawnAI.cpp для получения реальной уверенности
float GetCombatUtilitarianConfidence();

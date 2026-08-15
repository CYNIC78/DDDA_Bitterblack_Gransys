#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
/**
 * TacticalSwitch — фаза 1.6, новая модель.
 *
 * Раньше: переключал АКТИВНЫЙ ПРЕСЕТ по категории врага.
 * Теперь: возвращает delta[] — ситуативную поправку к базе (ползункам).
 *   Игрок задаёт характер (ползунки), бой его адаптирует (delta).
 *   Пресет игрок выбирает сам; система его не переключает — это его выбор.
 *
 * МАСШТАБИРУЕМОСТЬ (таргеты!): когда раскопаем target-сущностей,
 * сюда добавится ещё один источник: «враг целится в игрока → сильнее
 * поправка на защиту/агрессию к нему». Категория — пока единственный
 * вход, но delta-контракт уже готов к расширению.
 */
namespace PawnAI {
class TacticalSwitch {
public:
    void Init();
    void Shutdown();
    bool enabled = true;
    // Поправка к целевым весам по текущей ситуации в бою.
    void GetDelta(const float* base, float* delta) const;
private:
    int busId = -1;
    int lastCategory = -1;
    void onReport(const ::CombatReport& r);
};
}

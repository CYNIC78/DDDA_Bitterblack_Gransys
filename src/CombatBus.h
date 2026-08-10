#pragma once
/**
 * CombatBus.h — ШИНА (мегафон тренера)
 * 
 * Идея (из твоих слов):
 *   CombatIntel = Тренер с мегафоном
 *   PawnAI-модули = Спортсмены, которые слушают bus и сами решают что делать
 * 
 * Это пробивает стену CheatEngine-одиночек: вместо одного монолитного PawnAI у нас
 * независимые модули-слушатели. Добавить новый модуль = подписаться на bus.
 * Никаких спагетти-вызовов.
 * 
 * Использование types.tsv здесь: bus передаёт богатый CombatReport, где gid уже
 * расшифрован через EnemyTypes.Generated.h (groupId + точный uEmName + vtableRVA).
 */

#include <stdint.h>
#include <functional>
#include <vector>

// ============ Типы сообщений ============

struct EnemyContact {
    uint8_t  groupId;
    const char* uEmName;
    uint32_t vtableRVA;
    float    knowledge01;
    bool     isUnknown;
    uint32_t lastSeenMs;
    bool     hitByPawn;   // задет пешкой (для отладки)
    bool     hitByPlayer; // задет игроком
};

struct CombatReport {
    bool     inCombat;
    int      distinctTypes;         // всего разных врагов (игрок+пешки)
    int      unknownTypes;
    float    avgKnowledge01;
    float    utilitarianConfidence;
    int      dominantCategory;
    EnemyContact enemies[8];        // объединённый список (игрок+пешки)
    int      enemyCount;
    uint32_t timestampMs;
    // Раздельно для отладки/тактики
    int      playerDistinct;        // сколько типов задел игрок
    int      pawnDistinct;          // сколько типов задели пешки
    int      playerHits;            // всего хитов от игрока (throttle 200мс)
    int      pawnHits;              // всего хитов от пешек
    EnemyContact enemiesFromPawns[4]; // топ-4 врага которых били пешки (понятнее чем было)
    int      pawnEnemyCount;
};

// ============ Шина ============

class CombatBus {
public:
    using Listener = std::function<void(const CombatReport&)>;

    static CombatBus& Instance() {
        static CombatBus inst;
        return inst;
    }

    // Подписаться (модуль PawnAI вызывает в своём Hooks::XXX())
    // Возвращает id для отписки
    int Subscribe(Listener fn) {
        listeners.push_back(fn);
        return (int)listeners.size()-1;
    }
    void Unsubscribe(int id){ if(id>=0 && id < (int)listeners.size()) listeners[id]=nullptr; }

    // Опубликовать (вызывает только CombatIntel)
    void Publish(const CombatReport& rpt){
        lastReport = rpt;
        for(auto &fn: listeners) if(fn) fn(rpt);
    }

    const CombatReport& LastReport() const { return lastReport; }

private:
    std::vector<Listener> listeners;
    CombatReport lastReport{};
};

// ============ Хелпер для PawnAI-модулей ============
// Пример использования в модуле:
//   int busId = CombatBus::Instance().Subscribe([](const CombatReport& r){
//       float conf = r.utilitarianConfidence; // 0..1
//       // меняем свои инклинации
//   });

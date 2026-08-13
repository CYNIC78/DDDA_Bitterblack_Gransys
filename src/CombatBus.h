#pragma once
/**
 * CombatBus.h — ШИНА (мегафон тренера)
 *
 * CombatIntel = тренер по УДАРУ. Publish() каждые 150 мс ЗАТИРАЕТ LastReport().
 * WorldScan   = тренер по ПРИСУТСТВИЮ. PublishWorld() — второй канал, hit его не топчет.
 *
 * PawnAI-модули слушают bus и сами решают. Кривой модуль не роняет соседа.
 *
 * gid 0x61 в каталоге = Dragon. Live зайцы носят тот же байт (dump19/20).
 * WorldReport.dominantCategory НИКОГДА не берёт GetEnemyCategory(0x61).
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

// Presence before the first hit. Seeded by HUNT, refreshed by a cheap +0C rewalk.
// kind is the instance-vt label (uEm0100 / uNpc / uEm8000). Never "Dragon".
struct WorldPresence {
    uintptr_t   ptr;
    uint32_t    vt;
    uint8_t     gid;
    const char* kind;
    float       x, y, z;
    bool        fromScan;
};

struct WorldReport {
    int      count;
    int      goblinCount;
    int      dominantCategory; // -1 none. Only uEm0100/uEm0101 publish a category (0=small).
    uint32_t timestampMs;
    WorldPresence units[32];
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

    // Опубликовать удар (вызывает CombatIntel). Топчет lastReport.
    void Publish(const CombatReport& rpt){
        lastReport = rpt;
        for(auto &fn: listeners) if(fn) fn(rpt);
    }

    const CombatReport& LastReport() const { return lastReport; }

    // Присутствие. Не зовёт hit-слушателей — TacticalSwitch читает LastWorld().
    void PublishWorld(const WorldReport& w){ lastWorld = w; }
    const WorldReport& LastWorld() const { return lastWorld; }

private:
    std::vector<Listener> listeners;
    CombatReport lastReport{};
    WorldReport  lastWorld{};
};

// ============ Хелпер для PawnAI-модулей ============
// Пример использования в модуле:
//   int busId = CombatBus::Instance().Subscribe([](const CombatReport& r){
//       float conf = r.utilitarianConfidence; // 0..1
//       // меняем свои инклинации
//   });

#pragma once
/**
 * CombatBus.h — ШИНА (мегафон тренера)
 *
 * ПОТОКОБЕЗОПАСНОСТЬ:
 * - Шина защищена SRWLOCK (читателей много, писатель один)
 * - Publish/PublishWorld пишут под эксклюзивной блокировкой
 * - LastReport/LastWorld читают под разделяемой блокировкой
 * - Слушатели вызываются тоже под эксклюзивом — subscriber обязан
 *   не делать долгих операций в колбэке
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
    // Build 62: враг сейчас в боевом действии (Atk/Dmg/Guard/Eva/... — по
    // DTI-имени live Act). Пусто для не-врагов и трупов.
    bool        inCombatAction;
    // Build 73.4: имя текущего действия целиком (cEm0100ActNAttack1 и т.п.).
    // Скан его уже читает, а обеим сторонам оно нужно: пешке — чтобы
    // понимать, что гоблин в замахе, режиссёру монстров — чтобы знать,
    // когда его правки вообще применимы. Копия, а не указатель: буфер
    // скана переживает шину не всегда.
    char        actName[40];
};

struct WorldReport {
    int      count;
    int      goblinCount;
    // Все враги, а не только гоблины: uEm* плюс uHumanEnemy (бандиты,
    // солдаты). goblinCount оставлен для отладки конкретного вида.
    int      enemyCount;
    // Трупы (ActDie/ActDeadBody). В units[] и count НЕ входят.
    int      deadCount;
    // Безобидная живность (uEm8000, зайцы): существа, но не угроза.
    // Считаются отдельно, чтобы не завышать опасность на пустом месте.
    int      critterCount;
    // Build 62: сколько ЖИВЫХ врагов сейчас в боевом действии. Сигнал
    // «враги реально дерутся», независимый от нанесённого урона.
    int      enemyCombatCount;
    // Build 62: пешка выбрала боевую цель (uCmc+0x2EB8 != 0). Ранний сигнал
    // начала боя — пешка «увидела» угрозу до первого удара.
    bool     pawnEngaged;
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
    int Subscribe(Listener fn) {
        AcquireSRWLockExclusive(&m_lock);
        listeners.push_back(fn);
        int id = (int)listeners.size()-1;
        ReleaseSRWLockExclusive(&m_lock);
        return id;
    }
    void Unsubscribe(int id){
        AcquireSRWLockExclusive(&m_lock);
        if(id>=0 && id < (int)listeners.size()) listeners[id]=nullptr;
        ReleaseSRWLockExclusive(&m_lock);
    }

    // Опубликовать удар (вызывает CombatIntel). Топчет lastReport.
    void Publish(const CombatReport& rpt){
        AcquireSRWLockExclusive(&m_lock);
        lastReport = rpt;
        for(auto &fn: listeners) if(fn) fn(rpt);
        ReleaseSRWLockExclusive(&m_lock);
    }

    CombatReport LastReport() const {
        AcquireSRWLockShared(&m_lock);
        CombatReport r = lastReport;
        ReleaseSRWLockShared(&m_lock);
        return r;
    }

    // Присутствие. Не зовёт hit-слушателей — TacticalSwitch читает LastWorld().
    void PublishWorld(const WorldReport& w){
        AcquireSRWLockExclusive(&m_lock);
        lastWorld = w;
        ReleaseSRWLockExclusive(&m_lock);
    }
    WorldReport LastWorld() const {
        AcquireSRWLockShared(&m_lock);
        WorldReport w = lastWorld;
        ReleaseSRWLockShared(&m_lock);
        return w;
    }

private:
    mutable SRWLOCK m_lock = SRWLOCK_INIT;
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
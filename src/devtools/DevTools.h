#pragma once
/**
 * DevTools — глаза мода. Живёт внутри dinput8.dll, не отдельным инжектом.
 * SCAN ищет dword vtable в образе. DUMP разбирает sUnit / sSetManager.
 * HUNT снимает живой список. WorldScan_Tick дешёво обходит +0C и пишет WorldReport.
 * FactoryPointer из TSV — пустой слот.
 */
namespace TypeAtlas { struct Info; }

namespace Hooks {
    void DevTools();
    void DevTools_Shutdown();
}

namespace DevTools {
    uintptr_t ModuleBase();
    uintptr_t Rebase(uint32_t rva);                    // 0 если нет базы
    const TypeAtlas::Info* Identify(const void* ptr);  // по первому dword (vtable)
    void WorldScan_Tick();                             // 150 ms: rewalk +0C, PublishWorld

    // --- доступ для модулей поведения (EnemyTuner) ---
    // Тело первого живого врага (uEm*), 0 если список пуст.
    //
    // ОСТОРОЖНО: "первый" = первый в списке, а НЕ тот, на кого смотрит игрок.
    // У лагеря обычно 6 штук uEm8000 и 1 uEm0100, поэтому гоблин почти
    // никогда не первый. Для работы с конкретным видом бери EnemyBodyAt()
    // или FirstBodyOfKind() — иначе запишешь параметры гоблина зайцу.
    uintptr_t FirstEnemyBody();
    // Сколько врагов сейчас в списке.
    int       EnemyCount();
    // Перебор врагов: idx 0..EnemyCount()-1. Возвращает тело и (опц.) имя
    // класса ("uEm0100"). 0, если такого индекса нет.
    uintptr_t EnemyBodyAt(int idx, const char** kindOut);
    // Тело первого врага заданного вида. 0, если такого вида в мире нет.
    uintptr_t FirstBodyOfKind(const char* kind);
    // Имя класса живого объекта через DTI. Возвращает out или nullptr.
    // Безопасна к мусорным указателям.
    const char* NameOfLiveObjectSafe(const void* obj, char* out, int cap);
    // Сколько трупов в списке (uEm* в состоянии Die/DeadBody).
    int         DeadCount();
    // Живое имя состояния по индексу в ПОЛНОМ списке (трупы включены).
    // deadOut — признак смерти. nullptr, если индекса нет или имя не читается.
    const char* EnemyActAt(int idx, bool* deadOut);
}

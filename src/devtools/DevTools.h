#pragma once
/**
 * DevTools — ИССЛЕДОВАТЕЛЬСКИЙ слой мода. Продуктом не является.
 *
 * Build 69: продуктовая часть (WorldScan, PartyRecon, PriorityPlatform)
 * переехала в src/runtime/ и работает независимо от этого модуля.
 * Здесь остались только SCAN/DUMP/HUNT/TypeAtlas и research-пробы.
 * Продукт зовёт research исключительно через Runtime::ResearchHooks.
 *
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

    // --- доступ для модулей поведения (EnemyTuner) ---
    // Тело первого живого врага (uEm*), 0 если список пуст.
    //
    // ОСТОРОЖНО: "первый" = первый в списке, а НЕ тот, на кого смотрит игрок.
    // У лагеря обычно 6 штук uEm8000 и 1 uEm0100, поэтому гоблин почти
    // никогда не первый. Для работы с конкретным видом бери EnemyBodyAt()
    // или FirstBodyOfKind() — иначе запишешь параметры гоблина зайцу.
    uintptr_t FirstEnemyBody();
    // Сколько трупов в списке (uEm* в состоянии Die/DeadBody).
    int         DeadCount();
    // Живое имя состояния по индексу в ПОЛНОМ списке (трупы включены).
    // deadOut — признак смерти. nullptr, если индекса нет или имя не читается.
    const char* EnemyActAt(int idx, bool* deadOut);





}

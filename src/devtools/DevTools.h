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

    // Build 56.2 — Guardian doctrine anchor/pawn world positions.
    // +0x40/+0x44/+0x48 (SOURCE_OF_TRUTH §2). false, если тела не резолвлены.
    bool GetArisenWorldPos(float* x, float* y, float* z);
    bool GetMainPawnWorldPos(float* x, float* y, float* z);

    // Build 57 — разведка Guardian-штрафов (read-only).
    // Сканирует уже разрешённые cPrioParam-строки (g_pawnAi) и для кодов
    // Guardian-семейства (4/13/15/54/60/66) снимает identity-кортеж + AddS32
    // каждого personality-правила. Нужно, чтобы БЕЗ угадывания найти точное
    // правило code 54 с штрафом -3 для будущего A/B-записи.
    // Возвращает компактную строку-отчёт (внутренний буфер, лог — полный).
    const char* GuardianPenaltyAudit();

    // Build 59 — разведка target-selection слоя (read-only).
    // Дамп жилых объектов выбора цели (sRecognition::cEnemyInfo, sLockOnManager
    // и т.д.) в лог + корреляция с текущей целью пешки (uCmc+0x2EB8) и телами
    // врагов. Возвращает компактный статус; полные байты — в логе.
    const char* TargetSelectionAudit();

    // Build 61 — прицельная охота за code 4 / code 66 (read-only, всегда в фоне).
    // Возвращает компактный статус последнего пойманного кода (action/target/GOAP).
    const char* GuardianIntentHunt();
    // Build 57.1 — динамический Guardian-фикс (code 54 rule 0, штраф -3 → 0).
    // Транзакционный apply/rollback ОДНОГО правила, управляется флагом armed.
    // Кортеж подтверждён дампом Build 57 (GuardianAudit):
    //   code=54 tuple{s=1,cat=0,obj=0,extra=1} rule[0] AddS32=-3 break=0 checks=1
    // Build 58: желаемое значение теперь произвольное (градиент). Передаётся
    // через GuardianFixSetTarget(desired); desired == -3 (vanilla) = rollback.
    void  GuardianFixSetTarget(int32_t desiredAddS32);
    bool  GuardianFixIsApplied();
    const char* GuardianFixStatus();
    void  GuardianFixTick();   // вызывает доктрина каждый тик (apply/rollback)
}

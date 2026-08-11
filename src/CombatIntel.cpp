#include "stdafx.h"
#include "MinHook/MinHook.h"
extern BYTE *codeBase, *codeEnd;
#include "CombatIntel.h"
#include "BestiaryData.h"
#include "CombatBus.h"
#include "EnemyTypes.Generated.h"

#define PLAYER_BASE         0xA7000
#define PAWN_OFFSET         0x7F0
#define PAWN_STRIDE         0x1660
#define MSTUDYFLAG_OFFSET   0x1616
#define MSTUDYFLAG_SIZE     322
#define COMBAT_TIMEOUT_SEC  5.0f

enum DamageSource : BYTE { SRC_PLAYER = 0, SRC_PAWN = 1, SRC_OTHER = 2 };

struct CombatEntry {
    BYTE     groupId;
    BYTE     source;
    DWORD    timestamp;
    uint32_t vtableRVA;
    uintptr_t targetPtr;
};
#define RING_SIZE 32

static CombatEntry g_ring[RING_SIZE];
static int g_head = 0;
static CRITICAL_SECTION g_lock;
static bool g_enabled = true;

// Hit counters — раздельный учет по типу врага и источнику (игрок vs пешка)
static int   g_hitCount[256] = {};
static int   g_playerHitsTotal = 0;
static int   g_pawnHitsTotal = 0;
static DWORD g_lastHitTick[256][2] = {}; // [gid][0=Player, 1=Pawn]
static int   g_totalHits = 0;

// Дебаунсер повторных суб-тиков (mHPCurrent vs mHPRecoverable) на одной сущности
struct TargetDebounce {
    uintptr_t targetPtr;
    DWORD     lastHitTick;
};
static TargetDebounce g_targetDebounce[16] = {};
static int g_debounceHead = 0;

static bool IsDebouncedHit(uintptr_t targetPtr, DWORD now) {
    if (!targetPtr) return false;
    // Маскируем младшие 8 бит для объединения суб-структур одного объекта (710 vs 718)
    uintptr_t entityBucket = targetPtr & ~0xFF;
    for (int i = 0; i < 16; i++) {
        if (g_targetDebounce[i].targetPtr == entityBucket) {
            if (now - g_targetDebounce[i].lastHitTick < 120) {
                return true; // Повторный саб-тик от того же удара — пропускаем!
            }
            g_targetDebounce[i].lastHitTick = now;
            return false;
        }
    }
    // Новый таргет
    g_targetDebounce[g_debounceHead].targetPtr = entityBucket;
    g_targetDebounce[g_debounceHead].lastHitTick = now;
    g_debounceHead = (g_debounceHead + 1) % 16;
    return false;
}

// Caller-хуки игрока (для точной отметки времени атак игрока)
static LPBYTE pDmg1 = nullptr, oDmg1 = nullptr;
static LPBYTE pDmg2 = nullptr, oDmg2 = nullptr;
static LPBYTE pDmg3 = nullptr, oDmg3 = nullptr;

// Универсальный Callee-хук точки снижения здоровья (DDDA.exe+374739: movss [edi+08], xmm0)
static LPBYTE pHpDamageHook = nullptr, oHpDamageHook = nullptr;

static volatile DWORD g_lastPlayerAttackTick = 0;
static DWORD g_lastPublishTick = 0;
static bool  g_lastInCombatState = false;

static void PublishToBus();

/**
 * Проверка состояния активной игры (защита от крашей при загрузке сейвов и переходе между локациями)
 */
static bool IsInActiveGameplay() {
    if (!pBase || !*pBase) return false;
    __try {
        BYTE* pPlayer = *pBase + PLAYER_BASE;
        if (IsBadReadPtr(pPlayer, 0x1000)) return false;
        UINT16 level = *(UINT16*)(pPlayer + 0xDD0);
        if (level == 0) return false; // Экран загрузки / главное меню
        float maxHp = *(float*)(pPlayer + 0x96C + 4);
        if (maxHp <= 0.0f || maxHp > 200000.0f) return false; // Персонаж еще не в живом мире
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

/**
 * Проверка, является ли объект членом нашей группы (Игрок или Пешка) по VTable MT Framework:
 * uPlayer VTable RVA:     0x11E4F34
 * uPlayerBase VTable RVA: 0x11CEF40
 */
static bool IsPartyMember(void* ptr) {
    if (!ptr || IsBadReadPtr(ptr, 0x20)) return true;
    __try {
        uintptr_t vtable = *(uintptr_t*)ptr;
        uintptr_t modBase = (uintptr_t)GetModuleHandle(nullptr);
        if (vtable >= modBase && vtable < modBase + 0x2000000) {
            uint32_t rva = (uint32_t)(vtable - modBase);
            if (rva == 0x11E4F34 || rva == 0x11CEF40 || rva == 0x157852C) {
                return true; // Это Игрок или Пешка!
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
    return false;
}

/**
 * Строгая проверка подлинности объекта врага uCharacterBase / uEm*:
 * 1. Объект не должен быть структурой здоровья healthPtr
 * 2. Объект НЕ должен быть игроком или пешкой (uPlayer / uPlayerBase)
 * 3. Проверяет cand[0x2D] и VTable в исполняемом коде DDDA.exe
 */
static bool IsValidEnemyCharacter(BYTE* cand, BYTE* healthPtr, BYTE* outGid) {
    if (!cand || cand == healthPtr || IsBadReadPtr(cand, 0x40)) return false;
    __try {
        if (IsPartyMember(cand)) return false;

        uintptr_t modBase = (uintptr_t)GetModuleHandle(nullptr);
        uintptr_t vtable = *(uintptr_t*)cand;
        if (vtable < modBase || vtable >= modBase + 0x2000000) return false;

        // 1. Прямая проверка cand[0x2D] по бестиарию
        BYTE g = cand[0x2D];
        if (g != 0 && g != 0xFF) {
            const EnemyEntry* e = FindEnemyByGid(g);
            if (e && e->groupId == g) {
                if (outGid) *outGid = g;
                return true;
            }
        }

        // 2. Проверка через VTable RVA (для uHumanEnemy 0xE0 и фауны)
        uint32_t rva = (uint32_t)(vtable - modBase);
        const EnemyTypeInfo* info = FindByVTable(rva);
        if (info && info->groupId != 0) {
            if (outGid) *outGid = info->groupId;
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

/**
 * Интеллектуальный резолвер Group ID врага:
 * ШАГ 0: Если ptr сам уже является базовым объектом монстра (удары игрока) -> возвращает gid сразу!
 * ШАГ 1: Если ptr — это структура здоровья (удары пешек) -> находит родительский uCharacterBase.
 */
static BYTE ResolveGidFromEntity(BYTE* ptr) {
    if (!ptr) return 0;
    BYTE gid = 0;

    // ШАГ 0: Прямая проверка — если ptr уже является базой монстра (как в ebx при атаке игрока)
    if (IsValidEnemyCharacter(ptr, nullptr, &gid)) {
        return gid;
    }

    __try {
        // ШАГ 1: Проверяем приоритетные указатели на владельца внутри структуры (+0x1B4, +0x14, +0x18, +0x00...)
        int priorityOffsets[] = { 0x1B4, 0x14, 0x18, 0x00, 0x04, 0x10, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40 };
        for (int off : priorityOffsets) {
            BYTE* cand = *(BYTE**)(ptr + off);
            if (IsValidEnemyCharacter(cand, ptr, &gid)) {
                return gid;
            }
        }

        // ШАГ 2: Проверяем обратные смещения базового персонажа (-0x710, -0x96C, -0x970, -0x1B4, -0x1D4)
        int baseOffsets[] = { 0x710, 0x96C, 0x970, 0x1B4, 0x1D4, 0x86C };
        for (int off : baseOffsets) {
            BYTE* cand = ptr - off;
            if (IsValidEnemyCharacter(cand, ptr, &gid)) {
                return gid;
            }
        }

        // ШАГ 3: Корреляция с активным врагом текущего боя (если бой уже идет)
        DWORD now = GetTickCount();
        for (int i = 0; i < RING_SIZE; i++) {
            if (g_ring[i].timestamp != 0 && (now - g_ring[i].timestamp) < (DWORD)(COMBAT_TIMEOUT_SEC * 1000)) {
                if (g_ring[i].groupId != 0) {
                    return g_ring[i].groupId;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    return 0;
}

/**
 * Центральный обработчик урона:
 * Вызывается при нанесении урона игроком или пешками.
 */
static void OnDamageInternal(BYTE* targetBase, DamageSource src) {
    if (!g_enabled || !targetBase || !IsInActiveGameplay()) return;

    // Отсекаем урон, наносимый игроку и пешкам (нас интересует только урон ПО ВРАГАМ)
    if (IsPartyMember(targetBase)) return;

    uint32_t vtRVA = 0;
    __try {
        uintptr_t vtable = *(uintptr_t*)targetBase;
        uintptr_t modBase = (uintptr_t)GetModuleHandle(nullptr);
        if (vtable >= modBase && vtable < modBase + 0x2000000) {
            vtRVA = (uint32_t)(vtable - modBase);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    BYTE gid = ResolveGidFromEntity(targetBase);

    DWORD now = GetTickCount();
    int srcIdx = (src == SRC_PLAYER) ? 0 : 1;

    EnterCriticalSection(&g_lock);

    // Запись в Ring Buffer (всегда обновляет метку времени активности боя)
    g_ring[g_head].groupId   = gid;
    g_ring[g_head].source    = (BYTE)src;
    g_ring[g_head].timestamp = now;
    g_ring[g_head].vtableRVA = vtRVA;
    g_ring[g_head].targetPtr = (uintptr_t)targetBase;
    g_head = (g_head + 1) % RING_SIZE;

    // Регистрация хитов со счетчиком (throttle 150мс раздельно для игрока и пешек)
    if (now - g_lastHitTick[gid][srcIdx] >= 150 || g_lastHitTick[gid][srcIdx] == 0) {
        g_lastHitTick[gid][srcIdx] = now;
        g_hitCount[gid]++;
        g_totalHits++;
        if (src == SRC_PLAYER) {
            g_playerHitsTotal++;
        } else {
            g_pawnHitsTotal++;
        }
    }

    LeaveCriticalSection(&g_lock);

    // Мгновенная публикация в шину для синхронизации AI пешек
    PublishToBus();
}

void __stdcall MarkPlayerAttack() {
    g_lastPlayerAttackTick = GetTickCount();
}

// Caller-хуки игрока: только выставляют точную временную метку атаки игрока!
void __declspec(naked) HDmg1()
{
    __asm
    {
        pushad
        call MarkPlayerAttack
        popad
        jmp oDmg1
    }
}

void __declspec(naked) HDmg2()
{
    __asm
    {
        pushad
        call MarkPlayerAttack
        popad
        jmp oDmg2
    }
}

void __declspec(naked) HDmg3()
{
    __asm
    {
        pushad
        call MarkPlayerAttack
        popad
        jmp oDmg3
    }
}

void __stdcall OnDamage_UniversalHealthWrite(BYTE* targetBase) {
    if (!targetBase || !IsInActiveGameplay()) return;
    
    DWORD now = GetTickCount();
    uintptr_t targetPtr = (uintptr_t)targetBase;

    // 1. Дебаунс дубликатов sub-writes (mHPCurrent vs mHPRecoverable) на одной сущности (120 мс)
    if (IsDebouncedHit(targetPtr, now)) {
        return;
    }

    // 2. Определение источника:
    // Проверяем метку атаки игрока (окно 250 мс) + прямой опрос кнопок мыши/контроллера
    bool isPlayerAttack = (now - g_lastPlayerAttackTick < 250);
    if (!isPlayerAttack) {
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
            isPlayerAttack = true;
        }
    }

    DamageSource src = isPlayerAttack ? SRC_PLAYER : SRC_PAWN;
    OnDamageInternal(targetBase, src);
}

// ============================================================================
// Универсальный перехват точки записи здоровья в DDDA.exe (DDDA.exe+374739)
// movss [edi+08], xmm0 -> edi = targetBase (сущность, получающая урон)
// КРИТИЧЕСКИ ВАЖНО: сохранение xmm0 (новое здоровье), чтобы C++ не затирал его!
// ============================================================================

void __declspec(naked) HUniversalHealthDamage()
{
    __asm
    {
        sub esp, 16
        movdqu [esp], xmm0       // 1. Сохраняем точное новое значение здоровья в xmm0
        pushad                  // 2. Сохраняем целочисленные регистры
        push edi                // 3. Передаем targetBase (edi)
        call OnDamage_UniversalHealthWrite
        popad                   // 4. Восстанавливаем целочисленные регистры
        movdqu xmm0, [esp]       // 5. Восстанавливаем xmm0 В ТОЧНОСТИ как рассчитал движок!
        add esp, 16
        jmp oHpDamageHook       // 6. Трамплин выполняет movss [edi+08], xmm0 и возвращается
    }
}

static float GetKnowledgeLevel(int mStudyIdx)
{
    if (!pBase || !*pBase) return 0.0f;
    if (mStudyIdx < 0 || mStudyIdx >= MSTUDYFLAG_SIZE) return 0.0f;
    BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
    BYTE val = study[mStudyIdx];
    if (val == 0) return 0.0f;
    int bits = 0;
    for (int i = 0; i < 8; i++)
        if (val & (1 << i)) bits++;
    if (bits >= 6) return 0.95f;
    if (bits >= 4) return 0.80f + (bits - 4) * 0.05f;
    if (bits >= 2) return 0.55f + (bits - 2) * 0.075f;
    return 0.35f;
}

static bool PawnKnowsGroup(BYTE gid)
{
    const EnemyEntry* e = FindEnemyByGid(gid);
    if (!e || e->mStudyIdx < 0) return false;
    return GetKnowledgeLevel(e->mStudyIdx) > 0.0f;
}

static void AnalyzeCombat(int* total, int* unknown, float* avgKnowledge)
{
    *total = 0; *unknown = 0; *avgKnowledge = 0.0f;
    DWORD now = GetTickCount();
    bool seen[256] = {};
    float sumK = 0.0f;

    EnterCriticalSection(&g_lock);
    for (int i = 0; i < RING_SIZE; i++) {
        if (g_ring[i].timestamp == 0) continue;
        if (now - g_ring[i].timestamp > (DWORD)(COMBAT_TIMEOUT_SEC * 1000)) continue;

        BYTE gid = g_ring[i].groupId;
        if (seen[gid]) continue;
        seen[gid] = true;
        (*total)++;

        const EnemyEntry* e = FindEnemyByGid(gid);
        if (e && e->mStudyIdx >= 0) {
            float kl = GetKnowledgeLevel(e->mStudyIdx);
            sumK += kl;
            if (kl < 0.3f) (*unknown)++;
        } else {
            (*unknown)++;
        }
    }
    LeaveCriticalSection(&g_lock);

    if (*total > 0) *avgKnowledge = sumK / (float)(*total);
}

// ========== BUS: тренер с мегафоном (CombatIntel публикует, PawnAI-модули слушают) ==========
static void PublishToBus()
{
    CombatReport r{};
    r.timestampMs = GetTickCount();

    int total = 0, unknown = 0;
    float avgK = 0.0f;
    AnalyzeCombat(&total, &unknown, &avgK);

    r.distinctTypes  = total;
    r.unknownTypes   = unknown;
    r.avgKnowledge01 = avgK;
    r.inCombat       = (total > 0);

    if (total == 0) {
        r.utilitarianConfidence = 0.5f;
    } else {
        r.utilitarianConfidence = 0.25f + avgK * 0.65f;
    }

    r.dominantCategory = GetCombatEnemyCategory();

    DWORD now = GetTickCount();
    bool seen[256] = {};
    bool seenPawn[256] = {};
    bool seenPlayer[256] = {};
    int out = 0, outPawn = 0;
    int pawnTypes = 0, playerTypes = 0;

    EnterCriticalSection(&g_lock);

    // Объединенный список активных врагов (до 8)
    for (int i = 0; i < RING_SIZE && out < 8; i++) {
        if (g_ring[i].timestamp == 0) continue;
        if (now - g_ring[i].timestamp > (DWORD)(COMBAT_TIMEOUT_SEC * 1000)) continue;

        BYTE gid = g_ring[i].groupId;
        if (seen[gid]) continue;
        seen[gid] = true;

        const EnemyEntry* entry = FindEnemyByGid(gid);
        const EnemyTypeInfo* info = FindByGroupId(gid);

        r.enemies[out].groupId = gid;
        r.enemies[out].uEmName = entry ? entry->name : (info ? info->uEmName : (gid == 0 ? "Target In Combat" : "???"));
        r.enemies[out].vtableRVA = g_ring[i].vtableRVA ? g_ring[i].vtableRVA : (info ? info->vtableRVA : 0);
        r.enemies[out].lastSeenMs = g_ring[i].timestamp;
        r.enemies[out].hitByPawn = false;
        r.enemies[out].hitByPlayer = false;

        for (int k = 0; k < RING_SIZE; k++) {
            if (g_ring[k].groupId == gid && now - g_ring[k].timestamp <= (DWORD)(COMBAT_TIMEOUT_SEC * 1000)) {
                if (g_ring[k].source == SRC_PAWN || g_ring[k].source == SRC_OTHER) r.enemies[out].hitByPawn = true;
                if (g_ring[k].source == SRC_PLAYER) r.enemies[out].hitByPlayer = true;
            }
        }

        if (entry && entry->mStudyIdx >= 0) {
            r.enemies[out].knowledge01 = GetKnowledgeLevel(entry->mStudyIdx);
            r.enemies[out].isUnknown = (r.enemies[out].knowledge01 < 0.3f);
        } else {
            r.enemies[out].knowledge01 = 0.0f;
            r.enemies[out].isUnknown = true;
        }
        out++;
    }

    // Список врагов, которых атакуют пешки (топ-4)
    for (int i = 0; i < RING_SIZE && outPawn < 4; i++) {
        if (g_ring[i].timestamp == 0) continue;
        if (g_ring[i].source != SRC_PAWN && g_ring[i].source != SRC_OTHER) continue;
        if (now - g_ring[i].timestamp > (DWORD)(COMBAT_TIMEOUT_SEC * 1000)) continue;

        BYTE gid = g_ring[i].groupId;
        if (seenPawn[gid]) continue;
        seenPawn[gid] = true;

        const EnemyEntry* entry = FindEnemyByGid(gid);
        const EnemyTypeInfo* info = FindByGroupId(gid);

        r.enemiesFromPawns[outPawn].groupId = gid;
        r.enemiesFromPawns[outPawn].uEmName = entry ? entry->name : (info ? info->uEmName : (gid == 0 ? "Target In Combat" : "???"));
        r.enemiesFromPawns[outPawn].lastSeenMs = g_ring[i].timestamp;
        r.enemiesFromPawns[outPawn].hitByPawn = true;
        r.enemiesFromPawns[outPawn].hitByPlayer = false;

        if (entry && entry->mStudyIdx >= 0) {
            r.enemiesFromPawns[outPawn].knowledge01 = GetKnowledgeLevel(entry->mStudyIdx);
            r.enemiesFromPawns[outPawn].isUnknown = (r.enemiesFromPawns[outPawn].knowledge01 < 0.3f);
        } else {
            r.enemiesFromPawns[outPawn].knowledge01 = 0.0f;
            r.enemiesFromPawns[outPawn].isUnknown = true;
        }
        outPawn++;
    }

    // Подсчет distinct врагов по источникам
    for (int i = 0; i < 256; i++) {
        if (seenPawn[i]) pawnTypes++;
        if (seenPlayer[i]) playerTypes++;
    }

    r.pawnHits   = g_pawnHitsTotal;
    r.playerHits = g_playerHitsTotal;

    LeaveCriticalSection(&g_lock);

    r.enemyCount     = out;
    r.pawnEnemyCount = outPawn;
    r.pawnDistinct   = pawnTypes;
    r.playerDistinct = playerTypes;

    CombatBus::Instance().Publish(r);
}

void CombatIntel_Tick()
{
    if (!g_enabled) return;
    DWORD now = GetTickCount();
    if (now - g_lastPublishTick < 150) return;
    g_lastPublishTick = now;

    bool currentInCombat = IsInCombat();
    if (currentInCombat || g_lastInCombatState != currentInCombat) {
        g_lastInCombatState = currentInCombat;
        PublishToBus();
    }
}

float GetCombatUtilitarianConfidence()
{
    if (!g_enabled) return 0.5f;
    int total, unknown; float avgK;
    AnalyzeCombat(&total, &unknown, &avgK);
    if (total == 0) return 0.5f;
    return 0.25f + avgK * 0.65f;
}

bool IsInCombat()
{
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < RING_SIZE; i++) {
        if (g_ring[i].timestamp != 0 && (now - g_ring[i].timestamp) < (DWORD)(COMBAT_TIMEOUT_SEC * 1000)) {
            LeaveCriticalSection(&g_lock);
            return true;
        }
    }
    LeaveCriticalSection(&g_lock);
    return false;
}

int GetCombatEnemyCategory()
{
    int bestCat = -1;
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < RING_SIZE; i++) {
        if (g_ring[i].timestamp == 0) continue;
        if (now - g_ring[i].timestamp >= (DWORD)(COMBAT_TIMEOUT_SEC * 1000)) continue;
        BYTE gid = g_ring[i].groupId;
        int cat = GetEnemyCategory(gid);
        if (cat > bestCat) bestCat = cat;
    }
    LeaveCriticalSection(&g_lock);
    return bestCat;
}

void RenderCombatIntelUI()
{
    if (!ImGui::CollapsingHeader("Combat Intel v2.8")) return;
    PublishToBus(); // Periodic UI update
    ImGui::PushID("CI");

    if (ImGui::Checkbox("Enable Combat Intel", &g_enabled))
        config.setBool("combatIntel", "enabled", g_enabled);

    int total, unknown; float avgK;
    AnalyzeCombat(&total, &unknown, &avgK);
    float conf = GetCombatUtilitarianConfidence();
    auto &bus = CombatBus::Instance().LastReport();

    if (bus.inCombat || total > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[ACTIVE] IN COMBAT");
        ImGui::Text("Types: %d (Player: %d, Pawns: %d) | Unknown: %d | Know: %.0f%%",
            total, bus.playerDistinct, bus.pawnDistinct, unknown, avgK * 100);
        ImGui::Text("Hits: Player %d | Pawn %d | Total %d", bus.playerHits, bus.pawnHits, g_totalHits);

        if (bus.pawnEnemyCount > 0) {
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Pawn targets engaged:");
            for (int i = 0; i < bus.pawnEnemyCount; i++) {
                auto &e = bus.enemiesFromPawns[i];
                ImGui::BulletText("0x%02X %s (know: %.0f%%)", e.groupId, e.uEmName ? e.uEmName : "???", e.knowledge01 * 100);
            }
        }
        ImGui::ProgressBar(conf, ImVec2(200, 0));
        ImGui::SameLine();
        ImGui::TextDisabled("Util: %.0f%%", conf * 100);
    } else {
        ImGui::TextDisabled("Peace / No combat detected");
        ImGui::TextDisabled("Hits logged: Player %d | Pawn %d | Total %d", g_playerHitsTotal, g_pawnHitsTotal, g_totalHits);
    }

    ImGui::Separator();

    if (ImGui::TreeNode("Damage Ring Buffer")) {
        DWORD now = GetTickCount();
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < RING_SIZE; i++) {
            if (g_ring[i].timestamp == 0) continue;
            DWORD age = (now - g_ring[i].timestamp) / 1000;
            bool active = age < (DWORD)COMBAT_TIMEOUT_SEC;
            const EnemyEntry* e = FindEnemyByGid(g_ring[i].groupId);
            const char* srcStr = (g_ring[i].source == SRC_PAWN) ? "PAWN" : (g_ring[i].source == SRC_PLAYER) ? "PLR" : "OTH";
            ImGui::TextColored(active ? ImVec4(1, 0.5f, 0.3f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                "[%s] gid=0x%02X %-18s %s (%ds ago)",
                srcStr, g_ring[i].groupId,
                e ? e->name : (g_ring[i].groupId == 0 ? "Target" : "???"),
                PawnKnowsGroup(g_ring[i].groupId) ? "KNOWN" : "unk",
                age);
        }
        LeaveCriticalSection(&g_lock);

        ImGui::Separator();
        ImGui::TextDisabled("Total hits: %d (Player: %d, Pawn: %d)", g_totalHits, g_playerHitsTotal, g_pawnHitsTotal);
        for (int k = 0; k < 256; k++) {
            if (g_hitCount[k]) {
                const EnemyEntry* e = FindEnemyByGid((BYTE)k);
                ImGui::Text("  gid 0x%02X %-16s hits: %d", k, e ? e->name : (k == 0 ? "Target" : "???"), g_hitCount[k]);
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bestiary Knowledge (72 enemies from types.tsv + bestiary.py)")) {
        ImGui::TextWrapped("Full 72-enemy mapping derived from MT Framework factories and mStudyFlag[322].");
        for (const auto* e = g_bestiary; e->name; e++) {
            float kLevel = (e->mStudyIdx >= 0) ? GetKnowledgeLevel(e->mStudyIdx) : 0.0f;
            ImVec4 col = (kLevel >= 0.8f) ? ImVec4(0.3f, 1, 0.3f, 1) : (kLevel > 0.0f) ? ImVec4(1, 0.8f, 0.2f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1);
            ImGui::TextColored(col, "%02X [mIdx=%3d] %-22s %-12s %.0f%% %s",
                e->groupId, e->mStudyIdx,
                e->name, e->family ? e->family : "",
                kLevel * 100,
                (kLevel > 0.0f) ? "KNOWN" : "unk");
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void Hooks::CombatIntel()
{
    InitializeCriticalSection(&g_lock);
    memset(g_ring, 0, sizeof(g_ring));
    memset(g_targetDebounce, 0, sizeof(g_targetDebounce));
    g_enabled = config.getBool("combatIntel", "enabled", true);

    BYTE s1[] = { 0x51, 0xF3, 0x0F, 0x11, 0x0C, 0x24, 0xE8, 0xCC, 0xCC, 0xCC, 0xCC, 0x8B, 0x13, 0x8B, 0x82, 0xD4, 0x01, 0x00, 0x00 };
    BYTE s2[] = { 0x51, 0xF3, 0x0F, 0x11, 0x0C, 0x24, 0xE8, 0xCC, 0xCC, 0xCC, 0xCC, 0x8B, 0x16, 0x8B, 0x82, 0xD4, 0x01, 0x00, 0x00 };
    BYTE s3[] = { 0x51, 0xF3, 0x0F, 0x11, 0x0C, 0x24, 0xE8, 0xCC, 0xCC, 0xCC, 0xCC, 0x8B, 0x06, 0x8B, 0x90, 0xD4, 0x01, 0x00, 0x00 };

    // Сигнатура центральной функции вычитания здоровья (0x00774732 / 0x00774739)
    // subss xmm0, [esp+0C]; push esi; movss [edi+08], xmm0; test ecx, ecx; je ...
    BYTE sHP[] = { 0xF3, 0x0F, 0x5C, 0x44, 0x24, 0x0C, 0x56, 0xF3, 0x0F, 0x11, 0x47, 0x08, 0x85, 0xC9 };
    BYTE sHP_Short[] = { 0xF3, 0x0F, 0x11, 0x47, 0x08, 0x85, 0xC9, 0x74 };

    bool o1 = Hooks::FindSignature("CI1_PlayerMelee", s1, &pDmg1);
    bool o2 = Hooks::FindSignature("CI2_PlayerBow",   s2, &pDmg2);
    bool o3 = Hooks::FindSignature("CI3_PlayerSpell", s3, &pDmg3);

    // 1. Установка Caller-хуков игрока (выставляют точную временную метку атак игрока)
    if (o1) Hooks::CreateHook("CI1_PlayerMelee", pDmg1 += 6, HDmg1, (LPVOID*)&oDmg1, true);
    if (o2) Hooks::CreateHook("CI2_PlayerBow",   pDmg2 += 6, HDmg2, (LPVOID*)&oDmg2, true);
    if (o3) Hooks::CreateHook("CI3_PlayerSpell", pDmg3 += 6, HDmg3, (LPVOID*)&oDmg3, true);

    // 2. Установка Универсального хука на запись здоровья врага (DDDA.exe+374739: movss [edi+08], xmm0)
    LPBYTE pHpSig = nullptr;
    bool oHP = false;
    if (Hooks::FindSignature("CI_UniversalHealthWrite", sHP, &pHpSig)) {
        pHpDamageHook = pHpSig + 7; // Точка инструкции movss [edi+08], xmm0
        oHP = true;
    } else if (Hooks::FindSignature("CI_UniversalHealthWrite_Short", sHP_Short, &pHpSig)) {
        pHpDamageHook = pHpSig;
        oHP = true;
    }

    if (oHP && pHpDamageHook) {
        logFile << "CombatIntel: Universal Health Damage Hook at " << (LPVOID)pHpDamageHook << std::endl;
        Hooks::CreateHook("CI_UniversalHealthWrite", pHpDamageHook, HUniversalHealthDamage, (LPVOID*)&oHpDamageHook, true);
    }

    InGameUIAdd(RenderCombatIntelUI);
    logFile << "CombatIntel v2.8: " << (o1 ? 1 : 0) + (o2 ? 1 : 0) + (o3 ? 1 : 0) << " player attack markers, "
            << (oHP ? 1 : 0) << " universal health write hooks installed. " << BESTIARY_COUNT << " bestiary entries ready." << std::endl;
}

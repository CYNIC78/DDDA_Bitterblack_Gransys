/**
 * CombatIntel.cpp — боевая разведка (v1.0)
 *
 * ИДЕЯ: вместо поиска «кого таргетит пешка» (сложно, нужен реверс),
 * мы перехватываем ВСЕ damage-события и ведём «журнал боя».
 *
 * Как работает:
 *   1. Хукаем damage (3 сигнатуры из DamageLog динпут8)
 *   2. targetBase[0x2D] = enemyGroupId (1 байт!)
 *      - 0x05 = Goblin
 *      - 0x08 = Wolf
 *      - 0x?? = Skeleton
 *      - и т.д.
 *   3. Ведём ring buffer: enemyGroupId → lastHitTimestamp
 *   4. Каждый кадр: если враг был атакован в последние N секунд
 *      → он «в бою» → проверяем mStudyFlag
 *   5. Если есть незнакомые враги → UtilitarianConfidence ↓
 *      Если все знакомы → UtilitarianConfidence ↑
 *
 * mStudyFlag: pawn + 0x1616, 322 байта
 *
 * Маппинг enemyGroupId → mStudyFlag[index] (заполняется по мере находок):
 *   Goblin:  groupId=0x05 → mStudyFlag[4]
 *   Wolf:    groupId=0x08 → mStudyFlag[?]
 *   Skeleton:groupId=0x?? → mStudyFlag[8]
 *
 * Гипотеза: groupId может совпадать с индексом (±1).
 * Проверим через DamageLog в режиме "Group id".
 */

#include "stdafx.h"
#include "CombatIntel.h"
#include <ctime>

// ═══════════════════════════════════════════════════════════
// КОНСТАНТЫ
// ═══════════════════════════════════════════════════════════

#define PLAYER_BASE         0xA7000
#define PAWN_OFFSET         0x7F0
#define MSTUDYFLAG_OFFSET   0x1616
#define MSTUDYFLAG_SIZE     322

// Сколько секунд враг считается «в бою» после последнего удара
#define COMBAT_TIMEOUT_SEC  5.0f

// ═══════════════════════════════════════════════════════════
// RING BUFFER: враги в бою
// ═══════════════════════════════════════════════════════════

struct CombatEntry {
    BYTE  groupId;      // enemyGroupId из targetBase[0x2D]
    DWORD timestamp;     // GetTickCount()
};
#define COMBAT_RING_SIZE 32

static CombatEntry g_combatRing[COMBAT_RING_SIZE];
static int g_combatHead = 0;
static CRITICAL_SECTION g_combatLock;

// ═══════════════════════════════════════════════════════════
// МАППИНГ enemyGroupId → mStudyFlag индекс
// ═══════════════════════════════════════════════════════════
//
// Заполняется тобой по мере тестов:
//   1. Включи DamageLog в режиме "Group id"
//   2. Ударь врага → запиши groupId
//   3. Купи свиток знаний на этого врага → probe покажет индекс
//   4. Запиши ниже

// MSVC не поддерживает designated initializers в C++ — заполняем циклом
static int g_groupToStudy[256];

static void InitGroupToStudy() {
    for (int i = 0; i < 256; i++) g_groupToStudy[i] = -1;
    g_groupToStudy[0x05] = 4;    // Goblin (твои данные)
    g_groupToStudy[0x08] = -1;    // Wolf (ждём свиток!)
}

// ═══════════════════════════════════════════════════════════
// DAMAGE HOOK
// ═══════════════════════════════════════════════════════════

static LPBYTE pDmgHook1, oDmgHook1;
static LPBYTE pDmgHook2, oDmgHook2;
static LPBYTE pDmgHook3, oDmgHook3;

// Обработчик: вызывается при КАЖДОМ ударе по врагу
void __stdcall OnDamageDealt(BYTE *targetBase, float damage)
{
    BYTE groupId = targetBase[0x2D];  // 🔑 enemyGroupId!

    EnterCriticalSection(&g_combatLock);
    g_combatRing[g_combatHead].groupId   = groupId;
    g_combatRing[g_combatHead].timestamp = GetTickCount();
    g_combatHead = (g_combatHead + 1) % COMBAT_RING_SIZE;
    LeaveCriticalSection(&g_combatLock);
}

// Три сигнатуры — три типа урона (игрок, пешка, другой)
void __declspec(naked) HDamageHook1() {
    __asm mov eax, [esp];
    __asm pushad;
    __asm push eax;
    __asm push ebx;
    __asm call OnDamageDealt;
    __asm popad;
    __asm jmp oDmgHook1;
}
void __declspec(naked) HDamageHook2() {
    __asm mov eax, [esp];
    __asm pushad;
    __asm push eax;
    __asm push esi;
    __asm call OnDamageDealt;
    __asm popad;
    __asm jmp oDmgHook2;
}
void __declspec(naked) HDamageHook3() {
    __asm mov eax, [esp];
    __asm pushad;
    __asm push eax;
    __asm push esi;
    __asm call OnDamageDealt;
    __asm popad;
    __asm jmp oDmgHook3;
}

// ═══════════════════════════════════════════════════════════
// АНАЛИЗ: есть ли незнакомые враги в бою?
// ═══════════════════════════════════════════════════════════

static bool g_combatIntelEnabled = true;

/**
 * Проверить mStudyFlag для конкретного enemyGroupId.
 * Возвращает true если пешка ЗНАЕТ этого врага (флаг != 0).
 */
static bool PawnKnowsGroup(BYTE groupId) {
    if (!pBase || !*pBase) return false;

    int studyIdx = g_groupToStudy[groupId];
    if (studyIdx < 0 || studyIdx >= MSTUDYFLAG_SIZE) return false;

    BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
    return study[studyIdx] != 0;
}

/**
 * Просканировать ring buffer, найти ВСЕ типы врагов
 * которые были атакованы за последние COMBAT_TIMEOUT_SEC.
 *
 * Возвращает:
 *   totalEnemies — сколько разных типов в бою
 *   unknownEnemies — сколько из них пешка НЕ знает
 */
static void AnalyzeCombat(int* totalEnemies, int* unknownEnemies) {
    *totalEnemies = 0;
    *unknownEnemies = 0;

    DWORD now = GetTickCount();
    bool seen[256] = {};

    EnterCriticalSection(&g_combatLock);
    for (int i = 0; i < COMBAT_RING_SIZE; i++) {
        BYTE gid = g_combatRing[i].groupId;
        if (gid == 0) continue;
        if (seen[gid]) continue;

        DWORD elapsed = now - g_combatRing[i].timestamp;
        if (elapsed > COMBAT_TIMEOUT_SEC * 1000) continue;

        seen[gid] = true;
        (*totalEnemies)++;

        if (g_groupToStudy[gid] >= 0 && !PawnKnowsGroup(gid))
            (*unknownEnemies)++;
    }
    LeaveCriticalSection(&g_combatLock);
}

/**
 * ПОЛНАЯ уверенность Utilitarian на основе реального боя.
 *
 * Логика:
 *   - Если в бою НЕТ врагов → неопределённость, confidence = 0.5
 *   - Если в бою ТОЛЬКО знакомые → confidence ↑ до 0.95
 *   - Если в бою есть незнакомые → confidence ↓ до 0.25
 *   - Градация зависит от доли незнакомых
 */
float GetCombatUtilitarianConfidence() {
    if (!g_combatIntelEnabled) return 0.5f;

    int total, unknown;
    AnalyzeCombat(&total, &unknown);

    if (total == 0) return 0.5f;  // не в бою — неопределённость

    float knownRatio = 1.0f - (float)unknown / (float)total;

    // knownRatio = 1.0 (все знакомы) → 0.90 confidence
    // knownRatio = 0.5 (половина)   → 0.55 confidence
    // knownRatio = 0.0 (все чужие)  → 0.25 confidence
    return 0.25f + knownRatio * 0.65f;
}

// ═══════════════════════════════════════════════════════════
// ImGui UI
// ═══════════════════════════════════════════════════════════

void RenderCombatIntelUI() {
    if (!ImGui::CollapsingHeader("Combat Intel"))
        return;
    ImGui::PushID("CmbtIntel");

    if (ImGui::Checkbox("Enable Combat Intel", &g_combatIntelEnabled))
        config.setBool("combatIntel", "enabled", g_combatIntelEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hooks ALL damage events. Tracks which enemies\n"
                          "are in combat RIGHT NOW. Checks pawn's knowledge.");

    ImGui::Separator();

    // --- Текущий бой ---
    int total, unknown;
    AnalyzeCombat(&total, &unknown);
    float conf = GetCombatUtilitarianConfidence();

    if (total > 0) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "⚔ IN COMBAT");
        ImGui::Text("Enemy types in fight: %d", total);
        ImGui::Text("Unknown to pawn: %d", unknown);
        ImGui::Text("Utilitarian confidence: %.0f%%", conf * 100);
        ImGui::ProgressBar(conf, ImVec2(200, 0));

        if (unknown > 0)
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                "⚠ Unfamiliar foes! Utilitarian suppressed.");
        else
            ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1),
                "✓ All enemies known! Utilitarian at full power.");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "No combat detected");
    }

    ImGui::Separator();

    // --- Ring buffer дамп ---
    if (ImGui::TreeNode("Damage Ring Buffer")) {
        DWORD now = GetTickCount();
        EnterCriticalSection(&g_combatLock);
        for (int i = 0; i < COMBAT_RING_SIZE; i++) {
            if (g_combatRing[i].groupId == 0) continue;
            DWORD age = (now - g_combatRing[i].timestamp) / 1000;
            bool active = age < (DWORD)COMBAT_TIMEOUT_SEC;
            ImGui::TextColored(
                active ? ImVec4(1, 0.5f, 0.3f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
                "group=0x%02X  studyIdx=%d  known=%s  %ds ago",
                g_combatRing[i].groupId,
                g_groupToStudy[g_combatRing[i].groupId],
                PawnKnowsGroup(g_combatRing[i].groupId) ? "YES" : "NO",
                age
            );
        }
        LeaveCriticalSection(&g_combatLock);
        ImGui::TreePop();
    }

    // --- Таблица маппинга ---
    if (ImGui::TreeNode("Group → Study Mapping")) {
        ImGui::TextWrapped("Fill this as you test. Hit enemy → note groupId → use scroll → note studyIdx.");
        ImGui::Columns(3, nullptr, false);
        ImGui::Text("Group"); ImGui::NextColumn();
        ImGui::Text("StudyIdx"); ImGui::NextColumn();
        ImGui::Text("Enemy"); ImGui::NextColumn();
        ImGui::Separator();

        struct { BYTE gid; int idx; const char* name; } known[] = {
            {0x05, 4, "Goblin"},
            {0xFF, 8, "Skeleton (group TBD)"},
            {0x08, -1, "Wolf (study TBD)"},
        };
        for (auto& k : known) {
            if (k.gid == 0xFF) continue;
            ImGui::Text("0x%02X", k.gid); ImGui::NextColumn();
            ImGui::Text("%d", k.idx); ImGui::NextColumn();
            ImGui::Text("%s", k.name); ImGui::NextColumn();
        }
        ImGui::Columns();
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════
// ИНИЦИАЛИЗАЦИЯ
// ═══════════════════════════════════════════════════════════

void Hooks::CombatIntel() {
    InitializeCriticalSection(&g_combatLock);
    memset(g_combatRing, 0, sizeof(g_combatRing));
    InitGroupToStudy();

    g_combatIntelEnabled = config.getBool("combatIntel", "enabled", true);

    // Три сигнатуры из DamageLog.cpp (работают!)
    BYTE sig1[] = { 0x51, 0xF3, 0x0F, 0x11, 0x0C, 0x24, 0xE8,
                    0xCC, 0xCC, 0xCC, 0xCC, 0x8B, 0x13, 0x8B,
                    0x82, 0xD4, 0x01, 0x00, 0x00 };
    BYTE sig2[] = { 0x51, 0xF3, 0x0F, 0x11, 0x0C, 0x24, 0xE8,
                    0xCC, 0xCC, 0xCC, 0xCC, 0x8B, 0x16, 0x8B,
                    0x82, 0xD4, 0x01, 0x00, 0x00 };
    BYTE sig3[] = { 0x51, 0xF3, 0x0F, 0x11, 0x0C, 0x24, 0xE8,
                    0xCC, 0xCC, 0xCC, 0xCC, 0x8B, 0x06, 0x8B,
                    0x90, 0xD4, 0x01, 0x00, 0x00 };

    bool ok1 = Hooks::FindSignature("CombatIntel1", sig1, &pDmgHook1);
    bool ok2 = Hooks::FindSignature("CombatIntel2", sig2, &pDmgHook2);
    bool ok3 = Hooks::FindSignature("CombatIntel3", sig3, &pDmgHook3);

    if (ok1) Hooks::CreateHook("CombatIntel", pDmgHook1 += 6, HDamageHook1, (LPVOID*)&oDmgHook1, true);
    if (ok2) Hooks::CreateHook("CombatIntel", pDmgHook2 += 6, HDamageHook2, (LPVOID*)&oDmgHook2, true);
    if (ok3) Hooks::CreateHook("CombatIntel", pDmgHook3 += 6, HDamageHook3, (LPVOID*)&oDmgHook3, true);

    int sigsFound = (ok1?1:0) + (ok2?1:0) + (ok3?1:0);
    logFile << "CombatIntel initialized: " << sigsFound << "/3 damage sigs hooked" << std::endl;

    InGameUIAdd(RenderCombatIntelUI);
}

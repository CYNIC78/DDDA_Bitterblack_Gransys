/**
 * PawnAI.cpp — AI Overhaul для пешек (v2.0)
 *
 * ЛЁГКИЙ УРОВЕНЬ (dinput8 DLL):
 *   ✅ 1.1 Пресеты инклинаций (6 профилей)
 *   ✅ 1.2 Плавные переходы
 *   ✅ 1.3 Санитарный кордон (динамический, без хардкода)
 *   ✅ 1.4 Smart Utilitarian Phase A (по уровню)
 *   🔥 1.4 Smart Utilitarian Phase B (mStudyFlag!) ← СЕГОДНЯ
 *
 * ВАЖНЫЕ ОФФСЕТЫ (из твоей CT-таблицы, GoG):
 *   pBase → *(pBaseSig + 2)
 *   Пешка = pBase + 0xA7000 + 0x7F0
 *   Инклинации = Пешка + 0x96C + 0x1224  (ШАГ 0xC между значениями!)
 *   mStudyFlag  = Пешка + 0x1616           (322 байта)
 */

#include "stdafx.h"
#include "PawnAI.h"
#include "CombatIntel.h"

// ═══════════════════════════════════════════════════════════
// ОФФСЕТЫ (верифицированы твоей CT-таблицей, GoG версия)
// ═══════════════════════════════════════════════════════════

#define PLAYER_BASE         0xA7000
#define PAWN_OFFSET         0x7F0
#define INCL_OFFSET         (0x96C + 0x1224)   // от базы пешки
#define INCL_STRIDE         0xC                // 🔧 БЫЛО 4, СТАЛО 12!
#define MSTUDYFLAG_OFFSET   0x1616             // 🔥 от базы пешки, 322 байта
#define MSTUDYFLAG_SIZE     322

// Полный оффсет до инклинации: pBase + PLAYER_BASE + PAWN_OFFSET + INCL_OFFSET + i * INCL_STRIDE
// Полный оффсет до mStudyFlag:  pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET

// ═══════════════════════════════════════════════════════════
// ИНКЛИНАЦИИ
// ═══════════════════════════════════════════════════════════

enum InclIdx {
    I_SCATHER = 0, I_MEDICANT, I_MITIGATOR, I_CHALLENGER,
    I_UTILITARIAN, I_GUARDIAN, I_NEXUS, I_PIONEER,
    I_ACQUISITOR, I_SKILL_USE, I_COUNT
};

const char* InclNames[] = {
    "Scather","Medicant","Mitigator","Challenger",
    "Utilitarian","Guardian","Nexus","Pioneer",
    "Acquisitor","Skill Use"
};

// Категории для санитарного кордона
typedef enum { CAT_USEFUL, CAT_NEUTRAL, CAT_JUNK } InclCat;

InclCat GetInclCategory(int idx) {
    switch (idx) {
        case I_SCATHER: case I_MEDICANT: case I_MITIGATOR:
        case I_CHALLENGER: case I_UTILITARIAN: return CAT_USEFUL;
        case I_GUARDIAN: case I_NEXUS: case I_ACQUISITOR: return CAT_JUNK;
        default: return CAT_NEUTRAL;
    }
}

// ═══════════════════════════════════════════════════════════
// ПРЕСЕТЫ
// ═══════════════════════════════════════════════════════════

struct InclPreset {
    const char* name, *desc;
    float v[I_COUNT];
};

static const InclPreset presets[] = {
    {"Boss Killer",  "Крупные враги",       {900,350,450,700,800, 300,300,300,250, 850}},
    {"Crowd Ctrl",   "Группы врагов",        {700,350,900,650,500, 350,350,300,250, 700}},
    {"Tactical Sup", "Поддержка + тактика",   {300,900,350,400,800, 700,450,300,300, 600}},
    {"Ranged Hunter","Охота на магов",        {650,350,450,900,750, 350,350,350,250, 700}},
    {"Explorer",     "Вне боя",              {300,300,300,300,550, 350,400,850,800, 400}},
    {"Balanced",     "Универсал",            {700,500,500,600,800, 450,450,400,400, 600}},
};
#define PRESET_COUNT 6

// ═══════════════════════════════════════════════════════════
// СОСТОЯНИЕ
// ═══════════════════════════════════════════════════════════

static bool  g_enabled          = true;
static bool  g_presetsEnabled   = true;
static bool  g_sanitaryEnabled  = true;
static bool  g_smartUtilEnabled = true;
static int   g_presetIdx        = 5;
static float g_smooth           = 0.1f;

// ═══════════════════════════════════════════════════════════
// ДОСТУП К ИНКЛИНАЦИЯМ (с правильным stride!)
// ═══════════════════════════════════════════════════════════

float* GetPawnInclination(int idx, int partyIdx) {
    if (!pBase || !*pBase) return nullptr;
    int pawnBase = PLAYER_BASE + PAWN_OFFSET + partyIdx * 0x1660;
    // Каждая инклинация лежит через 0xC байт (float + padding?)
    return GetBasePtr<float>(pawnBase + INCL_OFFSET + idx * INCL_STRIDE);
}

static void ReadAllIncl(float* out, int partyIdx) {
    for (int i = 0; i < I_COUNT; i++) {
        float* p = GetPawnInclination(i, partyIdx);
        out[i] = p ? *p : 500.0f;
    }
}

static void WriteAllIncl(const float* vals, int partyIdx) {
    for (int i = 0; i < I_COUNT; i++) {
        float* p = GetPawnInclination(i, partyIdx);
        if (p) *p = vals[i];
    }
}

// ═══════════════════════════════════════════════════════════
// 1.3 САНИТАРНЫЙ КОРДОН (динамический)
// ═══════════════════════════════════════════════════════════

static void ApplySanitaryCordon(float* incl) {
    if (!g_sanitaryEnabled) return;

    // Собираем полезные, сортируем, cap = 3-я
    float u[I_COUNT]; int n = 0;
    for (int i = 0; i < I_COUNT; i++)
        if (GetInclCategory(i) == CAT_USEFUL) u[n++] = incl[i];
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (u[j] > u[i]) { float t=u[i]; u[i]=u[j]; u[j]=t; }
    float cap = (n >= 3) ? u[2] : 500.0f;

    // Плавно прижимаем мусор
    for (int i = 0; i < I_COUNT; i++) {
        if (GetInclCategory(i) != CAT_JUNK) continue;
        if (incl[i] > cap) {
            float decay = (incl[i] - cap) * 0.05f;
            if (decay < 0.5f) decay = 0.5f;
            incl[i] -= decay;
        }
    }
}

// ═══════════════════════════════════════════════════════════
// 1.4 SMART UTILITARIAN
// ═══════════════════════════════════════════════════════════

/**
 * Phase B: чтение mStudyFlag (322 байта) из памяти пешки.
 *
 * Твоя CT-таблица: mStudyFlag = pawn + 0x1616
 *   pawn = pBase + 0xA7000 + 0x7F0
 *
 * Каждый байт — битовая карта знаний по одному врагу.
 *   Байт != 0 → есть хоть какое-то знание.
 *
 * Goblin = индекс 0 (ты проверил свитком)
 * Skeleton = индекс 9 (ты проверил свитком)
 * Номера врагов растут примерно как emXXXX ID.
 */
static int CountKnownEnemies() {
    if (!pBase || !*pBase) return 0;

    BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
    int known = 0;
    for (int i = 0; i < MSTUDYFLAG_SIZE; i++) {
        if (study[i] != 0) known++;
    }
    return known;
}

/**
 * Проверить знание КОНКРЕТНОГО врага по его emID.
 *
 * Маппинг (из твоих тестов):
 *   Goblin (em0000) → индекс 0
 *   Skeleton (em0500?) → индекс 9
 *
 * Для простоты пока используем эвристику:
 *   emID / 100 → примерный индекс в mStudyFlag
 *   (гоблин=0, волк=2, ящер=4, циклоп=6, химера=7, дракон=10...)
 *
 * Более точный маппинг — когда ты просканируешь больше врагов.
 */
static int GetEnemyStudyIdx(int emId) {
    // Грубая эвристика: em0500 → 5, em1000 → 10
    // Но из твоих данных: скелет (em0500?) = индекс 9, а не 5.
    // Значит нумерация НЕ совпадает с emID.

    // Пока возвращаем -1 = "не знаем индекс"
    // Реальный маппинг построим когда ты просканируешь 5-6 врагов
    return -1;
}

static bool PawnKnowsEnemy(int emId) {
    if (!pBase || !*pBase) return false;
    int idx = GetEnemyStudyIdx(emId);
    if (idx < 0 || idx >= MSTUDYFLAG_SIZE) return false;

    BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
    return study[idx] != 0;
}

static float GetUtilitarianConfidence() {
    int known = CountKnownEnemies();

    // 0 врагов известно → Utilitarian почти бесполезен
    if (known == 0) return 0.2f;
    // 1-5 → начальные знания
    if (known <= 5)  return 0.35f;
    // 6-15 → растущая компетенция
    if (known <= 15) return 0.35f + (known - 5) * 0.025f;  // 0.375..0.60
    // 16-40 → опытная пешка
    if (known <= 40) return 0.60f + (known - 15) * 0.01f;  // 0.61..0.85
    // 40+ → мастер
    return 0.90f;
}

static void ApplySmartUtilitarian(float* incl) {
    if (!g_smartUtilEnabled) return;

    // 🔥 ПРИОРИТЕТ: CombatIntel (реальные данные боя)
    //    если врагов нет в бою → fallback на CountKnownEnemies
    float conf = GetCombatUtilitarianConfidence();
    if (conf == 0.5f)  // CombatIntel: нет боя → используем общую эвристику
        conf = GetUtilitarianConfidence();

    // Мало знаний → перенаправляем вес из Utilitarian в Scather+Challenger
    if (conf < 0.5f) {
        float excess = (0.5f - conf) * incl[I_UTILITARIAN];
        incl[I_UTILITARIAN] -= excess;
        incl[I_SCATHER]    += excess * 0.55f;
        incl[I_CHALLENGER] += excess * 0.30f;
        incl[I_MITIGATOR]  += excess * 0.15f;
    }

    // Мягкая коррекция к целевому уровню
    float target = conf * 850.0f;
    incl[I_UTILITARIAN] += (target - incl[I_UTILITARIAN]) * 0.02f;
}

// ═══════════════════════════════════════════════════════════
// ОБЩАЯ ОБРАБОТКА КАЖДЫЙ КАДР
// ═══════════════════════════════════════════════════════════

void SanitizeAllInclinations(float* incl) {
    if (!incl) return;
    ApplySanitaryCordon(incl);
    ApplySmartUtilitarian(incl);

    // Skill Use не должен уходить в крайности
    if (incl[I_SKILL_USE] < 300) incl[I_SKILL_USE] += 1.0f;
    if (incl[I_SKILL_USE] > 900) incl[I_SKILL_USE] -= 1.0f;
}

// ═══════════════════════════════════════════════════════════
// ПРЕСЕТЫ
// ═══════════════════════════════════════════════════════════

static void ApplyPresetInstant(float* incl, int idx) {
    for (int i = 0; i < I_COUNT; i++) incl[i] = presets[idx].v[i];
}

static void ApplyPresetSmooth(float* incl, int idx) {
    for (int i = 0; i < I_COUNT; i++)
        incl[i] += (presets[idx].v[i] - incl[i]) * (1.0f - g_smooth);
}

// ═══════════════════════════════════════════════════════════
// PER-FRAME UPDATE
// ═══════════════════════════════════════════════════════════

void UpdatePawnAI() {
    if (!g_enabled || !pBase || !*pBase) return;

    float incl[I_COUNT];
    ReadAllIncl(incl, 0);

    SanitizeAllInclinations(incl);

    if (g_presetsEnabled)
        ApplyPresetSmooth(incl, g_presetIdx);

    WriteAllIncl(incl, 0);
}

// ═══════════════════════════════════════════════════════════
// ImGui UI
// ═══════════════════════════════════════════════════════════

void RenderPawnAIUI() {
    if (!ImGui::CollapsingHeader("Pawn AI Overhaul v2.0"))
        return;
    ImGui::PushID("PawnAI");

    // --- Мастер ---
    if (ImGui::Checkbox("Enable Pawn AI", &g_enabled))
        config.setBool("pawnAI", "enabled", g_enabled);

    ImGui::Separator();

    // --- 1.3 Санитарный кордон ---
    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Phase 1.3: Sanitary Cordon");
    if (ImGui::Checkbox("Enable", &g_sanitaryEnabled))
        config.setBool("pawnAI", "sanitary", g_sanitaryEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Dynamic cap = 3rd highest useful inclination.\n"
                          "Guardian/Nexus/Acquisitor capped below it.\n"
                          "No hardcoded numbers — adapts to pawn!");

    // --- 1.4 Smart Utilitarian ---
    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Phase 1.4: Smart Utilitarian");
    if (ImGui::Checkbox("Enable", &g_smartUtilEnabled))
        config.setBool("pawnAI", "smartUtil", g_smartUtilEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Phase B: reads REAL mStudyFlag from memory!\n"
                          "Counts known enemies → adjusts Utilitarian weight.");

    int known = CountKnownEnemies();
    float combatConf = GetCombatUtilitarianConfidence();
    float totalConf = (combatConf != 0.5f) ? combatConf : GetUtilitarianConfidence();
    ImGui::SameLine();
    ImGui::ProgressBar(totalConf, ImVec2(80,0));
    ImGui::SameLine();
    ImGui::TextDisabled("%d known", known);
    ImGui::SameLine();
    if (combatConf != 0.5f)
        ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "combat:%.0f%%", combatConf*100);

    // --- mStudyFlag дамп ---
    if (ImGui::TreeNode("mStudyFlag Debug")) {
        ImGui::Text("Offset: pawn + 0x%X (%d bytes)", MSTUDYFLAG_OFFSET, MSTUDYFLAG_SIZE);
        ImGui::Text("Known enemies: %d / 322", known);

        if (pBase && *pBase) {
            BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
            ImGui::Text("First 32 bytes:");
            char hex[128] = {};
            for (int i = 0; i < 32 && i < MSTUDYFLAG_SIZE; i++)
                sprintf(hex + i*3, "%02X ", study[i]);
            ImGui::TextUnformatted(hex);
        }

        if (ImGui::Button("Rescan (use after scroll!)")) {
            int n = CountKnownEnemies();
            logFile << "mStudyFlag rescan: " << n << " known enemies" << std::endl;
        }
        ImGui::TreePop();
    }

    ImGui::Separator();

    // --- Пресеты ---
    ImGui::TextColored(ImVec4(0.6f,0.6f,1,1), "Phase 1.1-1.2: Presets");
    if (ImGui::Checkbox("Use Presets", &g_presetsEnabled))
        config.setBool("pawnAI", "presetsEnabled", g_presetsEnabled);

    const char* names[] = {"Boss Killer","Crowd Ctrl","Tactical Sup",
                           "Ranged Hunter","Explorer","Balanced"};
    ImGui::PushItemWidth(180);
    if (ImGui::Combo("Preset", &g_presetIdx, names, PRESET_COUNT))
        config.setInt("pawnAI", "preset", g_presetIdx);
    ImGui::SliderFloat("Smooth", &g_smooth, 0.0f, 1.0f, "%.2f");
    ImGui::PopItemWidth();

    if (ImGui::Button("Apply NOW"))
        { float f[I_COUNT]; ReadAllIncl(f,0); ApplyPresetInstant(f,g_presetIdx); WriteAllIncl(f,0); }
    ImGui::SameLine();
    if (ImGui::Button("Reset Balanced"))
        { float f[I_COUNT]; ReadAllIncl(f,0); ApplyPresetInstant(f,5); WriteAllIncl(f,0); }

    // --- Текущие значения ---
    if (ImGui::TreeNode("Live Inclinations")) {
        float incl[I_COUNT]; ReadAllIncl(incl, 0);

        float u[I_COUNT]; int n=0;
        for (int i=0;i<I_COUNT;i++)
            if (GetInclCategory(i)==CAT_USEFUL) u[n++]=incl[i];
        for (int i=0;i<n-1;i++)
            for (int j=i+1;j<n;j++)
                if (u[j]>u[i]){float t=u[i];u[i]=u[j];u[j]=t;}
        float cap = (n>=3) ? u[2] : 500;
        ImGui::TextDisabled("Dynamic cap: %.0f", cap);

        for (int i=0;i<I_COUNT;i++) {
            ImVec4 c;
            switch(GetInclCategory(i)) {
                case CAT_USEFUL:  c=ImVec4(0.3f,1,0.3f,1); break;
                case CAT_NEUTRAL: c=ImVec4(0.7f,0.7f,0.7f,1); break;
                case CAT_JUNK:    c=ImVec4(1,0.5f,0.3f,1); break;
            }
            ImGui::TextColored(c, "%-15s", InclNames[i]);
            ImGui::SameLine(160);
            ImGui::ProgressBar(incl[i]/1000.f, ImVec2(110,0));
            ImGui::SameLine();
            ImGui::Text("%6.0f", incl[i]);
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::TextDisabled("PawnAI v2.0 | stride=%d | mStudyFlag@+0x%X | GoG", INCL_STRIDE, MSTUDYFLAG_OFFSET);
    ImGui::PopID();
}

// ═══════════════════════════════════════════════════════════
// ИНИЦИАЛИЗАЦИЯ
// ═══════════════════════════════════════════════════════════

void Hooks::PawnAI() {
    g_enabled          = config.getBool("pawnAI", "enabled", true);
    g_presetsEnabled   = config.getBool("pawnAI", "presetsEnabled", true);
    g_sanitaryEnabled  = config.getBool("pawnAI", "sanitary", true);
    g_smartUtilEnabled = config.getBool("pawnAI", "smartUtil", true);
    g_presetIdx        = config.getInt("pawnAI", "preset", 5);
    g_smooth           = config.getFloat("pawnAI", "smooth", 0.1f);

    int known = CountKnownEnemies();
    logFile << "PawnAI v2.0 initialized" << std::endl;
    logFile << "  stride=" << INCL_STRIDE
            << " mStudyFlag@+" << std::hex << MSTUDYFLAG_OFFSET << std::dec
            << " knownEnemies=" << known
            << " confidence=" << GetUtilitarianConfidence() << std::endl;

    InGameUIAdd(RenderPawnAIUI);
}

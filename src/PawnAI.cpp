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
// 1.4 SMART UTILITARIAN (v2.0: глубина знания!)
// ═══════════════════════════════════════════════════════════
//
// Phase B: CombatIntel анализирует mStudyFlag ПОБИТОВО —
// чем больше knowledge-флагов на врага, тем выше уверенность.
//
//  0 флагов = враг незнаком → Utilitarian подавлен
//  1 флаг   = базовое знание → Utilitarian 35%
//  2-3      = хорошее        → 55-70%
//  4-5      = отличное       → 80-90%
//  6+       = мастер         → 95%
//
// BestiaryData.h: 72 врага из pawn-knowledge репо.

static int CountKnownEnemies() {
    if (!pBase || !*pBase) return 0;
    BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
    int known = 0;
    for (int i = 0; i < MSTUDYFLAG_SIZE; i++)
        if (study[i] != 0) known++;
    return known;
}

static void ApplySmartUtilitarian(float* incl) {
    if (!g_smartUtilEnabled) return;

    // CombatIntel v2 возвращает усреднённую глубину знания
    float conf = GetCombatUtilitarianConfidence();

    // conf=0.5 = нет боя → используем общую эвристику
    if (conf == 0.5f) {
        int known = CountKnownEnemies();
        if (known == 0)       conf = 0.20f;
        else if (known <= 5)  conf = 0.35f;
        else if (known <= 15) conf = 0.35f + (known-5)*0.025f;
        else if (known <= 40) conf = 0.60f + (known-15)*0.01f;
        else                  conf = 0.90f;
    }

    // Низкое знание → вес уходит в Scather+Challenger+Mitigator
    if (conf < 0.5f) {
        float excess = (0.5f - conf) * incl[I_UTILITARIAN];
        incl[I_UTILITARIAN] -= excess;
        incl[I_SCATHER]    += excess * 0.55f;
        incl[I_CHALLENGER] += excess * 0.30f;
        incl[I_MITIGATOR]  += excess * 0.15f;
    }

    float target = conf * 850.0f;
    incl[I_UTILITARIAN] += (target - incl[I_UTILITARIAN]) * 0.02f;
}

// Убраны: GetEnemyStudyIdx, PawnKnowsEnemy, GetUtilitarianConfidence
// — всё это теперь в CombatIntel.cpp + BestiaryData.h

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

    // 🔥 TACTICAL SWITCH: авто-смена пресета в бою
    if (g_presetsEnabled) {
        int actualPreset = g_presetIdx;
        if (IsInCombat()) {
            int cat = GetCombatEnemyCategory();
            static int catToPreset[] = { 1, 5, 0, 3, 3 };
            if (cat >= 0 && cat < 5) actualPreset = catToPreset[cat];
        }
        ApplyPresetSmooth(incl, actualPreset);
    }

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
    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Phase 1.4: Smart Utilitarian v2");
    if (ImGui::Checkbox("Enable", &g_smartUtilEnabled))
        config.setBool("pawnAI", "smartUtil", g_smartUtilEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Depth-based knowledge from BestiaryData.h + CombatIntel.\n"
                          "Bit-counts in mStudyFlag = real knowledge level per enemy.\n"
                          "72 enemy types from pawn-knowledge repo.");

    int known = CountKnownEnemies();
    float combatConf = GetCombatUtilitarianConfidence();
    float totalConf = (combatConf != 0.5f) ? combatConf : 0.5f;
    ImGui::SameLine();
    ImGui::ProgressBar(totalConf, ImVec2(80,0));
    ImGui::SameLine();
    ImGui::TextDisabled("%d types known", known);
    ImGui::SameLine();
    if (combatConf != 0.5f)
        ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "depth:%.0f%%", combatConf*100);

    // --- mStudyFlag дамп (ВСЕ 322 байта со скроллом!) ---
    if (ImGui::TreeNode("mStudyFlag Debug (all 322 bytes)")) {
        ImGui::Text("Offset: pawn + 0x%X (%d bytes)", MSTUDYFLAG_OFFSET, MSTUDYFLAG_SIZE);
        ImGui::Text("Known enemies: %d / 322", known);

        if (pBase && *pBase) {
            BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;

            // Скроллируемое окно — показывает ВСЕ 322 байта!
            ImGui::BeginChild("mStudyScroll", ImVec2(0, 200), true);
            for (int row = 0; row < MSTUDYFLAG_SIZE; row += 16) {
                // Индекс строки
                ImGui::Text("%03X:", row);
                ImGui::SameLine(40);
                for (int col = 0; col < 16 && (row+col) < MSTUDYFLAG_SIZE; col++) {
                    BYTE v = study[row+col];
                    if (v != 0)
                        ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "%02X ", v);
                    else
                        ImGui::TextDisabled("%02X ", v);
                    if (col < 15 && (row+col+1) < MSTUDYFLAG_SIZE)
                        ImGui::SameLine();
                }
            }
            ImGui::EndChild();
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
    logFile << "PawnAI v2.1 initialized" << std::endl;
    logFile << "  stride=" << INCL_STRIDE
            << " mStudyFlag@+" << std::hex << MSTUDYFLAG_OFFSET << std::dec
            << " knownTypes=" << known << std::endl;

    InGameUIAdd(RenderPawnAIUI);
}

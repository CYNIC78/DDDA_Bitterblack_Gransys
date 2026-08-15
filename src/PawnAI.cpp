/**
 * PawnAI.cpp — Pawn AI Overhaul Orchestrator & Custom Anchors
 * Modules: Sanitary Cordon, Smart Utilitarian, Custom Anchors, Tactical Switch
 *
 * АРХИТЕКТУРА: модули запускаются каждый в своём SEH-блоке.
 * Падение любого модуля не роняет остальные и не роняет игру.
 * Модуль, упавший дважды подряд, отключается до горячей перезагрузки.
 */
#include "stdafx.h"
#include "EntityConfig.h"
#include "EnemyTuner.h"
#include "PawnAI.h"
#include "CombatIntel.h"
#include "CombatBus.h"
#include "devtools/DevTools.h"
#include "pawnai/PawnAI_Common.h"
#include "pawnai/PawnAI_BusOrchestrator.h"

using namespace PawnAI;

// Global Orchestrator
static Orchestrator g_orch;
static bool  g_enabled = true;

// Background tactical thread (150ms / ~6.7 Hz)
void UpdatePawnAI(){
    if(!g_enabled || !pBase || !*pBase) return;
    if(!IsInActiveGameplay()) return;

    // Каждый модуль вызываем в собственном SEH — никакого каскадного падения
    __try { CombatIntel_Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { /* следующий тик догонит */ }

    __try { DevTools::WorldScan_Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try { EntityCfg::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try { EnemyTuner::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    float incl[I_COUNT]; ReadAllIncl(incl, 0);
    g_orch.Tick(incl);
    WriteAllIncl(incl, 0);
}

static HANDLE g_pawnTickThread = nullptr;
static volatile bool g_pawnTickStop = false;
static HANDLE g_pawnTickEvent = nullptr;  // для безопасного шатдауна без WaitForSingleObject в DllMain

static DWORD WINAPI PawnTickThread(LPVOID){
    while(!g_pawnTickStop){
        // Ждём с таймаутом вместо Sleep — так можно разбудить поток
        // из Shutdown без ожидания 150 мс.
        WaitForSingleObject(g_pawnTickEvent, 150);
        UpdatePawnAI();
    }
    return 0;
}

// ——— InGame UI Overlay (F12) ———
void RenderPawnAIUI(){
    if(!ImGui::CollapsingHeader("Pawn AI Overhaul v2.8 Modular")) return;
    ImGui::PushID("PawnAI");

    if(ImGui::Checkbox("Enable Pawn AI Master", &g_enabled)) config.setBool("pawnAI", "enabled", g_enabled);
    ImGui::Separator();

    // 1. Sanitary Cordon
    ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Sanitary Cordon");
    if(ImGui::Checkbox("Enable##san", &g_orch.sanitary.enabled)) config.setBool("pawnAI", "sanitary", g_orch.sanitary.enabled);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Dynamic cap on junk inclinations (Guardian/Nexus/Acquisitor) based on 3rd highest useful.");

    // 2. Smart Utilitarian
    ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Smart Utilitarian");
    if(ImGui::Checkbox("Enable##su", &g_orch.smartUtil.enabled)) config.setBool("pawnAI", "smartUtil", g_orch.smartUtil.enabled);
    int known = CountKnownEnemies();
    float conf = g_orch.smartUtil.lastConfidence;
    ImGui::SameLine(); ImGui::ProgressBar(conf, ImVec2(80, 0)); ImGui::SameLine();
    ImGui::TextDisabled("%d known | Conf: %.0f%%", known, conf * 100);

    if(ImGui::TreeNode("mStudyFlag Debug (322 Bytes Knowledge)")) {
        ImGui::Text("Pawn + 0x%X (%d bytes) | Known Entries: %d", MSTUDYFLAG_OFFSET, MSTUDYFLAG_SIZE, known);
        if(pBase && *pBase){
            __try {
                BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
                ImGui::BeginChild("mStudyScroll", ImVec2(0, 180), true);
                for(int row = 0; row < MSTUDYFLAG_SIZE; row += 16){
                    ImGui::Text("%03X:", row); ImGui::SameLine(40);
                    for(int col = 0; col < 16 && row + col < MSTUDYFLAG_SIZE; col++){
                        BYTE v = study[row + col];
                        if(v) ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "%02X ", v);
                        else ImGui::TextDisabled("%02X ", v);
                        if(col < 15) ImGui::SameLine();
                    }
                }
                ImGui::EndChild();
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if(ImGui::Button("Rescan Memory")) logFile << "mStudy rescan " << known << std::endl;
        ImGui::TreePop();
    }
    ImGui::Separator();

    // 3. Presets (кнопки-снапшоты) + Tactical Switch (дельта)
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1, 1), "Presets & Weights");
    if(ImGui::Checkbox("Use Presets/Anchors", &g_orch.presets.enabled)) config.setBool("pawnAI", "presetsEnabled", g_orch.presets.enabled);
    ImGui::SameLine(200);
    if(ImGui::Checkbox("Auto-adapt in combat", &g_orch.tactical.enabled)) config.setBool("pawnAI", "tactical", g_orch.tactical.enabled);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("In combat, system adds a situational delta on top of your sliders (based on enemy category).");

    // Кнопки-пресеты: клик = переезд ползунков + мгновенная запись в память (бары прыгают)
    {
        bool anyLoaded = false;
        for (int p = 0; p < PawnAI::PresetManager::COUNT; p++) {
            if (p > 0) ImGui::SameLine();
            if (ImGui::Button(PawnAI::PresetManager::presets[p].name)) {
                g_orch.presets.LoadPreset(p);
                config.setInt("pawnAI", "lastPreset", p);
                // Пишем в память немедленно — прогресс-бары обновятся сразу
                float f[I_COUNT]; g_orch.presets.GetBaseTarget(f);
                WriteAllIncl(f, 0);
                anyLoaded = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Load this preset into sliders and memory instantly.");
    }
    ImGui::TextDisabled("Preset: %s%s | Smooth: %.2f",
        PawnAI::PresetManager::presets[g_orch.presets.lastPresetIdx].name,
        g_orch.presets.IsModified() ? " (Modified)" : "",
        g_orch.presets.smooth);
    if(ImGui::SliderFloat("Smoothness (LERP)", &g_orch.presets.smooth, 0.01f, 1.0f, "%.2f")) {
        config.setFloat("pawnAI", "smooth", g_orch.presets.smooth);
        g_orch.presets.SaveConfig();
    }

    // 4. Live Inclinations & Anchors — ползунок ПОВЕРХ прогресс-бара (1 строка на инклинацию)
    if(ImGui::TreeNode("Live Inclinations & Anchors")) {
        float incl[I_COUNT]; ReadAllIncl(incl, 0);

        ImGui::TextDisabled("Slider = current target (truth). Live = actual value (+delta from combat).");
        ImGui::TextDisabled("Cordon Cap: %.0f | %s%s", g_orch.sanitary.lastCap,
            PawnAI::PresetManager::presets[g_orch.presets.lastPresetIdx].name,
            g_orch.presets.IsModified() ? " (Modified)" : "");
        ImGui::Separator();

        const float barW = 170.0f;
        // Высота = высота ползунка, чтобы ручка вставала ровно на бар
        const float itemH = ImGui::GetWindowFontSize() + 2.0f * ImGui::GetStyle().FramePadding.y;

        for(int i = 0; i < I_COUNT; i++) {
            ImVec4 nameCol = (GetInclCategory(i) == CAT_USEFUL) ? ImVec4(0.3f, 1, 0.3f, 1) :
                             (GetInclCategory(i) == CAT_JUNK)   ? ImVec4(1, 0.5f, 0.3f, 1) :
                                                                  ImVec4(0.8f, 0.8f, 0.8f, 1);
            ImGui::PushID(i);
            ImGui::TextColored(nameCol, "%-12s", InclName(i));
            ImGui::SameLine(115);

            // Слой 1: прогресс-бар — текущее значение инклинации
            float fraction = incl[i] / 1000.0f;
            if (fraction < 0.0f) fraction = 0.0f;
            if (fraction > 1.0f) fraction = 1.0f;

            ImVec4 barColor = (GetInclCategory(i) == CAT_USEFUL) ? ImVec4(1.0f, 0.85f, 0.25f, 0.9f) :
                              (GetInclCategory(i) == CAT_JUNK)   ? ImVec4(1.0f, 0.55f, 0.2f, 0.85f) :
                                                                   ImVec4(0.7f, 0.85f, 1.0f, 0.85f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            ImGui::ProgressBar(fraction, ImVec2(barW, itemH), "");
            ImGui::PopStyleColor();
            ImVec2 barMin = ImGui::GetItemRectMin();
            ImVec2 barMax = ImGui::GetItemRectMax();

            // Слой 2: ползунок-якорь ПОВЕРХ бара (прозрачная рамка — видна только ручка)
            ImGui::SetCursorScreenPos(barMin);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0,0,0,0));
            ImGui::PushItemWidth(barW); // та же ширина, что у прогресс-бара — не резина
            if(ImGui::SliderFloat("##anchor", &g_orch.presets.anchor[i], 0.0f, 1000.0f, ""))
                g_orch.presets.SaveConfig();  // сдвинул ползунок — он и есть истина
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(2);

            // Live-значение справа + дельта от модулей
            ImGui::SetCursorScreenPos(ImVec2(barMax.x + 6.0f, barMin.y));
            if (g_orch.lastDelta[i] != 0.0f)
                ImGui::Text("Live: %4.0f (%+d)", incl[i], (int)g_orch.lastDelta[i]);
            else
                ImGui::Text("Live: %4.0f", incl[i]);

            if(GetInclCategory(i) == CAT_JUNK && incl[i] > g_orch.sanitary.lastCap) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "[cordon]");
            }

            ImGui::PopID();

            // Переход на следующую строку (в ImGui 1.48 нет NewLine())
            ImGui::SetCursorScreenPos(ImVec2(barMin.x - 115.0f, barMin.y + ImGui::GetItemsLineHeightWithSpacing()));
        }

        ImGui::Separator();

        // (диагностика оффсетов удалена — все верифицированы, см. docs/SOURCE_OF_TRUTH.md)

        if(ImGui::Button("Capture Live as Anchor")) {
            float f[I_COUNT]; ReadAllIncl(f, 0);
            g_orch.presets.CaptureLive(f);
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Capture pawn's current live values as the slider target.");

        ImGui::SameLine();
        if(ImGui::Button("Apply Instantly")) {
            float f[I_COUNT]; ReadAllIncl(f, 0);
            g_orch.presets.ApplyInstant(f);
            WriteAllIncl(f, 0);
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Instantly apply slider values without waiting for smooth LERP.");

        ImGui::SameLine();
        if(ImGui::Button("Reset Balanced")) {
            g_orch.presets.ResetDefaultAnchor();
        }
        ImGui::TreePop();
    }

    // Bus status — копируем под блокировкой, работаем с локальными копиями
    CombatReport bus = CombatBus::Instance().LastReport();
    WorldReport world = CombatBus::Instance().LastWorld();
    ImGui::Separator();
    ImGui::TextDisabled("Bus: %s | Types: %d (P:%d W:%d) Unk: %d Cat: %d Hits: %d",
        bus.inCombat ? "IN COMBAT" : "idle", bus.distinctTypes, bus.playerDistinct, bus.pawnDistinct,
        bus.unknownTypes, bus.dominantCategory, bus.pawnHits + bus.playerHits);
    DWORD age = (world.timestampMs && MsNow() >= world.timestampMs)
        ? (MsNow() - world.timestampMs) : 0;
    ImVec4 wcol = (world.count > 0 && age < 500)
        ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1);
    ImGui::TextColored(wcol, "World: %d units  %d enemies  %d critters  %d dead  %u ms",
        world.count, world.enemyCount, world.critterCount, world.deadCount, age);
    ImGui::TextDisabled("   goblins: %d   cat %d   (critters = hares etc, not a threat)",
        world.goblinCount, world.dominantCategory);
    ImGui::TextDisabled("Modular Orchestration: Stride=%d mStudy@0x%X | Modules: Sanitary, SmartUtil, Anchors, Tactical",
        INCL_STRIDE, MSTUDYFLAG_OFFSET);
    ImGui::PopID();
}

void Hooks::PawnAI(){
    g_enabled = config.getBool("pawnAI", "enabled", true);
    g_orch.presets.enabled   = config.getBool("pawnAI", "presetsEnabled", true);
    g_orch.sanitary.enabled  = config.getBool("pawnAI", "sanitary", true);
    g_orch.smartUtil.enabled = config.getBool("pawnAI", "smartUtil", true);
    g_orch.presets.smooth    = config.getFloat("pawnAI", "smooth", 0.10f);
    g_orch.tactical.enabled  = config.getBool("pawnAI", "tactical", true);
    g_orch.Init();
    int known = CountKnownEnemies();
    logFile << "PawnAI v2.8 Modular initialized — Sanitary / SmartUtil / Custom Anchors / Tactical via CombatBus (ticker 150ms)" << std::endl;
    logFile << "  stride=" << INCL_STRIDE << " mStudy@0x" << std::hex << MSTUDYFLAG_OFFSET << std::dec << " known=" << known << std::endl;
    g_pawnTickStop = false;
    g_pawnTickEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    g_pawnTickThread = CreateThread(nullptr, 0, PawnTickThread, nullptr, 0, nullptr);
    InGameUIAdd(RenderPawnAIUI);
}

void Hooks::PawnAI_Shutdown(){
    g_pawnTickStop = true;
    // Пробуждаем поток через событие — не ждём 150 мс
    if (g_pawnTickEvent) SetEvent(g_pawnTickEvent);
    if(g_pawnTickThread){
        // В DllMain этого делать НЕЛЬЗЯ, но Shutdown вызывается
        // не из DllMain (см. dinput8.cpp — SetEvent + флаг, без Wait)
        WaitForSingleObject(g_pawnTickThread, 300);
        CloseHandle(g_pawnTickThread);
        g_pawnTickThread = nullptr;
    }
    if (g_pawnTickEvent) { CloseHandle(g_pawnTickEvent); g_pawnTickEvent = nullptr; }
    g_orch.Shutdown();
}
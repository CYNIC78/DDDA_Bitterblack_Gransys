/**
 * PawnAI.cpp — Pawn AI Overhaul Orchestrator & Custom Anchors
 * Modules: Sanitary Cordon, Smart Utilitarian, Custom Anchors, Tactical Switch
 */
#include "stdafx.h"
#include "PawnAI.h"
#include "CombatIntel.h"
#include "CombatBus.h"
#include "pawnai/PawnAI_Common.h"
#include "pawnai/PawnAI_BusOrchestrator.h"

using namespace PawnAI;

// Global Orchestrator
static Orchestrator g_orch;
static bool  g_enabled = true;

// Background tactical thread (150ms / ~6.7 Hz)
void UpdatePawnAI(){
    if(!g_enabled || !pBase || !*pBase) return;
    CombatIntel_Tick(); // Background combat sync with CombatBus
    float incl[I_COUNT]; ReadAllIncl(incl, 0);
    g_orch.Tick(incl);
    WriteAllIncl(incl, 0);
}

static HANDLE g_pawnTickThread = nullptr;
static volatile bool g_pawnTickStop = false;
static DWORD WINAPI PawnTickThread(LPVOID){
    while(!g_pawnTickStop){
        Sleep(150); // 150ms tactical cadence
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
        }
        if(ImGui::Button("Rescan Memory")) logFile << "mStudy rescan " << known << std::endl;
        ImGui::TreePop();
    }
    ImGui::Separator();

    // 3. Presets + Tactical Switch
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1, 1), "Presets & Custom Anchors");
    if(ImGui::Checkbox("Use Presets/Anchors", &g_orch.presets.enabled)) config.setBool("pawnAI", "presetsEnabled", g_orch.presets.enabled);
    ImGui::SameLine(200);
    if(ImGui::Checkbox("Tactical Switch (Auto-adapt)", &g_orch.tactical.enabled)) config.setBool("pawnAI", "tactical", g_orch.tactical.enabled);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically adapts active profile based on enemy category in combat (Boss, Flying, Horde).");

    const char* names[] = {
        "0: Boss Killer", "1: Crowd Ctrl", "2: Tactical Sup",
        "3: Ranged Hunter", "4: Explorer", "5: Balanced",
        "6: Custom Anchor"
    };
    ImGui::PushItemWidth(200);
    if(ImGui::Combo("Active Profile", &g_orch.presets.presetIdx, names, 7)) {
        config.setInt("pawnAI", "preset", g_orch.presets.presetIdx);
        g_orch.presets.SaveConfig();
    }
    if(ImGui::SliderFloat("Smoothness (LERP)", &g_orch.presets.smooth, 0.01f, 1.0f, "%.2f")) {
        config.setFloat("pawnAI", "smooth", g_orch.presets.smooth);
        g_orch.presets.SaveConfig();
    }
    ImGui::PopItemWidth();

    // 4. Live Inclinations & Custom Anchors with Dual Sliders + Progress Bars
    if(ImGui::TreeNode("Live Inclinations & Custom Anchors")) {
        float incl[I_COUNT]; ReadAllIncl(incl, 0);

        // Dynamic sanitary cordon cap calculation
        float useful[I_COUNT]; int n = 0;
        for(int i = 0; i < I_COUNT; i++) if(GetInclCategory(i) == CAT_USEFUL) useful[n++] = incl[i];
        for(int i = 0; i < n - 1; i++) for(int j = i + 1; j < n; j++) if(useful[j] > useful[i]) { float t = useful[i]; useful[i] = useful[j]; useful[j] = t; }
        float cap = (n >= 3) ? useful[2] : 500.0f;

        ImGui::TextDisabled("Cordon Cap: %.0f | Active Profile: %s", cap, names[g_orch.presets.presetIdx]);
        ImGui::Separator();

        for(int i = 0; i < I_COUNT; i++) {
            ImVec4 nameCol = (GetInclCategory(i) == CAT_USEFUL) ? ImVec4(0.3f, 1, 0.3f, 1) :
                             (GetInclCategory(i) == CAT_JUNK)   ? ImVec4(1, 0.5f, 0.3f, 1) :
                                                                  ImVec4(0.8f, 0.8f, 0.8f, 1);
            ImGui::PushID(i);
            ImGui::TextColored(nameCol, "%-12s", InclName(i));
            ImGui::SameLine(115);

            // Interactive Anchor Slider
            ImGui::PushItemWidth(170);
            if(ImGui::SliderFloat("##anchor", &g_orch.presets.customAnchor[i], 0.0f, 1000.0f, "Set: %.0f")) {
                g_orch.presets.presetIdx = 6; // Automatically activate custom anchor mode on slider drag
                g_orch.presets.SaveConfig();
            }
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::Text("Live: %4.0f", incl[i]);

            if(GetInclCategory(i) == CAT_JUNK && incl[i] > cap) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "[Cordon Active]");
            }

            // Live progress bar directly under slider
            ImGui::SetCursorPosX(115);
            float fraction = incl[i] / 1000.0f;
            if (fraction < 0.0f) fraction = 0.0f;
            if (fraction > 1.0f) fraction = 1.0f;

            ImVec4 barColor = (GetInclCategory(i) == CAT_USEFUL) ? ImVec4(1.0f, 0.85f, 0.25f, 0.9f) :
                              (GetInclCategory(i) == CAT_JUNK)   ? ImVec4(1.0f, 0.55f, 0.2f, 0.85f) :
                                                                   ImVec4(0.7f, 0.85f, 1.0f, 0.85f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            ImGui::ProgressBar(fraction, ImVec2(170, 5), "");
            ImGui::PopStyleColor();

            ImGui::PopID();
        }

        ImGui::Separator();
        if(ImGui::Button("Capture Live as Anchor")) {
            float f[I_COUNT]; ReadAllIncl(f, 0);
            g_orch.presets.CaptureLive(f);
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Capture pawn's current live values and save as custom anchor in INI.");

        ImGui::SameLine();
        if(ImGui::Button("Apply Instantly")) {
            float f[I_COUNT]; ReadAllIncl(f, 0);
            g_orch.presets.ApplyInstant(f, g_orch.presets.presetIdx);
            WriteAllIncl(f, 0);
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Instantly apply target values without waiting for smooth LERP.");

        ImGui::SameLine();
        if(ImGui::Button("Reset Balanced")) {
            g_orch.presets.ResetDefaultAnchor();
        }
        ImGui::TreePop();
    }

    // Bus status
    auto &bus = CombatBus::Instance().LastReport();
    ImGui::Separator();
    ImGui::TextDisabled("Bus: %s | Types: %d (P:%d W:%d) Unk: %d Cat: %d Hits: %d",
        bus.inCombat ? "IN COMBAT" : "idle", bus.distinctTypes, bus.playerDistinct, bus.pawnDistinct,
        bus.unknownTypes, bus.dominantCategory, bus.pawnHits + bus.playerHits);
    ImGui::TextDisabled("Modular Orchestration: Stride=%d mStudy@0x%X | Modules: Sanitary, SmartUtil, Anchors, Tactical",
        INCL_STRIDE, MSTUDYFLAG_OFFSET);
    ImGui::PopID();
}

void Hooks::PawnAI(){
    g_enabled = config.getBool("pawnAI", "enabled", true);
    g_orch.presets.enabled   = config.getBool("pawnAI", "presetsEnabled", true);
    g_orch.sanitary.enabled  = config.getBool("pawnAI", "sanitary", true);
    g_orch.smartUtil.enabled = config.getBool("pawnAI", "smartUtil", true);
    g_orch.presets.presetIdx = config.getInt("pawnAI", "preset", 5);
    g_orch.presets.smooth    = config.getFloat("pawnAI", "smooth", 0.10f);
    g_orch.tactical.enabled  = config.getBool("pawnAI", "tactical", true);
    g_orch.Init();
    int known = CountKnownEnemies();
    logFile << "PawnAI v2.8 Modular initialized — Sanitary / SmartUtil / Custom Anchors / Tactical via CombatBus (ticker 150ms)" << std::endl;
    logFile << "  stride=" << INCL_STRIDE << " mStudy@0x" << std::hex << MSTUDYFLAG_OFFSET << std::dec << " known=" << known << std::endl;
    g_pawnTickStop = false;
    g_pawnTickThread = CreateThread(nullptr, 0, PawnTickThread, nullptr, 0, nullptr);
    InGameUIAdd(RenderPawnAIUI);
}

void Hooks::PawnAI_Shutdown(){
    g_pawnTickStop = true;
    if(g_pawnTickThread){
        WaitForSingleObject(g_pawnTickThread, 300);
        CloseHandle(g_pawnTickThread);
        g_pawnTickThread = nullptr;
    }
    g_orch.Shutdown();
}

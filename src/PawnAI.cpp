/**
 * PawnAI.cpp — ТОНКИЙ ОРКЕСТРАТОР (рефактор из монолита 408 строк → 80 строк)
 * Вся логика — в src/pawnai/*, каждый модуль независим и слушает CombatBus (мегафон тренера).
 * Старая логика сохранена, просто разнесена по модулям. Производительность та же, зато расширять — магия.
 */
#include "stdafx.h"
#include "PawnAI.h"
#include "CombatIntel.h"
#include "CombatBus.h"
#include "pawnai/PawnAI_Common.h"
#include "pawnai/PawnAI_BusOrchestrator.h"

using namespace PawnAI;

// Глобальный оркестратор — единственный кто тикает
static Orchestrator g_orch;
static bool  g_enabled = true;

// Для UI нужны те же имена что и раньше


// GetPawnInclination уже в PawnAI_Common.h

// Per-frame — тикает в фоне, чтобы Live Inclinations ожили
void UpdatePawnAI(){
    if(!g_enabled || !pBase || !*pBase) return;
    float incl[I_COUNT]; ReadAllIncl(incl,0);
    g_orch.Tick(incl);
    WriteAllIncl(incl,0);
}
static HANDLE g_pawnTickThread = nullptr;
static volatile bool g_pawnTickStop = false;
static DWORD WINAPI PawnTickThread(LPVOID){
    while(!g_pawnTickStop){
        Sleep(150); // тактический темп, не каждый кадр
        UpdatePawnAI();
    }
    return 0;
}

// ——— UI остаётся здесь, чтобы не плодить файлы ———
void RenderPawnAIUI(){
    if(!ImGui::CollapsingHeader("Pawn AI Overhaul v2.1 Modular")) return;
    ImGui::PushID("PawnAI");

    if(ImGui::Checkbox("Enable Pawn AI", &g_enabled)) config.setBool("pawnAI","enabled", g_enabled);
    ImGui::Separator();

    // Санитария
    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1),"Sanitary Cordon (модуль)");
    if(ImGui::Checkbox("Enable##san", &g_orch.sanitary.enabled)) config.setBool("pawnAI","sanitary", g_orch.sanitary.enabled);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Dynamic cap = 3rd useful. Слушает шину, но тикает сам.");

    // Smart Util
    ImGui::TextColored(ImVec4(0.3f,1,0.3f,1),"Smart Utilitarian (модуль-слушатель)");
    if(ImGui::Checkbox("Enable##su", &g_orch.smartUtil.enabled)) config.setBool("pawnAI","smartUtil", g_orch.smartUtil.enabled);
    int known = CountKnownEnemies();
    float conf = g_orch.smartUtil.lastConfidence;
    ImGui::SameLine(); ImGui::ProgressBar(conf, ImVec2(80,0)); ImGui::SameLine();
    ImGui::TextDisabled("%d known | conf %.0f%%", known, conf*100);

    if(ImGui::TreeNode("mStudyFlag Debug (all 322 bytes)")){
        ImGui::Text("pawn + 0x%X (%d bytes) | known %d", MSTUDYFLAG_OFFSET, MSTUDYFLAG_SIZE, known);
        if(pBase && *pBase){
            BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
            ImGui::BeginChild("mStudyScroll", ImVec2(0,200), true);
            for(int row=0; row<MSTUDYFLAG_SIZE; row+=16){
                ImGui::Text("%03X:", row); ImGui::SameLine(40);
                for(int col=0; col<16 && row+col<MSTUDYFLAG_SIZE; col++){
                    BYTE v=study[row+col];
                    if(v) ImGui::TextColored(ImVec4(0.3f,1,0.3f,1),"%02X ",v); else ImGui::TextDisabled("%02X ",v);
                    if(col<15) ImGui::SameLine();
                }
            }
            ImGui::EndChild();
        }
        if(ImGui::Button("Rescan")) logFile<<"mStudy rescan "<<known<<std::endl;
        ImGui::TreePop();
    }
    ImGui::Separator();

    // Пресеты + Tactical Switch
    ImGui::TextColored(ImVec4(0.6f,0.6f,1,1),"Presets + Tactical Switch (модуль)");
    if(ImGui::Checkbox("Use Presets", &g_orch.presets.enabled)) config.setBool("pawnAI","presetsEnabled", g_orch.presets.enabled);
    if(ImGui::Checkbox("Tactical Switch", &g_orch.tactical.enabled)) config.setBool("pawnAI","tactical", g_orch.tactical.enabled);
    const char* names[]={"Boss Killer","Crowd Ctrl","Tactical Sup","Ranged Hunter","Explorer","Balanced"};
    ImGui::PushItemWidth(180);
    if(ImGui::Combo("Preset", &g_orch.presets.presetIdx, names, 6)) config.setInt("pawnAI","preset", g_orch.presets.presetIdx);
    if(ImGui::SliderFloat("Smooth", &g_orch.presets.smooth, 0.0f, 1.0f, "%.2f")) config.setFloat("pawnAI","smooth", g_orch.presets.smooth);
    ImGui::PopItemWidth();
    if(ImGui::Button("Apply NOW")){ float f[I_COUNT]; ReadAllIncl(f,0); g_orch.presets.ApplyInstant(f,g_orch.presets.presetIdx); WriteAllIncl(f,0); }
    ImGui::SameLine(); if(ImGui::Button("Reset Balanced")){ float f[I_COUNT]; ReadAllIncl(f,0); g_orch.presets.ApplyInstant(f,5); WriteAllIncl(f,0); }

    if(ImGui::TreeNode("Live Inclinations")){
        float incl[I_COUNT]; ReadAllIncl(incl,0);
        float useful[I_COUNT]; int n=0; for(int i=0;i<I_COUNT;i++) if(GetInclCategory(i)==CAT_USEFUL) useful[n++]=incl[i];
        for(int i=0;i<n-1;i++) for(int j=i+1;j<n;j++) if(useful[j]>useful[i]){ float t=useful[i]; useful[i]=useful[j]; useful[j]=t; }
        float cap=(n>=3)?useful[2]:500; ImGui::TextDisabled("Dynamic cap: %.0f",cap);
        for(int i=0;i<I_COUNT;i++){
            ImVec4 c= (GetInclCategory(i)==CAT_USEFUL? ImVec4(0.3f,1,0.3f,1) : GetInclCategory(i)==CAT_JUNK? ImVec4(1,0.5f,0.3f,1) : ImVec4(0.7f,0.7f,0.7f,1));
            ImGui::TextColored(c,"%-15s", InclName(i)); ImGui::SameLine(160); ImGui::ProgressBar(incl[i]/1000.f, ImVec2(110,0)); ImGui::SameLine(); ImGui::Text("%6.0f",incl[i]);
        }
        ImGui::TreePop();
    }
    // Bus status
    auto &bus = CombatBus::Instance().LastReport();
    ImGui::Separator(); ImGui::TextDisabled("Bus: %s | types:%d unk:%d cat:%d", bus.inCombat?"IN COMBAT":"idle", bus.distinctTypes, bus.unknownTypes, bus.dominantCategory);
    ImGui::TextDisabled("Modular: stride=%d mStudy@0x%X | modules: sanitary, smartUtil, tactical", INCL_STRIDE, MSTUDYFLAG_OFFSET);
    ImGui::PopID();
}

void Hooks::PawnAI(){
    g_enabled = config.getBool("pawnAI","enabled",true);
    g_orch.presets.enabled   = config.getBool("pawnAI","presetsEnabled",true);
    g_orch.sanitary.enabled  = config.getBool("pawnAI","sanitary",true);
    g_orch.smartUtil.enabled = config.getBool("pawnAI","smartUtil",true);
    g_orch.presets.presetIdx = config.getInt("pawnAI","preset",5);
    g_orch.presets.smooth    = config.getFloat("pawnAI","smooth",0.1f);
    g_orch.tactical.enabled  = config.getBool("pawnAI","tactical",true);
    g_orch.Init();
    int known=CountKnownEnemies();
    logFile<<"PawnAI v2.1 Modular initialized — sanitary/smartUtil/tactical via CombatBus (ticker 150ms)"<<std::endl;
    logFile<<"  stride="<<INCL_STRIDE<<" mStudy@0x"<<std::hex<<MSTUDYFLAG_OFFSET<<std::dec<<" known="<<known<<std::endl;
    g_pawnTickStop=false;
    g_pawnTickThread = CreateThread(nullptr,0,PawnTickThread,nullptr,0,nullptr);
    InGameUIAdd(RenderPawnAIUI);
}
void Hooks::PawnAI_Shutdown(){
    g_pawnTickStop=true;
    if(g_pawnTickThread){ WaitForSingleObject(g_pawnTickThread,300); CloseHandle(g_pawnTickThread); g_pawnTickThread=nullptr; }
    g_orch.Shutdown();
}

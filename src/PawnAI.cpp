/**
 * PawnAI.cpp — Pawn AI Overhaul Orchestrator & Custom Anchors
 * Modules: Acquisitor Manager, Smart Utilitarian, Custom Anchors, Tactical Switch
 *
 * Build 55: Sanitary Cordon заменён на AcquisitorManager. Guardian/Nexus
 * вышли из-под кордона (CAT_DOCTRINE) — их реализацией займётся GuardianDoctrine.
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
#include "pawnai/GuardianDoctrine.h"

using namespace PawnAI;

// Global Orchestrator
static Orchestrator g_orch;
static bool  g_enabled = true;

// Background tactical thread (150ms / ~6.7 Hz)
void UpdatePawnAI(){
    // DevTools owns rollback-safe diagnostics. Let it observe world unload
    // before the gameplay guards return, even when Pawn AI itself is disabled.
    __try { DevTools::WorldScan_Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    if(!g_enabled || !pBase || !*pBase) return;
    if(!IsInActiveGameplay()) return;

    // Каждый модуль вызываем в собственном SEH — никакого каскадного падения
    __try { CombatIntel_Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { /* следующий тик догонит */ }

    __try { EntityCfg::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try { EnemyTuner::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Build 57.1: динамический Guardian-фикс (включён только при guardianFix=on).
    __try { PawnAI::GuardianDoctrineTick(); }
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

    // 1. Acquisitor Manager (бывший Sanitary Cordon)
    ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Acquisitor Manager");
    if(ImGui::Checkbox("Enable##acq", &g_orch.acquisitor.enabled)) config.setBool("pawnAI", "acquisitor", g_orch.acquisitor.enabled);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Soft manager for Acquisitor only: suppressed in combat, temporarily boosted out of combat (loot vacuum). Guardian/Nexus are now doctrines and are NOT capped.");

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
        ImGui::TextDisabled("Acquisitor mgr: combat=suppress, idle=loot boost | %s%s",
            PawnAI::PresetManager::presets[g_orch.presets.lastPresetIdx].name,
            g_orch.presets.IsModified() ? " (Modified)" : "");
        ImGui::Separator();

        const float barW = 170.0f;
        // Высота = высота ползунка, чтобы ручка вставала ровно на бар
        const float itemH = ImGui::GetWindowFontSize() + 2.0f * ImGui::GetStyle().FramePadding.y;

        for(int i = 0; i < I_COUNT; i++) {
            ImVec4 nameCol = (GetInclCategory(i) == CAT_USEFUL)   ? ImVec4(0.3f, 1, 0.3f, 1) :
                             (GetInclCategory(i) == CAT_DOCTRINE) ? ImVec4(0.5f, 0.6f, 1, 1) :
                             (GetInclCategory(i) == CAT_JUNK)     ? ImVec4(1, 0.5f, 0.3f, 1) :
                                                                    ImVec4(0.8f, 0.8f, 0.8f, 1);
            ImGui::PushID(i);
            ImGui::TextColored(nameCol, "%-12s", InclName(i));
            ImGui::SameLine(115);

            // Слой 1: прогресс-бар — текущее значение инклинации
            float fraction = incl[i] / 1000.0f;
            if (fraction < 0.0f) fraction = 0.0f;
            if (fraction > 1.0f) fraction = 1.0f;

            ImVec4 barColor = (GetInclCategory(i) == CAT_USEFUL)   ? ImVec4(1.0f, 0.85f, 0.25f, 0.9f) :
                              (GetInclCategory(i) == CAT_DOCTRINE) ? ImVec4(0.5f, 0.6f, 1.0f, 0.9f) :
                              (GetInclCategory(i) == CAT_JUNK)     ? ImVec4(1.0f, 0.55f, 0.2f, 0.85f) :
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

            if(i == I_ACQUISITOR && g_orch.acquisitor.lastState == PawnAI::AcquisitorManager::ST_SUPPRESS) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "[mgr]");
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

    // 5. Guardian Doctrine (единая секция, без номеров билдов)
    if(ImGui::TreeNode("Guardian Doctrine")) {
        uintptr_t playerRec = 0, pawnRec = 0;
        if(pBase && *pBase){
            playerRec = (uintptr_t)(*pBase) + PLAYER_BASE;
            pawnRec   = playerRec + PAWN_OFFSET;
        }
        int arisenVoc = ReadVocation(playerRec);
        int pawnVoc   = ReadVocation(pawnRec);

        ImGui::Text("Arisen: %s (%s) | Main pawn: %s (%s)",
            VocationName(arisenVoc), VocationClassName(VocationClassOf(arisenVoc)),
            VocationName(pawnVoc),   VocationClassName(VocationClassOf(pawnVoc)));

        // Доктрина — observe-only. Decide() ничего не пишет в игру.
        static PawnAI::GuardianDoctrine doctrine;
        doctrine.worldUnitsPerMeter = config.getFloat("pawnAI", "worldUnitsPerMeter", 100.0f);
        PawnAI::GuardianSitRep sit;
        PawnAI::BuildGuardianSitRep(sit);
        PawnAI::GuardianReport rep;
        doctrine.Decide(sit, rep);

        float incl[I_COUNT]; ReadAllIncl(incl, 0);
        const char* owner =
            (incl[I_GUARDIAN] > incl[I_NEXUS] + 1.0f) ? "Guardian (anchor = Arisen)" :
            (incl[I_NEXUS] > incl[I_GUARDIAN] + 1.0f) ? "Nexus (anchor = selected pawn)" :
                                                        "Tie (primary inclination decides)";
        ImGui::Text("Guardian %.0f / Nexus %.0f — %s", incl[I_GUARDIAN], incl[I_NEXUS], owner);
        ImGui::TextDisabled("Observe-only: decides, writes NOTHING to game memory.");

        ImGui::Separator();
        // Build 57.1: динамический Guardian-фикс (code 54, -3 -> 0).
        if (ImGui::Checkbox("Guardian fix (code 54, -3->0)", &PawnAI::g_guardianFixEnabled)) {
            config.setBool("pawnAI", "guardianFix", PawnAI::g_guardianFixEnabled);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Transactional: lifts Guardian -3 penalty on WpnDaggerAtk ONLY while a threat is in your zone and pawn is melee/hybrid. Rollback when zone clears.");
        ImGui::TextColored(DevTools::GuardianFixIsApplied() ? ImVec4(0.3f,1,0.3f,1) : ImVec4(0.7f,0.7f,0.7f,1),
            "%s", DevTools::GuardianFixStatus());

        ImGui::Separator();
        // Build 57: разведка Guardian-штрафов (read-only). Кнопка + статус.
        // Результат кэшируем в static — аудит нельзя гонять каждый кадр (спам лога).
        static char auditCache[640] = "Guardian audit: press the button (needs bodies resolved).";
        if (ImGui::Button("Audit Guardian penalties (code 54)")) {
            const char* r = DevTools::GuardianPenaltyAudit();
            if (r) lstrcpynA(auditCache, r, sizeof(auditCache));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("read-only, logs full tuple");
        ImGui::TextWrapped("%s", auditCache);

        ImGui::Separator();
        ImGui::Text("Response mode: %s | combat %s",
            rep.responseMode, sit.inCombat ? "YES" : "no");

        if(!rep.anchorResolved){
            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
                "Arisen position NOT resolved yet (world not loaded / DevTools off).");
        } else {
            ImGui::Text("Arisen pos: (%.1f, %.1f, %.1f) raw", sit.anchorX, sit.anchorY, sit.anchorZ);
            if(rep.pawnResolved)
                ImGui::Text("Pawn   pos: (%.1f, %.1f, %.1f) raw", sit.pawnX, sit.pawnY, sit.pawnZ);
            else
                ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Pawn pos: (pending resolution...)");
            char nearestBuf[32];
            if (rep.threatsInZone > 0 && rep.nearestThreatDist < 1e8f)
                sprintf_s(nearestBuf, sizeof(nearestBuf), "%.1f m", rep.nearestThreatDist);
            else
                lstrcpynA(nearestBuf, "---", sizeof(nearestBuf));
            ImGui::Text("Threats in zone: %d | nearest %s | pawn->Arisen %.1f m | engaged %s",
                rep.threatsInZone, nearestBuf, rep.pawnAnchorDist,
                rep.zoneEngaged ? "YES" : "no");
        }
        ImGui::TextDisabled("World units are NOT meters (~cm, scale %.0f/m). Distances shown in meters.",
            doctrine.worldUnitsPerMeter);

        if(rep.adviceCount){
            for(int i = 0; i < rep.adviceCount; i++){
                const PawnAI::GuardianAdvice& a = rep.advice[i];
                ImGui::TextColored(ImVec4(0.5f, 0.6f, 1, 1), "[advice] %s", a.reason);
                ImGui::TextDisabled("   code=%u intent=%s deltaS32=%d",
                    a.code, a.intentKey ? a.intentKey : "(none)", a.deltaS32);
            }
        } else {
            ImGui::TextDisabled("No advice (vanilla) — no threat in zone, or zone unresolved.");
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
    ImGui::TextDisabled("Modular Orchestration: Stride=%d mStudy@0x%X | Modules: Acquisitor, SmartUtil, Anchors, Tactical",
        INCL_STRIDE, MSTUDYFLAG_OFFSET);
    ImGui::PopID();
}

void Hooks::PawnAI(){
    g_enabled = config.getBool("pawnAI", "enabled", true);
    g_orch.presets.enabled   = config.getBool("pawnAI", "presetsEnabled", true);
    g_orch.acquisitor.enabled = config.getBool("pawnAI", "acquisitor", true);
    g_orch.smartUtil.enabled = config.getBool("pawnAI", "smartUtil", true);
    g_orch.presets.smooth    = config.getFloat("pawnAI", "smooth", 0.10f);
    g_orch.tactical.enabled  = config.getBool("pawnAI", "tactical", true);
    // Build 57.1: динамический Guardian-фикс (vanilla по умолчанию).
    PawnAI::g_guardianFixEnabled = config.getBool("pawnAI", "guardianFix", false);
    PawnAI::g_guardianMeleeRadius = config.getFloat("pawnAI", "guardianMeleeRadius", 6.0f);
    // Build 58: градиентные зоны телохранителя.
    PawnAI::g_guardianPreemptRadius = config.getFloat("pawnAI", "guardianPreemptRadius", 10.0f);
    PawnAI::g_guardianDaggerBiasMelee = config.getInt("pawnAI", "guardianDaggerBiasMelee", 2);
    PawnAI::g_guardianDaggerBiasPreempt = config.getInt("pawnAI", "guardianDaggerBiasPreempt", 0);
    g_orch.acquisitor.suppressFloor = config.getFloat("pawnAI", "acquisitorCombatFloor", 100.0f);
    g_orch.acquisitor.boostAmount   = config.getFloat("pawnAI", "acquisitorLootBoost", 180.0f);
    g_orch.acquisitor.boostWindowMs = (DWORD)config.getInt("pawnAI", "acquisitorBoostWindowMs", 8000);
    g_orch.acquisitor.returnMs      = (DWORD)config.getInt("pawnAI", "acquisitorReturnMs", 4000);
    g_orch.Init();
    int known = CountKnownEnemies();
    logFile << "PawnAI v2.9 Modular initialized — Acquisitor / SmartUtil / Custom Anchors / Tactical via CombatBus (ticker 150ms)" << std::endl;
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
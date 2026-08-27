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
#include "runtime/Runtime.h"
#include "EntityConfig.h"
#include "EnemyTuner.h"
#include "PawnAI.h"
#include "runtime/MonsterTempo.h"
#include "CombatIntel.h"
#include "CombatBus.h"
#include "pawnai/PawnAI_Common.h"
#include "pawnai/PawnAI_BusOrchestrator.h"
#include "pawnai/GuardianDoctrine.h"
#include "monsterai/MonsterDirector.h"
#include "monsterai/PackObserve.h"
#include "runtime/AggroWatch.h"
#include "runtime/PartyStatus.h"
#include "pawnai/PawnHaste.h"
#include "pawnai/DashWatch.h"
#include "pawnai/WandRange.h"
#include "pawnai/Possession.h"

using namespace PawnAI;

// Global Orchestrator
static Orchestrator g_orch;
static bool  g_enabled = true;

// Background tactical thread (150ms / ~6.7 Hz)
// Самопроверка записи склонностей определена ниже, рядом с панелью, а
// зовётся отсюда. Объявление обязано быть выше вызова: в прошлый раз тот
// же промах дал C2065 на статике, теперь C3861 на функции.
void HiredInclSelfTestTick();

void UpdatePawnAI(){
    // DevTools owns rollback-safe diagnostics. Let it observe world unload
    // before the gameplay guards return, even when Pawn AI itself is disabled.
    __try { Runtime::WorldScan_Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    // P0-1: Director/Pack sidecar сбрасывается на переходе даже если
    // pawn AI выключен и Tick режиссёра не дойдёт до гейта gameplay.
    {
        static bool s_wasGameplay = false;
        const bool gp = IsInActiveGameplay();
        if (s_wasGameplay && !gp) {
            __try { MonsterAI::OnWorldUnload(); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        s_wasGameplay = gp;
    }
    // Read-only night instrument. Must run even if pawn AI / Director are off.
    __try { MonsterAI::PackObserveTick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    // 84.16/84.18: универсальный card recon (GOBCARD/CARDRECON).
    // Read-only; работает при выключенном Director.
    __try { Runtime::Aggro::CardReconTick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    // 84.16 dual-observe: статусы партии + downed/revive FSM (PS: строки).
    // Read-only; нужен Director-снапшоту (downedValid/downedRevivable).
    __try { Runtime::PartyStatus::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    // Possession WATCH/unload-clear even if Pawn AI master is off.
    __try { PawnAI::Possession::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    if(!g_enabled || !pBase || !*pBase) {
        PawnAI::WandRange::Restore("pawn AI off");
        return;
    }
    if(!IsInActiveGameplay()) {
        PawnAI::WandRange::Restore("not in gameplay");
        return;
    }

    // Каждый модуль вызываем в собственном SEH — никакого каскадного падения
    __try { CombatIntel_Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { /* следующий тик догонит */ }

    // Самопроверка записи склонностей: живёт в тике, а не в отрисовке
    // панели, иначе закрытие окна оборвало бы опыт на середине.
    __try { HiredInclSelfTestTick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try { EntityCfg::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try { EnemyTuner::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Режиссёр стороны монстров. Симметричен оркестратору пешек: читает ту
    // же шину, управляет своими примитивами. Свой SEH — чтобы его ошибка
    // не уронила соседей.
    __try { MonsterAI::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Прибор «на кого смотрит пачка» (docs/AGGRO_RECON.md, этап 1).
    // Только читает; по умолчанию выключен и стоит ноль.
    __try { Runtime::Aggro::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Рывок пешки: латаем дыру с отсутствующим спринтом множителем
    // передвижения (docs/PAWN_SPRINT_RECON.md).
    __try { PawnAI::Haste::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Наблюдатель за рывками: приёмочный тест для будущей правки GOAP.
    __try { PawnAI::DashWatch::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try { PawnAI::WandRange::Tick(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Build 57.1: динамический Guardian-фикс (включён только при guardianFix=on).
    __try { Runtime::ErrataTick(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
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

// ============================================================================
// ВРЕМЕННЫЙ ЭКСПЕРИМЕНТ: склонности НАЁМНЫХ пешек (75.2)
// ============================================================================
//
// ЗАЧЕМ. Доктрина Guardian отлажена на одной вокации, а проверить её на
// Файтере/Маге можно только имея такую пешку. Наёмные дают это сразу — но
// их склонности игра менять не даёт: они приезжают из Рифта снимком.
//
// Тестер просит открыть их НА ВРЕМЯ, чтобы вживую посмотреть, как доктрина
// отрабатывает на чужой вокации. Оценка будет субъективной — это принято и
// записано: строгих чисел здесь не получится, инструмент даёт возможность
// посмотреть, а не доказать.
//
// ПОЧЕМУ ЭТО НЕ НАРУШЕНИЕ НАШЕЙ ЖЕ ГРАНИЦЫ (HIRED_PAWNS_SCOPE.md §1):
// граница запрещает менять чужой билд НАСОВСЕМ. Здесь три предохранителя:
//   1. значения снимаются ДО первой записи и хранятся как база;
//   2. откат по кнопке, по снятию галки и АВТОМАТИЧЕСКИ при выгрузке DLL;
//   3. на диск не пишет ничего и никогда — сохранение игры мы не трогаем.
// То есть чужая пешка возвращается в Рифт ровно такой, какой пришла,
// если только игрок сам не сохранится с изменёнными значениями. Об этом
// в панели написано прямым текстом.
//
// Запись идёт в запись персонажа (`pBase + 0xA7000 + 0x7F0 + idx*0x1660`),
// то есть ровно туда же, куда доктрина пишет своей пешке.
static bool  g_hiredInclUnlocked = false;
static bool  g_hiredInclShow = false;
static bool  g_hiCaptured[4] = {};
static float g_hiBase[4][I_COUNT];
static bool  g_hiChanged[4] = {};

// Проверка, что по индексу действительно живая запись пешки, а не мусор.
// Без неё «наёмная пешка №3» при партии из двух окажется случайными
// байтами, а мы в них запишем float.
static bool HiredRecordOk(int idx, int* vocOut, int* lvlOut)
{
    if (vocOut) *vocOut = 0;
    if (lvlOut) *lvlOut = 0;
    if (!pBase || !*pBase || idx < 0 || idx > 3) return false;
    const uintptr_t rec = (uintptr_t)(*pBase) + PLAYER_BASE + PAWN_OFFSET
                        + (uintptr_t)idx * PAWN_STRIDE;
    int voc = 0; unsigned short lvl = 0; float maxHp = 0.0f;
    __try {
        voc   = *(int32_t*)(rec + VOCATION_OFFSET);
        lvl   = *(unsigned short*)(rec + 0xDD0);
        maxHp = *(float*)(rec + 0x970);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (voc < 1 || voc > 9) return false;
    if (lvl == 0 || lvl > 250) return false;
    if (!(maxHp > 0.0f) || maxHp > 200000.0f) return false;
    if (vocOut) *vocOut = voc;
    if (lvlOut) *lvlOut = (int)lvl;
    return true;
}

// idx здесь — fixed pawn record (0 main, 1/2 hired), а не ordinal в
// компактном PawnBodyAt(). При временно unresolved предыдущем record нельзя
// дать следующему телу «съехать» в этот слот: live write обязан fail closed.
static uintptr_t ExactPawnBodyForRecord(int idx)
{
    uintptr_t body = 0;
    if (idx < 0 || idx > 2
        || !Runtime::PartyRecordInfo(idx, 0, 0, &body)) return 0;
    return body;
}

static void HiredInclCapture(int idx)
{
    if (idx < 0 || idx > 3 || g_hiCaptured[idx]) return;
    ReadAllIncl(g_hiBase[idx], idx);
    g_hiCaptured[idx] = true;
    char l[220];
    sprintf_s(l, "HiredIncl: baseline captured for pawn #%d: %.0f %.0f %.0f %.0f"
                 " %.0f %.0f %.0f %.0f %.0f", idx,
              g_hiBase[idx][0], g_hiBase[idx][1], g_hiBase[idx][2],
              g_hiBase[idx][3], g_hiBase[idx][4], g_hiBase[idx][5],
              g_hiBase[idx][6], g_hiBase[idx][7], g_hiBase[idx][8]);
    logFile << l << std::endl;
}

static void HiredInclRestore(int idx)
{
    if (idx < 0 || idx > 3 || !g_hiCaptured[idx] || !g_hiChanged[idx]) return;
    WriteAllIncl(g_hiBase[idx], idx);
    g_hiChanged[idx] = false;
    logFile << "HiredIncl: pawn #" << idx << " restored to the captured baseline"
            << std::endl;
}

void HiredInclRestoreAll()
{
    for (int i = 0; i < 4; ++i) HiredInclRestore(i);
}


// ============================================================================
// САМОПРОВЕРКА ЗАПИСИ СКЛОННОСТЕЙ (75.19)
// ============================================================================
//
// ПОВОД — процессный, а не технический. Тестер выставил склонности
// наёмной пешке, отвоевал бой, и только потом случайно заглянул в профиль
// и увидел, что ничего не изменилось. Замер пропал, а вопрос «работает ли
// запись» так и остался открытым.
//
// Это моя ошибка в постановке работы: проверять чужую реализацию глазами
// в игре — не работа тестера. Мод обязан проверять себя сам и говорить
// одной строкой, что получилось.
//
// ЧТО ДЕЛАЕТ САМОПРОВЕРКА (одна кнопка, пять секунд, полный откат):
//   1. запоминает исходные значения обоих источников;
//   2. пишет пробное значение в оба (запись персонажа + живое тело);
//   3. три секунды следит, ДЕРЖАТСЯ ли числа или движок их возвращает;
//   4. печатает вердикт по каждому источнику отдельно;
//   5. восстанавливает исходное.
//
// Вердикт «держится» не означает «игра это читает» — он означает, что
// память наша и её никто не перетирает. Читает ли её ИИ, показывает
// только поведение; но без этого шага и спрашивать нечего.
struct HiredSelfTest {
    bool     active;
    int      idx;
    int      phase;          // 0 — не запущен, 1 — наблюдение, 2 — откат
    DWORD    t0;
    int      samples;
    int      recHeld, recReverted;
    int      bodyHeld, bodyReverted, bodyUnavailable;
    float    probe;
    int      inclId;
    float    recBase[I_COUNT];
    float    bodyBase[9];
    bool     bodyBaseOk;
};
static HiredSelfTest g_hst = {};

void HiredInclSelfTestStart(int idx)
{
    if (g_hst.active || idx < 1 || idx > 3) return;
    memset(&g_hst, 0, sizeof(g_hst));
    g_hst.idx = idx;
    g_hst.inclId = I_SCATHER;     // безопасная склонность для пробы
    g_hst.probe = 999.0f;

    ReadAllIncl(g_hst.recBase, idx);
    const uintptr_t body = ExactPawnBodyForRecord(idx);
    g_hst.bodyBaseOk = body && Runtime::PawnInclinationsLive(body, g_hst.bodyBase);

    float v[I_COUNT];
    memcpy(v, g_hst.recBase, sizeof(v));
    v[g_hst.inclId] = g_hst.probe;
    WriteAllIncl(v, idx);
    if (body) Runtime::PawnSetInclinationLive(body, g_hst.inclId, g_hst.probe);

    g_hst.active = true;
    g_hst.phase = 1;
    g_hst.t0 = MsNow();
    logFile << "HiredIncl selftest: started on hired #" << idx
            << ", writing Scather = 999 to both sources" << std::endl;
}

void HiredInclSelfTestTick()
{
    if (!g_hst.active) return;
    const DWORD now = MsNow();

    if (g_hst.phase == 1) {
        float rec[I_COUNT];
        ReadAllIncl(rec, g_hst.idx);
        if (rec[g_hst.inclId] == g_hst.probe) ++g_hst.recHeld; else ++g_hst.recReverted;

        const uintptr_t body = ExactPawnBodyForRecord(g_hst.idx);
        float live[9] = {};
        if (body && Runtime::PawnInclinationsLive(body, live)) {
            if (live[g_hst.inclId] == g_hst.probe) ++g_hst.bodyHeld;
            else ++g_hst.bodyReverted;
        } else {
            ++g_hst.bodyUnavailable;
        }
        ++g_hst.samples;

        if (now - g_hst.t0 < 3000) return;
        g_hst.phase = 2;
    }

    // Откат и вердикт.
    WriteAllIncl(g_hst.recBase, g_hst.idx);
    const uintptr_t body = ExactPawnBodyForRecord(g_hst.idx);
    if (body && g_hst.bodyBaseOk)
        Runtime::PawnSetInclinationLive(body, g_hst.inclId, g_hst.bodyBase[g_hst.inclId]);

    char l[300];
    sprintf_s(l, "HiredIncl selftest: hired #%d, %d samples over 3 s", g_hst.idx, g_hst.samples);
    logFile << l << std::endl;
    sprintf_s(l, "   RECORD: held %d, reverted %d -> %s",
              g_hst.recHeld, g_hst.recReverted,
              g_hst.recReverted ? "the engine REWRITES this place - our value does not live here"
                                : "our value STAYS (nobody overwrites it)");
    logFile << l << std::endl;
    if (g_hst.bodyUnavailable && !g_hst.bodyHeld && !g_hst.bodyReverted)
        logFile << "   BODY  : cCmcInfo not readable on this pawn" << std::endl;
    else {
        sprintf_s(l, "   BODY  : held %d, reverted %d -> %s",
                  g_hst.bodyHeld, g_hst.bodyReverted,
                  g_hst.bodyReverted ? "the engine REWRITES the live mirror"
                                     : "our value STAYS in the live body");
        logFile << l << std::endl;
    }
    logFile << "   NOTE: 'stays' means nobody overwrites our bytes. Whether the AI"
               " READS them is a behaviour question, not a memory one." << std::endl;
    logFile << "   restored to the captured baseline" << std::endl;

    g_hst.active = false;
    g_hst.phase = 0;
}

void RenderPawnAIUI(){
    if(!ImGui::CollapsingHeader("Pawn AI Overhaul v2.8 Modular")) return;
    ImGui::PushID("PawnAI");

    if(ImGui::Checkbox("Enable Pawn AI Master", &g_enabled)) config.setBool("pawnAI", "enabled", g_enabled);
    ImGui::Separator();

    {
        PawnAI::Possession::Status ps = PawnAI::Possession::Get();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1), "Debilitation & Status Lab");
        bool arm = ps.armed;
        if (ImGui::Checkbox("arm writes##poss", &arm)) {
            PawnAI::Possession::SetArmed(arm);
            config.setBool("possession", "enabled", arm);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable status apply/clear writes. Unload clears.");

        ImGui::SameLine();
        bool ar = ps.targetArisen;
        if (ImGui::RadioButton("Main Pawn##tgt", !ar)) {
            PawnAI::Possession::SetTargetArisen(false);
            config.setBool("possession", "targetArisen", false);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Arisen##tgt", ar)) {
            PawnAI::Possession::SetTargetArisen(true);
            config.setBool("possession", "targetArisen", true);
        }

        static const char* kStatusItems =
            "7: Possession\0"
            "0: Poison\0"
            "1: Torpor (Slow)\0"
            "2: Blindness\0"
            "3: Silence\0"
            "4: Sleep\0"
            "6: Drenched\0"
            "8: Tarred\0"
            "9: Curse\0"
            "10: Caught Fire\0"
            "11: Frozen\0"
            "12: Petrifaction\0"
            "33: Stamina Boost\0\0";
        static const int kStatusIds[] = { 7, 0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 33 };
        static const int kStatusCount = 13;

        int cur = 0;
        for (int i = 0; i < kStatusCount; ++i) {
            if (kStatusIds[i] == ps.selectedId) { cur = i; break; }
        }
        ImGui::PushItemWidth(170);
        if (ImGui::Combo("status##sel", &cur, kStatusItems)) {
            PawnAI::Possession::SetSelectedId(kStatusIds[cur]);
            config.setInt("possession", "statusId", kStatusIds[cur]);
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::SmallButton("apply##poss")) PawnAI::Possession::RequestApply();
        ImGui::SameLine();
        if (ImGui::SmallButton("clear##poss")) PawnAI::Possession::RequestClear();

        ImGui::TextDisabled("  target=%s | %s | work %d | id=%d t=%.0f cnt=%d | hook %s | layout %s",
                            ps.targetArisen ? "Arisen" : "MainPawn",
                            ps.why, ps.slot, ps.liveId, ps.liveTimer, ps.liveCount,
                            ps.hookArmed ? "armed" : "missing",
                            ps.layout ? "ready" : "no");
        bool cust = ps.customOn;
        if (ImGui::Checkbox("custom params##poss", &cust)) {
            PawnAI::Possession::SetCustom(cust);
            config.setBool("possession", "customParams", cust);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("xmm on OUR apply only. Off = catalog default values.");
        if (cust) {
            float ct = ps.customT, cp0 = ps.customP0, cp1 = ps.customP1;
            ImGui::PushItemWidth(160);
            if (ImGui::SliderFloat("timer s##poss", &ct, 5.0f, 180.0f, "%.0f")) {
                PawnAI::Possession::SetCustomTimer(ct);
                config.setFloat("possession", "timer", ct);
            }
            if (ImGui::SliderFloat("param0##poss", &cp0, 0.05f, 20.00f, "%.2f")) {
                PawnAI::Possession::SetCustomP0(cp0);
                config.setFloat("possession", "param0", cp0);
            }
            if (ImGui::SliderFloat("param1##poss", &cp1, 0.00f, 2.00f, "%.2f")) {
                PawnAI::Possession::SetCustomP1(cp1);
                config.setFloat("possession", "param1", cp1);
            }
            ImGui::PopItemWidth();
        }
        ImGui::Separator();
    }

    // 1. Acquisitor Manager (бывший Sanitary Cordon)
    ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Acquisitor Manager");
    if(ImGui::Checkbox("Enable##acq", &g_orch.acquisitor.enabled)) config.setBool("pawnAI", "acquisitor", g_orch.acquisitor.enabled);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acquisitor only: suppress in combat, loot boost out of it.");

    // 1b. Рывок в бою: у пешек нет даша на боевых целях вообще
    {
        PawnAI::Haste::Status hs = PawnAI::Haste::Get();
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Combat haste (no sprint in GOAP)");
        bool on = hs.enabled;
        if (ImGui::Checkbox("Enable##haste", &on)) {
            PawnAI::Haste::SetEnabled(on);
            config.setBool("pawnHaste", "enabled", on);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Combat close-in speed. Dash itself is still Follow-only.");
        if (on) {
            float f = hs.factor;
            if (ImGui::SliderFloat("haste factor", &f, 1.00f, 1.30f, "%.2f")) {
                PawnAI::Haste::SetFactor(f);
                config.setFloat("pawnHaste", "factor", f);
            }
            ImGui::TextColored(hs.active ? ImVec4(1, 0.8f, 0.3f, 1)
                                         : ImVec4(0.6f, 0.6f, 0.6f, 1),
                "%s | nearest enemy %.1f m | bursts %d | applied x%.2f",
                hs.why, hs.distM, hs.applied, hs.used);
            ImGui::TextDisabled("  pawns tracked %d, compensating now %d "
                                "(hired pawns included)",
                                hs.pawnsTracked, hs.pawnsActive);
            ImGui::TextDisabled("  factor source: %s",
                hs.matchTempo ? "matched to the fastest monster nearby"
                              : "fixed number above");
            // Замеры 74.8 и 75.13 подряд шли с множителем 1.30 — это
            // потолок, то есть фиксированное число из ini. Ключ
            // matchMonsterTempo существовал только в файле, а файл у
            // тестера старше сборки. Ручка должна быть там, где смотрят.
            bool match = hs.matchTempo;
            if (ImGui::Checkbox("match monster tempo (compensation, not buff)", &match)) {
                PawnAI::Haste::SetMatchTempo(match);
                config.setBool("pawnHaste", "matchMonsterTempo", match);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ON: match fastest nearby monster. OFF: use the slider.");
            bool needWpn = hs.requireWeapon;
            if (ImGui::Checkbox("require the pawn's weapon drawn", &needWpn)) {
                PawnAI::Haste::SetRequireWeapon(needWpn);
                config.setBool("pawnHaste", "requireWeaponDrawn", needWpn);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Weapon drawn OR combat detector. Stops wall-camp false hits.");
            ImGui::TextDisabled("  last burst act: %s  |  confirmed by weapon %d / "
                                "by detector %d",
                                hs.act[0] ? hs.act : "-",
                                hs.burstsWeapon, hs.burstsDetector);
            {
                const Runtime::Tempo::Status ts = Runtime::Tempo::GetStatus();
                ImGui::TextDisabled("  anim writes %u | engine rewrites %u | "
                                    "track restores %u",
                                    ts.animOurWrites, ts.animEngineWrites,
                                    ts.animRestores);
            }
            // Замер 74.5 прошёл со связкой в положении «выкл», и это
            // выяснилось только из первой строки лога. Теперь состояние
            // видно там, где на него смотрят.
            if (!hs.animCouple)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1),
                    "  coupling OFF - sliding feet");

            // Связка анимации. Пока вердикт пробы не прочитан в логе,
            // флажок держать выключенным: он разрешает ЗАПИСЬ в тело
            // пешки по смещению, подтверждённому только на монстрах.
            bool couple = hs.animCouple;
            if (ImGui::Checkbox("couple run animation", &couple)) {
                PawnAI::Haste::SetAnimCouple(couple);
                config.setBool("pawnHaste", "animCouple", couple);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Scale run animation with speed. Off = sliding feet.");
            
            // Первое поле ряда живое: у стоящей пешки 1.000, у бегущей
            // 1.060. Какое поле за что отвечает — вопрос к наблюдению.
            const bool rw = Runtime::Tempo::AnimRowWatchActive();
            if (rw) ImGui::TextDisabled("  watching the row...");
            else if (ImGui::SmallButton("watch anim row 15 s")) {
                const uintptr_t pb = Runtime::MainPawnBody();
                if (pb) Runtime::Tempo::AnimRowWatchStart(pb, 15000);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("15s log of the five rate fields. Read-only.");
        }



        // Прибор для трека «честный спринт»: считает переходы пешки в
        // состояния cPlActDash*. До правки GOAP в бою обязан быть ноль.
        PawnAI::DashWatch::Stats ds = PawnAI::DashWatch::Get();
        ImGui::TextColored(ds.inCombat ? ImVec4(0.4f, 1.0f, 0.6f, 1)
                                       : ImVec4(1.0f, 0.7f, 0.4f, 1),
            "dash states: in combat %u | out of combat %u",
            ds.inCombat, ds.outOfCombat);
        ImGui::SameLine();
        if (ImGui::SmallButton("reset##dash")) PawnAI::DashWatch::Reset();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dash-state entries on the main pawn.");
        ImGui::TextDisabled("  now: %s | last dash at %.1f m",
                            ds.lastAct[0] ? ds.lastAct : "?", ds.lastDistM);
        // Сшивка с планировщиком. Замер 73.27 дал ноль выборов кода 84/85
        // и при этом живые рывки: значит рывок приходит НЕ через код
        // рывка. Эта строка отвечает, через какой именно код он приходит.
        ImGui::TextDisabled("  dash under code: DashFollow 84/85 %u | Follow 1 %u | other %u | unknown %u",
                            ds.dashUnderDash, ds.dashUnderFollow,
                            ds.dashUnderOther, ds.dashCodeUnknown);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Priority code at the dash moment.");
        ImGui::TextDisabled("  last dash under code %d \"%s\"",
                            ds.lastCode, ds.lastGoal[0] ? ds.lastGoal : "?");
        {
            // Сигналы боя рядом со счётчиком: если метка снова соврёт,
            // будет видно, какой именно сигнал виноват.
            const ::CombatReport rr = CombatBus::Instance().LastReport();
            const ::WorldReport  ww = CombatBus::Instance().LastWorld();
            ImGui::TextDisabled("  combat signals: detector %d | enemy acts %d | pawn target %d",
                                rr.inCombat ? 1 : 0, ww.enemyCombatCount,
                                ww.pawnEngaged ? 1 : 0);
        }
        ImGui::Separator();
    }

    // 1c. Вокационный кордон Guardian
    {
        ImGui::TextColored(ImVec4(0.5f, 0.6f, 1, 1), "Guardian vocation cordon");
        if (ImGui::Checkbox("Enable##cordon", &g_orch.cordon.enabled))
            config.setBool("pawnAI", "vocationCordon", g_orch.cordon.enabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Cap Guardian on ranged/caster so they do not glue to you.");
        if (g_orch.cordon.enabled) {
            ImGui::Text("pawn: %s | %s", g_orch.cordon.lastClassName, g_orch.cordon.lastAction);
            if (g_orch.cordon.lastGuardianCap > 0.0f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "| cap %.0f",
                                   g_orch.cordon.lastGuardianCap);
            }
            bool ch = false;
            if (ImGui::SliderFloat("ranged: guardian cap", &g_orch.cordon.guardianCapRanged, 0.0f, 1000.0f, "%.0f")) ch = true;
            if (ImGui::SliderFloat("ranged: pioneer floor", &g_orch.cordon.pioneerFloorRanged, 0.0f, 1000.0f, "%.0f")) ch = true;
            if (ImGui::SliderFloat("hybrid: guardian cap", &g_orch.cordon.guardianCapHybrid, 0.0f, 1000.0f, "%.0f")) ch = true;
            if (ImGui::SliderFloat("hybrid: pioneer floor", &g_orch.cordon.pioneerFloorHybrid, 0.0f, 1000.0f, "%.0f")) ch = true;
            if (ch) {
                config.setFloat("pawnAI", "cordonGuardianCapRanged", g_orch.cordon.guardianCapRanged);
                config.setFloat("pawnAI", "cordonPioneerFloorRanged", g_orch.cordon.pioneerFloorRanged);
                config.setFloat("pawnAI", "cordonGuardianCapHybrid", g_orch.cordon.guardianCapHybrid);
                config.setFloat("pawnAI", "cordonPioneerFloorHybrid", g_orch.cordon.pioneerFloorHybrid);
            }
            ImGui::TextDisabled("Pioneer = stop hugging. Flanking is a later Follow fix.");
        }
        ImGui::Separator();
    }

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
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Combat adds a small delta on top of the sliders.");

    // ПРЕСЕТЫ — ВЫПАДАЮЩИМ СПИСКОМ (75.39).
    //
    // Шесть кнопок в ряд занимали всю ширину панели и читались как шесть
    // разных действий, хотя это одно действие с шестью значениями. Список
    // занимает одну строку и заодно ЧЕСТНО показывает седьмое состояние —
    // «Custom», то есть ползунки, сдвинутые рукой.
    //
    // Отдельного «кастомного пресета» заводить не нужно и не надо: ползунки
    // И ЕСТЬ якорь, они уже сохраняются в ini при каждом движении. «Custom»
    // в списке — это не ещё одно хранилище, а имя текущего состояния якоря.
    {
        const int kCustom = PawnAI::PresetManager::COUNT;
        static const char* names[PawnAI::PresetManager::COUNT + 1] = {};
        for (int p = 0; p < PawnAI::PresetManager::COUNT; ++p)
            names[p] = PawnAI::PresetManager::presets[p].name;
        names[kCustom] = "Custom (your sliders)";

        int sel = g_orch.presets.IsModified() ? kCustom
                                              : g_orch.presets.lastPresetIdx;
        ImGui::PushItemWidth(220);
        if (ImGui::Combo("preset", &sel, names, kCustom + 1)) {
            if (sel < kCustom) {
                g_orch.presets.LoadPreset(sel);
                config.setInt("pawnAI", "lastPreset", sel);
                // Пишем в память немедленно — прогресс-бары обновятся сразу.
                float f[I_COUNT]; g_orch.presets.GetBaseTarget(f);
                WriteAllIncl(f, 0);
            }
            // Выбор "Custom" сам по себе ничего не меняет: он и так означает
            // «то, что сейчас на ползунках».
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Loads slider values. Custom = you moved them by hand.");
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

        ImGui::TextDisabled("Slider = target. Bar = live.");

        // КТО ТЯНЕТ СКЛОННОСТЬ ВНИЗ — ГОВОРИТЬ ВСЛУХ.
        //
        // Жалоба тестера 20.08: «какого хрена у меня инклинации моей пешки
        // сбрасываются, каждый раз крутить надо». Разбор показал, что тянет
        // их НАШ ЖЕ модуль: вокационный кордон ставит Guardian потолок, если
        // пешка не мили. Страйдер — гибрид, значит потолок применяется.
        //
        // Ползунок при этом честно стоит на 1000, а живое значение уезжает к
        // потолку, и снаружи это выглядит как «настройка не сохранилась».
        // Панель обязана называть виновника сама, а не оставлять это на
        // раскопки в логе.
        if (g_orch.cordon.enabled && g_orch.cordon.lastGuardianCap > 0.0f
            && g_orch.presets.anchor[I_GUARDIAN] > g_orch.cordon.lastGuardianCap) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "Cordon caps Guardian at %.0f (%s). Slider is %.0f.",
                g_orch.cordon.lastGuardianCap, g_orch.cordon.lastClassName,
                g_orch.presets.anchor[I_GUARDIAN]);
            if (ImGui::Checkbox("vocation cordon (cap Guardian on non-melee pawns)",
                                &g_orch.cordon.enabled))
                config.setBool("pawnAI", "vocationCordon", g_orch.cordon.enabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Off: Guardian slider is obeyed as written.");
            ImGui::Separator();
        }
        ImGui::TextDisabled("%s%s",
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
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Copy live values onto the sliders.");

        ImGui::SameLine();
        if(ImGui::Button("Apply Instantly")) {
            float f[I_COUNT]; ReadAllIncl(f, 0);
            g_orch.presets.ApplyInstant(f);
            WriteAllIncl(f, 0);
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Write sliders now, skip lerp.");

        ImGui::SameLine();
        if(ImGui::Button("Reset Balanced")) {
            g_orch.presets.ResetDefaultAnchor();
        }

        // [UI-BLOCK-PAWN-BEGIN]  <- метка для tools/syntax_check.sh, не удалять
        // --- НАЁМНЫЕ ПЕШКИ, ТУТ ЖЕ ----------------------------------------
        //
        // Раньше это была отдельная секция ниже по панели. Держать
        // склонности в двух разных местах — значит заставлять человека
        // помнить, где что; всё, что про склонности, живёт здесь.
        //
        // ПОДТВЕРЖДЕНО (75.20): запись в наёмных РАБОТАЕТ. Профиль пешки
        // просто кэшируется и обновляется при смене локации — проверено и
        // на нашем моде, и на `ddda-dinput8`. То есть заморожен был не
        // билд пешки, а его отображение.
        ImGui::Separator();
        ImGui::Checkbox("show hired pawns", &g_hiredInclShow);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Temp edit. Restored on unload. Profile card updates on zone change.");

        if (g_hiredInclShow) {
            ImGui::SameLine();
            ImGui::Checkbox("allow editing", &g_hiredInclUnlocked);
            if (!g_hiredInclUnlocked && g_hiChanged[1] + g_hiChanged[2] + g_hiChanged[3])
                HiredInclRestoreAll();

            for (int idx = 1; idx <= 3; ++idx) {
                int voc = 0, lvl = 0;
                if (!HiredRecordOk(idx, &voc, &lvl)) continue;
                ImGui::PushID(1000 + idx);
                HiredInclCapture(idx);

                // ЗАГОЛОВОК НЕ МЕНЯЕТСЯ.
                //
                // Дрожание интерфейса, из-за которого тестер промахивался
                // мимо ползунков, шло отсюда: в заголовок подставлялась
                // пометка «[edited]», а строка справа то появлялась, то
                // исчезала вместе с телом пешки (разбор партии
                // пересобирается раз в несколько секунд). Менялась ширина
                // — прыгала вся раскладка.
                //
                // Теперь заголовок постоянный, а всё изменчивое ушло в
                // поля фиксированной ширины.
                char hdr[64];
                sprintf_s(hdr, "hired #%d: %-14s lvl %2d", idx, VocationName(voc), lvl);
                if (!ImGui::CollapsingHeader(hdr)) { ImGui::PopID(); continue; }

                float rec[I_COUNT];
                ReadAllIncl(rec, idx);
                for (int i = 0; i < 9; ++i) {
                    ImGui::PushID(i);
                    ImGui::Text("%-12s", InclName(i));
                    ImGui::SameLine(115);

                    float fr = rec[i] / 1000.0f;
                    if (fr < 0.0f) fr = 0.0f;
                    if (fr > 1.0f) fr = 1.0f;
                    ImGui::ProgressBar(fr, ImVec2(150.0f, 0), "");
                    ImVec2 bmin = ImGui::GetItemRectMin();
                    ImVec2 bmax = ImGui::GetItemRectMax();

                    if (g_hiredInclUnlocked) {
                        ImGui::SetCursorScreenPos(bmin);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0,0,0,0));
                        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0,0,0,0));
                        ImGui::PushItemWidth(150.0f);
                        float v = rec[i];
                        if (ImGui::SliderFloat("##hi", &v, 0.0f, 1000.0f, "")) {
                            rec[i] = v;
                            WriteAllIncl(rec, idx);
                            const uintptr_t b = ExactPawnBodyForRecord(idx);
                            if (b) Runtime::PawnSetInclinationLive(b, i, v);
                            g_hiChanged[idx] = true;
                        }
                        ImGui::PopItemWidth();
                        ImGui::PopStyleColor(2);
                    }

                    // Ширина этой надписи постоянна при любых значениях —
                    // именно она раньше и дёргала раскладку.
                    ImGui::SetCursorScreenPos(ImVec2(bmax.x + 6.0f, bmin.y));
                    ImGui::Text("%4.0f", rec[i]);
                    ImGui::PopID();
                }

                if (g_hiredInclUnlocked) {
                    if (ImGui::SmallButton("restore")) HiredInclRestore(idx);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("self-test write (5 s)"))
                        HiredInclSelfTestStart(idx);
                }
                ImGui::PopID();
            }
        }
        // [UI-BLOCK-PAWN-END]

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
        ImGui::Text("Guardian %.0f / Nexus %.0f - %s", incl[I_GUARDIAN], incl[I_NEXUS], owner);
        ImGui::TextDisabled("Observe only.");

        ImGui::Separator();
        // --- ЭРРАТА (слой B): статичная починка сломанного правила ---------
        //
        // Это НЕ доктрина. Доктрина крутит веса по ситуации, как задумано
        // дизайном; эррата снимает запрет, которого в дизайне быть не
        // должно. Разделение и его смысл — в docs/ERRATA_ARCHITECTURE.md.
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                           "Errata - static fixes of broken vanilla rules");
        ImGui::TextDisabled("Static rule fixes, not doctrine.");

        // --- E01/E02: Guardian душит кинжалы --------------------------------
        if (ImGui::Checkbox("Guardian dagger ban - LIFT IT (code 54)",
                            &Runtime::g_errataDaggerOn)) {
            config.setBool("errata", "guardianDaggerBan", Runtime::g_errataDaggerOn);
            if (!Runtime::g_errataDaggerOn) Runtime::ErrataRestore(0);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Lift Guardian -3/-2 on daggers. Shared resource, whole party.");
        {
            int v = (int)Runtime::g_errataDaggerVal;
            ImGui::PushItemWidth(160);
            if (ImGui::SliderInt("dagger AddS32", &v, -3, 5)) {
                Runtime::g_errataDaggerVal = (int32_t)v;
                config.setInt("errata", "guardianDaggerValue", v);
            }
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("-3 vanilla ban, 0 lift, +5 Scather-like.");
        }
        ImGui::TextColored(Runtime::ErrataIsApplied(0) ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                                       : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "%s", Runtime::ErrataStatus(0));

        // --- E03/E04: Nexus душит магию -------------------------------------
        if (ImGui::Checkbox("Nexus magic ban - LIFT IT (code 55)",
                            &Runtime::g_errataMagicOn)) {
            config.setBool("errata", "nexusMagicBan", Runtime::g_errataMagicOn);
            if (!Runtime::g_errataMagicOn) Runtime::ErrataRestore(1);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Lift Nexus -3/-2 on wand attacks. Not combat-measured yet.");
        {
            int v = (int)Runtime::g_errataMagicVal;
            ImGui::PushItemWidth(160);
            if (ImGui::SliderInt("magic AddS32", &v, -3, 5)) {
                Runtime::g_errataMagicVal = (int32_t)v;
                config.setInt("errata", "nexusMagicValue", v);
            }
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("-3 vanilla, 0 lift.");
        }
        ImGui::TextColored(Runtime::ErrataIsApplied(1) ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                                       : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "%s", Runtime::ErrataStatus(1));

        {
            PawnAI::WandRange::Status ws = PawnAI::WandRange::Get();
            bool won = ws.enabled;
            if (ImGui::Checkbox("Caster AI range -> 15 m (pawns)", &won)) {
                PawnAI::WandRange::SetEnabled(won);
                config.setBool("errata", "wandRange", won);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Pawn staff eligibility like a bow. Not the player. IceWalk stays short.");
            ImGui::TextColored(ws.applied ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                          : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               "%s", ws.why);
        }

        // --- ВЫПОЛОТО В 75.40 -----------------------------------------------
        //
        // Здесь стояли ещё две ручки на то же самое правило:
        //
        //   `Guardian fix (code 54, -3->0)` — ситуативная правка, включалась
        //       только пока угроза в зоне телохранителя;
        //   `inclination lever` — понижение Guardian / подъём Scather, плюс
        //       два ползунка смещения.
        //
        // Обе проиграли эррате и обе были построены на выводах, которые
        // потом оказались ложными:
        //   - рычаг склонности вырос из «AddS32 не двигает строку» (75.28) —
        //     это была ошибка чтения приборов;
        //   - и он же опирался на «смена склонности пересчитывает раскладку»,
        //     что опровергнуто логом 16a: живая запись склонности раскладку
        //     НЕ пересчитывает.
        //
        // Три ручки на одно правило — это гарантированная война записей и
        // непонятный интерфейс. Победитель один: эррата выше. Код доктрины
        // пока остаётся (на нём висит зона телохранителя), но из панели
        // рычаги убраны, а ключи ini принудительно выключены при загрузке.

        ImGui::Separator();
        ImGui::Text("Role: %s | combat %s",
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
        ImGui::TextDisabled("Distances in meters (scale %.0f).", doctrine.worldUnitsPerMeter);

        if(rep.adviceCount){
            for(int i = 0; i < rep.adviceCount; i++){
                const PawnAI::GuardianAdvice& a = rep.advice[i];
                ImGui::TextColored(ImVec4(0.5f, 0.6f, 1, 1), "[advice] %s", a.reason);
                ImGui::TextDisabled("   code=%u intent=%s deltaS32=%d",
                    a.code, a.intentKey ? a.intentKey : "(none)", a.deltaS32);
            }
        } else {
            ImGui::TextDisabled("No advice (vanilla) - no threat in zone, or zone unresolved.");
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
    ImGui::TextDisabled("   goblins %d  cat %d", world.goblinCount, world.dominantCategory);
    // Build 62: сигналы трёхсигнального детектора (урон виден в Hits выше).
    ImGui::TextDisabled("   enemyInCombatAction=%d  pawnTarget=%s",
        world.enemyCombatCount, world.pawnEngaged ? "SET" : "none");
    ImGui::TextDisabled("stride %d  mStudy 0x%X", INCL_STRIDE, MSTUDYFLAG_OFFSET);
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
    // Две старые ручки на правило code 54 выполоты из панели (75.40) и
    // принудительно выключены: победила эррата, а три хозяина у одного
    // правила — это война записей.
    PawnAI::g_guardianFixEnabled  = false;
    PawnAI::g_guardianUseInclLever = false;
    Runtime::g_errataDaggerOn  = config.getBool("errata", "guardianDaggerBan", false);
    Runtime::g_errataDaggerVal = (int32_t)config.getInt("errata", "guardianDaggerValue", 0);
    Runtime::g_errataMagicOn   = config.getBool("errata", "nexusMagicBan", false);
    Runtime::g_errataMagicVal  = (int32_t)config.getInt("errata", "nexusMagicValue", 0);
    PawnAI::g_guardianScatherBoost = config.getFloat("pawnAI", "guardianScatherBoost", 800.0f);
    PawnAI::g_guardianLeverMode = (int)config.getFloat("pawnAI", "guardianLeverMode", 1.0f);
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
    PawnAI::Haste::Init();
    PawnAI::DashWatch::Init();
    PawnAI::WandRange::Init();
    PawnAI::Possession::Init();
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
    // Чужие пешки возвращаются в Рифт такими, какими пришли: откат
    // склонностей наёмных — часть выгрузки, а не забота пользователя.
    HiredInclRestoreAll();
    PawnAI::GuardianLeverRestore();   // склонность обязана вернуться при выгрузке
    PawnAI::Haste::Shutdown();        // снять множители с тел партии до выгрузки
    PawnAI::DashWatch::Shutdown();
    PawnAI::WandRange::Shutdown();
    PawnAI::Possession::Shutdown();
    g_orch.Shutdown();
}
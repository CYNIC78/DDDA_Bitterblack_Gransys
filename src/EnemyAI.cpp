/**
 * EnemyAI.cpp — ум врага (орган, не пак файлов)
 *
 * Полка: LIVE. CATALOG (снятые XML) кормит политику.
 * PACK/FSM — только если не хватает графа состояний, см. docs/ROADMAP.md фаза 3.
 *
 * Сейчас: заготовка UI. Слайдеры ничего не пишут, пока WorldScan
 * не найдёт живой cThinkMgr / cCharParamEnemy.
 */

#include "stdafx.h"
#include "monsterai/MonsterDirector.h"
#include "monsterai/PackObserve.h"
#include "runtime/AggroWatch.h"
#include "runtime/MonsterTempo.h"
#include "EnemyAI.h"

static bool enemyAIEnabled = true;

// Политика, которую ФАЗА 3 запишет в живой think. Не в .arc.
static float enemyAggression = 1.3f;
static float enemyReactionSpeed = 1.2f;
static int maxSimultaneousAttackers = 5;
static bool smarterTactics = true;

void RenderEnemyAIUI()
{
    if (!ImGui::CollapsingHeader("Enemy AI Overhaul"))
        return;

    ImGui::PushID("EnemyAI");

    ImGui::TextDisabled("Sliders are wishes until we hold a live think object.");

    if (ImGui::Checkbox("Enable Enemy AI Module", &enemyAIEnabled))
        config.setBool("enemyAI", "enabled", enemyAIEnabled);

    ImGui::Separator();

    // --- Темп передвижения: ломаем выученный ритм подхода ------------------
    {
        const Runtime::Tempo::Status ts = Runtime::Tempo::GetStatus();
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Movement tempo variation");
        bool movementOn = ts.enabled;
        if (ImGui::Checkbox("enable movement tempo##mt", &movementOn)) {
            Runtime::Tempo::SetEnabled(movementOn);
            config.setBool("monsterTempo", "enabled", movementOn);
        }
        if (!movementOn) {
            ImGui::TextDisabled("off (saved to INI); hook remains ready for live ON");
        } else {
            // Legacy-поле walkHooked на деле обозначает общий канал
            // локомоции: dash/track, run и walk. Sprint — отдельный путь,
            // а не критерий успеха общей локомоции.
            ImGui::TextColored(ts.walkHooked ? ImVec4(0.3f,1,0.3f,1) : ImVec4(1,0.5f,0.3f,1),
                "general locomotion hook (dash/run/walk): %s (%d sig matches)",
                ts.walkHooked ? "ON" : "not installed", ts.walkMatches);
            ImGui::TextColored(ts.sprintHooked ? ImVec4(0.3f,1,0.3f,1) : ImVec4(1,0.5f,0.3f,1),
                "separate sprint hook: %s (%d sig matches)",
                ts.sprintHooked ? "ON" : "not installed", ts.sprintMatches);
            if (!ts.walkHooked)
                ImGui::TextDisabled("General locomotion is vanilla: dash/run/walk proof unavailable.");
            else if (!ts.sprintHooked)
                ImGui::TextDisabled("Sprint path is vanilla; general locomotion remains active.");
            float lo = 0, hi = 0;
            Runtime::Tempo::GetRange(&lo, &hi);
            bool changed = false;
            if (ImGui::SliderFloat("speed min", &lo, 0.75f, 1.30f, "%.2f")) changed = true;
            if (ImGui::SliderFloat("speed max", &hi, 0.75f, 1.30f, "%.2f")) changed = true;
            if (changed) {
                Runtime::Tempo::SetRange(lo, hi);
                Runtime::Tempo::GetRange(&lo, &hi);
                config.setFloat("monsterTempo", "factorMin", lo);
                config.setFloat("monsterTempo", "factorMax", hi);
            }
            ImGui::Text("tracked %d monsters | set %.2f..%.2f -> live %.2f..%.2f",
                ts.tracked, lo, hi, ts.minFactor, ts.maxFactor);

            // Кто вообще спринтует. Через этот хук проходит любой
            // спринтующий, поэтому счётчик отвечает на вопрос «пешки
            // правда не спринтят?» замером, а не впечатлением.
            const Runtime::Tempo::SprintStats sp = Runtime::Tempo::GetSprintStats();
            ImGui::TextColored(sp.pawn ? ImVec4(0.8f, 0.8f, 0.8f, 1)
                                       : ImVec4(1.0f, 0.7f, 0.4f, 1),
                "sprint seen: player %u, pawns %u, enemies %u, other %u",
                sp.player, sp.pawn, sp.enemy, sp.other);
            ImGui::SameLine();
            if (ImGui::SmallButton("reset##sprint")) Runtime::Tempo::ResetSprintStats();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Bodies that hit the sprint hook this session.");
            if (ImGui::SmallButton("locomotion counters to log"))
                Runtime::Tempo::DumpLocomotionDiagnostics("manual");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Write bounded general/sprint application counters to the log.");
        }
    }

    // --- Темп анимации: вторая, независимая ручка ---------------------------
    //
    // Найдено дифом по торпору 19.08: ряд из пяти множителей скорости
    // воспроизведения в теле (+0x0EE4…+0x0EF4). Правка мультипликативная,
    // поэтому торпор и захват продолжают работать поверх неё.
    {
        const Runtime::Tempo::Status ts = Runtime::Tempo::GetStatus();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Attack tempo variation");
        bool on = ts.animEnabled;
        if (ImGui::Checkbox("enable attack tempo", &on)) {
            Runtime::Tempo::SetAnimEnabled(on);
            config.setBool("monsterTempo", "animEnabled", on);
        }
        if (!on) {
            ImGui::TextDisabled("off - the swing timing stays vanilla");
        } else {
            float lo = 0, hi = 0;
            Runtime::Tempo::GetAnimRange(&lo, &hi);
            bool changed = false;
            if (ImGui::SliderFloat("tempo min", &lo, 0.70f, 1.40f, "%.2f")) changed = true;
            if (ImGui::SliderFloat("tempo max", &hi, 0.70f, 1.40f, "%.2f")) changed = true;
            if (changed) {
                Runtime::Tempo::SetAnimRange(lo, hi);
                Runtime::Tempo::GetAnimRange(&lo, &hi);
                config.setFloat("monsterTempo", "animFactorMin", lo);
                config.setFloat("monsterTempo", "animFactorMax", hi);
            }
            ImGui::TextColored(ts.animAttacksOnly ? ImVec4(0.4f, 1.0f, 0.6f, 1)
                                                  : ImVec4(1.0f, 0.7f, 0.4f, 1),
                "scope: %s", ts.animAttacksOnly ? "ATTACKS ONLY"
                                                : "EVERYTHING (walking too)");
            bool only = ts.animAttacksOnly;
            if (ImGui::Checkbox("attacks only (leave walking vanilla)", &only)) {
                Runtime::Tempo::SetAnimAttacksOnly(only);
                config.setBool("monsterTempo", "animAttacksOnly", only);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Apply playback rate only during attacks.");
            float cpl = ts.animCoupling;
            if (ImGui::SliderFloat("coupling to speed", &cpl, 0.0f, 1.0f, "%.2f")) {
                Runtime::Tempo::SetAnimCoupling(cpl);
                config.setFloat("monsterTempo", "animCoupling", cpl);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("0 = independent knobs. 1 = fast runners also swing fast.");

            // Заданное рядом с фактическим: узкий фактический разброс при
            // широком заданном — это либо мало монстров, либо плохой хеш.
            // Один раз мы уже на этом попались.
            ImGui::Text("tracked %d monsters | set %.2f..%.2f -> live %.2f..%.2f",
                ts.animTracked, lo, hi, ts.animMin, ts.animMax);
            // Измерение вместо догадки: писал ли движок в эти поля сам.
            // Раскладка по особям. Живой тест дал вопрос «почему взбесился
            // только один?» — ответ виден здесь: множитель у каждого свой.
            const int nl = Runtime::Tempo::AnimListCount();
            for (int i = 0; i < nl && i < 8; ++i) {
                uintptr_t b = 0; float f = 0; char kind[16] = {};
                if (!Runtime::Tempo::AnimListAt(i, &b, &f, kind, sizeof(kind))) continue;
                // Обе ручки в одной строке: подбирать параметры, глядя на
                // два разных списка в разных местах панели, невозможно.
                float loco = 1.0f, atk = 1.0f;
                Runtime::Tempo::GetFactors(b, &loco, &atk);
                const bool hot = (atk > 1.10f), slow = (atk < 0.90f);
                ImGui::TextColored(hot  ? ImVec4(1.0f, 0.6f, 0.4f, 1)
                                 : slow ? ImVec4(0.5f, 0.8f, 1.0f, 1)
                                        : ImVec4(0.8f, 0.8f, 0.8f, 1),
                    "  0x%08X %-10s move x%.2f  swing x%.2f",
                    (unsigned)b, kind, loco, atk);
            }
            if (nl > 8) ImGui::TextDisabled("  ... and %d more", nl - 8);

            uint32_t cLast = 0, cAvg = 0, cMax = 0;
            Runtime::Tempo::AnimCost(&cLast, &cAvg, &cMax);
            ImGui::Text("cost %u us/frame (avg %u, max %u) | writes: ours %u, engine %u",
                cLast, cAvg, cMax, ts.animOurWrites, ts.animEngineWrites);
            ImGui::SameLine();
            if (ImGui::SmallButton("reset##animcnt")) Runtime::Tempo::AnimResetCounters();
            const int novr = Runtime::Tempo::OverrideCount();
            if (novr) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "| overrides %d", novr);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("engine overwrote=0 means we hold the fields ourselves.");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Whole-clip playback rate. Torpor still stacks.");
        }
    }

    // --- Пресеты: подбор четырёх чисел руками — плохой интерфейс -----------
    //
    // Фидбек тестера: система гибкая, но «довольно сложная в управлении».
    // Ручки остаются, но начинать надо не с них.
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Presets");
        struct P { const char* name; float lo, hi, alo, ahi, cpl; const char* hint; };
        static const P kP[] = {
            { "Vanilla",   1.00f, 1.00f, 1.00f, 1.00f, 0.0f,
              "Everything off. Baseline for comparison." },
            { "Ragged",    0.90f, 1.20f, 1.00f, 1.12f, 0.5f,
              "A pack that arrives out of sync. Swings mostly vanilla." },
            { "Sprinters", 1.00f, 1.30f, 1.05f, 1.25f, 1.0f,
              "Fast approach and fast swings on the same body - coherent creatures." },
            { "Predators", 0.85f, 1.10f, 1.15f, 1.30f, 0.0f,
              "Move deliberately, strike hard. The wind-up window is gone." },
        };
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Button(kP[i].name)) {
                Runtime::Tempo::SetRange(kP[i].lo, kP[i].hi);
                Runtime::Tempo::SetAnimRange(kP[i].alo, kP[i].ahi);
                Runtime::Tempo::SetAnimCoupling(kP[i].cpl);
                config.setFloat("monsterTempo", "factorMin", kP[i].lo);
                config.setFloat("monsterTempo", "factorMax", kP[i].hi);
                config.setFloat("monsterTempo", "animFactorMin", kP[i].alo);
                config.setFloat("monsterTempo", "animFactorMax", kP[i].ahi);
                config.setFloat("monsterTempo", "animCoupling", kP[i].cpl);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nmove %.2f..%.2f  swing %.2f..%.2f  coupling %.2f",
                                  kP[i].hint, kP[i].lo, kP[i].hi,
                                  kP[i].alo, kP[i].ahi, kP[i].cpl);
            if (i < 3) ImGui::SameLine();
        }
        ImGui::TextDisabled("Presets set both knobs. Sliders stay live.");
    }

    // --- Режиссёр стороны монстров -----------------------------------------
    // Build 012 retains exact target+urgency and splits ownership: Aggro consumes
    // the target while Tempo mobilizes each exact free responder. Existing
    // checkboxes remain the only consent; every safety gate stays fail-closed.
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1, 1), "Monster director (Build 012 urgency + mobilization)");
        bool on = MonsterAI::Enabled();
        if (ImGui::Checkbox("enable monster director", &on)) {
            MonsterAI::SetEnabled(on);
            config.setBool("monsterAI", "enabled", on);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("snapshot to log")) MonsterAI::DumpSnapshot();
        ImGui::TextDisabled("%s", MonsterAI::PackObserveStatus());

        bool pilot = MonsterAI::ActuatorEnabled();
        if (ImGui::Checkbox("enable Director actuator (WRITES)", &pilot)) {
            MonsterAI::SetActuatorEnabled(pilot);
            config.setBool("monsterAI", "wolfActuator", pilot);
        }
        ImGui::Text("policy: %s | %s | gameplay calls %d",
                    MonsterAI::PolicyStatus(),
                    MonsterAI::PolicyEngaged() ? "ENGAGED" : "released",
                    MonsterAI::GameplayWriteCount());
        const Runtime::Tempo::DirectorReadiness tr =
            Runtime::Tempo::GetDirectorReadiness();
        ImGui::TextDisabled("Tempo ready: movement %s | general hook %s | animation %s | attacks-only %s",
            tr.movementEnabled ? "yes" : "NO",
            tr.generalHookInstalled ? "yes" : "NO",
            tr.animationEnabled ? "yes" : "NO",
            tr.attacksOnly ? "yes" : "NO");
        ImGui::TextWrapped("%s", MonsterAI::Status());
        if (on) {
            static const char* kPartyName[4] = {
                "Arisen", "MainPawn", "Hired1", "Hired2"
            };
            const int mark = MonsterAI::PackMarkSlot();
            const int runner = MonsterAI::RunnerUpSlot();
            const int p0 = MonsterAI::PrioritySlot(0);
            const int p1 = MonsterAI::PrioritySlot(1);
            const int p2 = MonsterAI::PrioritySlot(2);
            const int p3 = MonsterAI::PrioritySlot(3);
            const float holdS = (float)MonsterAI::HoldRemainingMs() / 1000.0f;
            const float isolationPct = MonsterAI::TargetIsolationRatio() * 100.0f;
            const float depthPct = MonsterAI::TargetDepthRatio() * 100.0f;
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1),
                "Build 012 target+urgency | Aggro target + Tempo responders | actuator %s | gameplay calls %d",
                MonsterAI::ActuatorEnabled() ? "ON" : "OFF",
                MonsterAI::GameplayWriteCount());
            ImGui::Text("wolves %d | raw priority %s > %s > %s > %s",
                MonsterAI::ScoredWolfCount(),
                p0 >= 0 && p0 < 4 ? kPartyName[p0] : "-",
                p1 >= 0 && p1 < 4 ? kPartyName[p1] : "-",
                p2 >= 0 && p2 < 4 ? kPartyName[p2] : "-",
                p3 >= 0 && p3 < 4 ? kPartyName[p3] : "-");
            ImGui::Text("committed PackMark %s | runner %s | focus intent %s | hold %.1fs",
                mark >= 0 && mark < 4 ? kPartyName[mark] : "none",
                runner >= 0 && runner < 4 ? kPartyName[runner] : "none",
                MonsterAI::RecommendationName(), holdS);
            ImGui::Text("isolation (runner/mark - 1) %+.1f%% | target depth (highest/mark - 1) %+.1f%%",
                isolationPct, depthPct);
            ImGui::TextDisabled(
                "Focus tiers use isolation only: <20%% NONE | 20..<100%% BIAS | >=100%% FOCUS-WINDOW.");
            ImGui::TextDisabled(
                "Decision input: confirmed ABSOLUTE current HP only. max-HP percentage is not used.");
            ImGui::TextDisabled(
                "Strategic DEF/ATK, equipment, vocation, skills and status: ignored; cue uses exact actions + pair distance.");
            ImGui::TextDisabled(
                "Both metrics follow the held PackMark; healing/level-up metadata cannot reset that hold.");
            ImGui::TextDisabled(
                "Actuation requires all four record slots to map to exact unique live bodies; ambiguity releases.");
            ImGui::TextDisabled(
                "GrabStart opens a 5s read-only nearby-action probe; ground pin is not actuated until its exact pair is known.");

            for (int m = 0; m < 4; ++m) {
                MonsterAI::HuntTelemetry h;
                if (!MonsterAI::HuntTelemetryAt(m, &h)) {
                    ImGui::TextDisabled("  %-8s record unavailable", kPartyName[m]);
                    continue;
                }
                const ImVec4 c = m == mark ? ImVec4(1.0f, 0.78f, 0.35f, 1)
                                           : ImVec4(0.82f, 0.82f, 0.82f, 1);
                ImGui::TextColored(c,
                    "%c rank %d | %-8s HP %s%.0f | HP-score %s%.3f | maxHP %.0f diag-only",
                    m == mark ? '*' : ' ', h.priorityRank, kPartyName[m],
                    h.hpValid ? "" : "? ", h.currentHp,
                    h.scoreValid ? "" : "? ", h.huntScore, h.maxHp);
                ImGui::TextDisabled(
                    "    record %s | body %s | position %s (HP ignores; cue requires exact)",
                    h.recordValid ? "valid" : "invalid",
                    h.bodyValid ? "mapped" : "UNVALIDATED",
                    h.positionValid ? "available" : "UNVALIDATED");
                ImGui::TextDisabled(
                    "    CORE/UNVALIDATED ignored: STR %.0f DEF %.0f MAG %.0f MDEF %.0f | loadout totals UNKNOWN",
                    h.coreStrength, h.coreDefense, h.coreMagick,
                    h.coreMagickDefense);
            }

            const int n = MonsterAI::ViewCount();
            for (int i = 0; i < n && i < 8; ++i) {
                const MonsterAI::MonsterView* v = MonsterAI::ViewAt(i);
                if (!v) continue;
                ImGui::TextColored(v->attacking ? ImVec4(1, 0.7f, 0.4f, 1)
                                                : ImVec4(0.8f, 0.8f, 0.8f, 1),
                    "  %-8s %-26s %5.1f m  loco x%.2f atk x%.2f",
                    v->kind, v->act[0] ? v->act : "?", v->distM,
                    v->locoFactor, v->atkFactor);
            }
            if (n > 8) ImGui::TextDisabled("  ... and %d more", n - 8);

            ImGui::TextDisabled(
                "Actuator identity is revalidated internally before writes; use bounded policy transitions in the log.");
        }
    }

    // --- Прибор агра (docs/AGGRO_RECON.md, этап 1) --------------------------
    //
    // Только читает. Ищет в теле врага слоты, ссылающиеся на членов партии,
    // и считает, какой из них МЕНЯЕТ члена: цель — это подвижный слот, а
    // не любой указатель на игрока.
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.8f, 0.6f, 1), "Aggro watch (read-only)");
        bool aw = Runtime::Aggro::Enabled();
        if (ImGui::Checkbox("enable aggro watch", &aw)) {
            Runtime::Aggro::SetEnabled(aw);
            config.setBool("aggro", "watch", aw);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("snapshot")) Runtime::Aggro::DumpSnapshot();
        ImGui::SameLine();
        if (ImGui::SmallButton("roster diff")) Runtime::Aggro::DumpRoster();
        // CARDWATCH (79.0): непрерывное слежение за карточками двух особей
        // с живой дистанцией. Дельта-лог: строка только при изменении.
        bool cw = Runtime::Aggro::CardWatchOn();
        if (ImGui::Checkbox("card watch (2 foes, delta log)", &cw)) {
            Runtime::Aggro::SetCardWatch(cw);
            config.setBool("aggro", "cardwatch", cw);
        }
        ImGui::TextWrapped("%s", Runtime::Aggro::Status());

        // PIN (80.0, AGGRO_RECON §20) — первая мутация трека: штырь
        // внимания на члене партии. Пишет ТОЛЬКО в карты uEm0200,
        // нативное значение 300, readback + откат. Сброс = снять штырь,
        // затухание движка доведёт поле до нуля само.
        const bool directorLease = Runtime::Aggro::DirectorFocusMember() >= 0;
        if (aw && !directorLease) {
            const int pm = Runtime::Aggro::PinMember();
            const int ps = Runtime::Aggro::PinScope();
            // 83.0: FOCUS — продуктовая операция одной кнопкой:
            // pin + suppress + fakehit на выбранном члене. Всё, что
            // ниже (pin/suppress/fakehit по отдельности) — ручной режим
            // для A/B-исследований; любое ручное изменение отсоединяет
            // FOCUS с логом (он не молчит о том, что не правдив).
            const int fm = Runtime::Aggro::PinFocusMember();
            static const char* kFocusBtn[4] = {
                "focus Arisen", "focus Main", "focus H1", "focus H2"
            };
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.4f, 1),
                               "FOCUS = pin+suppress+fakehit  (WRITES!)");
            for (int m = 0; m < 4; ++m) {
                if (fm == m)
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          ImVec4(0.60f, 0.42f, 0.20f, 1));
                if (ImGui::SmallButton(kFocusBtn[m]))
                    Runtime::Aggro::PinFocusSet(m);
                if (fm == m) ImGui::PopStyleColor();
                if (m < 3) ImGui::SameLine();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("release all"))
                Runtime::Aggro::PinFocusSet(-1);
            static const char* kPinBtn[4] = {
                "pin Arisen", "pin Main", "pin H1", "pin H2"
            };
            ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1),
                               "aggro pin (manual, WRITES! uEm0200 only)");
            for (int m = 0; m < 4; ++m) {
                if (pm == m)
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          ImVec4(0.30f, 0.55f, 0.30f, 1));
                if (ImGui::SmallButton(kPinBtn[m]))
                    Runtime::Aggro::PinSet(m, ps);
                if (pm == m) ImGui::PopStyleColor();
                if (m < 3) ImGui::SameLine();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("unpin"))
                Runtime::Aggro::PinSet(-1, ps);
            ImGui::SameLine();
            if (ImGui::SmallButton("scope nearest"))
                Runtime::Aggro::PinSet(pm, 0);
            ImGui::SameLine();
            if (ImGui::SmallButton("scope all"))
                Runtime::Aggro::PinSet(pm, 1);
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", ps ? "all" : "nearest");
            // 81.0: гасить чужие карты до 0 -> чистый argmax.
            bool sp = Runtime::Aggro::PinSuppressOn();
            if (ImGui::Checkbox("suppress others", &sp)) {
                Runtime::Aggro::PinSuppressSet(sp);
                config.setBool("aggro", "pin_suppress", sp);
            }
            // 82.0: фейк-хит — пере-заявка «свежего урона» в блоке B
            // (не сбрасывается восприятием, в отличие от +0x10).
            bool fh = Runtime::Aggro::PinFakehitOn();
            if (ImGui::Checkbox("fake hit (block B)", &fh)) {
                Runtime::Aggro::PinFakehitSet(fh);
                config.setBool("aggro", "pin_fakehit", fh);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("27c=%.0f", config.getFloat("aggro",
                                   "pin_fakehit_value", 150.0f));
            uint32_t pw = 0, pr = 0;
            Runtime::Aggro::PinStats(&pw, &pr);
            if (pm >= 0 || pw)
                ImGui::TextDisabled("pin: writes %u  rollbacks %u", pw, pr);
        } else if (aw && directorLease) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1),
                "Manual PIN/FOCUS controls suspended: Monster Director owns the focus lease.");
        }
        if (aw) {
            // Отметки режут лог замера на A и B. Без них две половины
            // смешаются в одну бесполезную сумму.
            if (ImGui::SmallButton("MARK: lure ON"))  Runtime::Aggro::MarkEvent("lure ON");
            ImGui::SameLine();
            if (ImGui::SmallButton("MARK: lure OFF")) Runtime::Aggro::MarkEvent("lure OFF");
            ImGui::SameLine();
            if (ImGui::SmallButton("MARK: taunt"))    Runtime::Aggro::MarkEvent("taunt used");

            const int n = Runtime::Aggro::RowCount();
            for (int i = 0; i < n && i < 8; ++i) {
                const Runtime::Aggro::Row* r = Runtime::Aggro::RowAt(i);
                if (!r) continue;
                if (r->best < 0) {
                    ImGui::TextDisabled("  %-8s %-22s  %d slots, none moved",
                        r->kind, r->act[0] ? r->act : "?", r->nSlots);
                } else {
                    const Runtime::Aggro::Slot& sl = r->slot[r->best];
                    ImGui::Text("  %-8s %-22s  +0x%04X -> %-9s sw %u  held %u ms",
                        r->kind, r->act[0] ? r->act : "?", sl.off,
                        Runtime::Aggro::MemberName(sl.member),
                        sl.switches, sl.holdMs);
                }
            }
            if (n > 8) ImGui::TextDisabled("  ... and %d more", n - 8);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("No .arc rewrite. Catalog in, live objects out.");

    ImGui::Separator();

    if (ImGui::TreeNode("Policy (not yet bound)"))
    {
        ImGui::TextDisabled("Unbound. Waiting for cThinkMgr.");

        ImGui::PushItemWidth(200.0f);
        if (ImGui::SliderFloat("Target Aggression", &enemyAggression, 0.5f, 3.0f))
            config.setFloat("enemyAI", "aggression", enemyAggression);
        if (ImGui::SliderFloat("Target Reaction Speed", &enemyReactionSpeed, 0.5f, 3.0f))
            config.setFloat("enemyAI", "reactionSpeed", enemyReactionSpeed);
        if (ImGui::SliderInt("Max Simult. Attackers", &maxSimultaneousAttackers, 1, 10))
            config.setInt("enemyAI", "maxAttackers", maxSimultaneousAttackers);
        ImGui::PopItemWidth();

        if (ImGui::Checkbox("Smarter Tactics (live think, not FSM hex)", &smarterTactics))
            config.setBool("enemyAI", "smarterTactics", smarterTactics);
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::TextDisabled("STUB - no cThinkMgr yet.");

    ImGui::PopID();
}

void Hooks::EnemyAI()
{
    enemyAIEnabled = config.getBool("enemyAI", "enabled", true);
    logFile << "EnemyAI module initialized (LIVE stub, no disk writes)" << std::endl;
    InGameUIAdd(RenderEnemyAIUI);
}

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
        if (!ts.enabled) {
            ImGui::TextDisabled("off - [monsterTempo] enabled = on in the ini");
        } else {
            // Счётчики совпадений показываем ВСЕГДА: в Build 70.0 не встал
            // хук обычного движения, а увидеть это было негде — сообщение
            // выводилось только когда не встали ОБА.
            ImGui::TextColored(ts.walkHooked ? ImVec4(0.3f,1,0.3f,1) : ImVec4(1,0.5f,0.3f,1),
                "walk hook: %s (%d sig matches)",
                ts.walkHooked ? "ON" : "not installed", ts.walkMatches);
            ImGui::TextColored(ts.sprintHooked ? ImVec4(0.3f,1,0.3f,1) : ImVec4(1,0.5f,0.3f,1),
                "sprint hook: %s (%d sig matches)",
                ts.sprintHooked ? "ON" : "not installed", ts.sprintMatches);
            if (!ts.walkHooked || !ts.sprintHooked)
                ImGui::TextDisabled("Need both hooks, else one gait stays vanilla.");
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Per-monster speed. Pack arrives out of sync.");
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
    //
    // Пока наблюдатель: показывает, что он видит на шине. Политики появятся
    // только после того, как картина боя окажется верной.
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1, 1), "Monster director (observer)");
        bool on = MonsterAI::Enabled();
        if (ImGui::Checkbox("enable monster director", &on)) {
            MonsterAI::SetEnabled(on);
            config.setBool("monsterAI", "enabled", on);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("snapshot to log")) MonsterAI::DumpSnapshot();
        ImGui::TextWrapped("%s", MonsterAI::Status());
        if (on) {
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

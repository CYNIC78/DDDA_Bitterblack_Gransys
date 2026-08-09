/**
 * EnemyAI.cpp — AI Overhaul для монстров
 * 
 * Фаза 2 (текущая): Редактирование через .arc файлы
 * Фаза 3 (будущее): Хуки AI-функций (отложено)
 *
 * Подход: СТАТИЧЕСКИЙ — редактируем файлы игры через ARCtool.
 * Никакого реверс-инжиниринга, только правка данных.
 */

#include "stdafx.h"
#include "EnemyAI.h"

// ============================================================
// Состояние модуля
// ============================================================

static bool enemyAIEnabled = true;

// === Настройки через .arc (требуют переупаковки game_main.arc) ===
static float enemyAggression = 1.3f;
static float enemyReactionSpeed = 1.2f;
static int maxSimultaneousAttackers = 5;
static bool smarterTactics = true;

// === Какие файлы мы редактируем (см. ARC_MAP.txt) ===
// 
// 🟢 Параметры AI-действий пешек:
//    game_main/AI/AIPlayerActionParameter/AIPlActParam*.xml
//    — после распаковки ARCtool -xfs
//    Содержат: приоритеты действий, дистанции, веса
//
// 🟢 Спавны врагов (LOT):
//    stage100/scr/st100/etc/*.lot → .txt через ARCtool -lot
//    Содержат: позиции, типы, уровни врагов
//
// 🟡 AI-параметры врагов (FSM):
//    stage*/scr/st*/fsm/*.fsm — бинарный формат
//    Редактируются через хекс-редактор
//    (не конвертируются в читаемый формат!)
//
// 🟢 Параметры аугментов/скиллов:
//    game_main/param/pl/other/PlAbilityParam.ablparam.xml
//

// ============================================================
// ImGui UI
// ============================================================

void RenderEnemyAIUI()
{
    if (!ImGui::CollapsingHeader("Enemy AI Overhaul"))
        return;

    ImGui::PushID("EnemyAI");

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), 
        "Phase 2: Static .arc editing (no reverse engineering!)");

    if (ImGui::Checkbox("Enable Enemy AI Module", &enemyAIEnabled))
        config.setBool("enemyAI", "enabled", enemyAIEnabled);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), 
        "How to edit (no Cheat Engine needed):");

    ImGui::BulletText("1. Run: python arctool_helper.py unpack game_main.arc");
    ImGui::BulletText("2. Edit XML files in game_main/AI/AIPlayerActionParameter/");
    ImGui::BulletText("3. Run: python arctool_helper.py pack game_main");
    ImGui::BulletText("4. Copy game_main.arc back to nativePC/rom/");

    ImGui::Separator();

    if (ImGui::TreeNode("AI Action Parameters (Pawn Behavior)"))
    {
        ImGui::TextWrapped(
            "Files in game_main/AI/AIPlayerActionParameter/ control HOW pawns act.\n"
            "After unpacking with -xfs, you get .xml files you can edit.\n"
            "Likely parameters inside:\n"
            "  - Action priorities (attack vs defend vs buff vs heal)\n"
            "  - Engagement distances (how close before switching to melee)\n"
            "  - Skill usage weights (how often each skill is picked)\n"
            "  - Reaction thresholds (HP%, enemy state, etc.)"
        );
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Enemy Spawn Editing (LOT files)"))
    {
        ImGui::TextWrapped(
            "LOT files in stage100/scr/st100/etc/ define enemy spawns.\n"
            "Unpack with -lot flag to get .txt files.\n"
            "You can change:\n"
            "  - Enemy type (goblin → warg, cyclops → gorecyclops)\n"
            "  - Enemy level and scale\n"
            "  - Number of enemies per spawn group\n"
            "  - Respawn behavior"
        );
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("FSM Files (Advanced)"))
    {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "FSM = Finite State Machine — the actual AI brain.");
        ImGui::TextWrapped(
            "Found in: stage*/scr/st*/fsm/*.fsm\n"
            "These are BINARY files. MT Framework format.\n"
            "Cannot be converted to readable text by ARCtool.\n"
            "Editing requires:\n"
            "  - Hex editor\n"
            "  - Understanding of MT Framework FSM structure\n"
            "  - OR: wait for a community FSM editor tool\n"
            "\n"
            "This is where Lefein did her magic for World Difficulty."
        );
        ImGui::TreePop();
    }

    ImGui::Separator();
    
    if (ImGui::TreeNode("Quick Actions"))
    {
        ImGui::TextWrapped(
            "These settings serve as documentation for the .arc edits.\n"
            "Actual changes must be made in the XML/LOT files."
        );

        ImGui::PushItemWidth(200.0f);
        if (ImGui::SliderFloat("Target Aggression", &enemyAggression, 0.5f, 3.0f))
            config.setFloat("enemyAI", "aggression", enemyAggression);
        if (ImGui::SliderFloat("Target Reaction Speed", &enemyReactionSpeed, 0.5f, 3.0f))
            config.setFloat("enemyAI", "reactionSpeed", enemyReactionSpeed);
        if (ImGui::SliderInt("Max Simult. Attackers", &maxSimultaneousAttackers, 1, 10))
            config.setInt("enemyAI", "maxAttackers", maxSimultaneousAttackers);
        ImGui::PopItemWidth();

        if (ImGui::Checkbox("Smarter Tactics (FSM edits)", &smarterTactics))
            config.setBool("enemyAI", "smarterTactics", smarterTactics);
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
        "Module status: STATIC MODE (arc editing)\n"
        "Dynamic hooks: PLANNED for Phase 3 (future)");

    ImGui::PopID();
}

// ============================================================
// Инициализация
// ============================================================

void Hooks::EnemyAI()
{
    enemyAIEnabled = config.getBool("enemyAI", "enabled", true);

    logFile << "EnemyAI module initialized (STATIC mode)" << std::endl;
    logFile << "  Edit AI via ARCtool → unpack game_main.arc → edit XML → repack" << std::endl;

    InGameUIAdd(RenderEnemyAIUI);
}

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

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1),
        "Runtime-first. Sliders are wishes until we hold a live think object.");

    if (ImGui::Checkbox("Enable Enemy AI Module", &enemyAIEnabled))
        config.setBool("enemyAI", "enabled", enemyAIEnabled);

    ImGui::Separator();
    ImGui::TextWrapped(
        "We do not replace the player's .arc. XML/LOT are a catalog: "
        "read at build time, written into loaded objects in memory. "
        "See docs/ROADMAP.md phase 3.");

    ImGui::Separator();

    if (ImGui::TreeNode("Policy (not yet bound)"))
    {
        ImGui::TextWrapped(
            "These sliders are the policy we WILL write into live think objects. "
            "They do not touch disk. They do nothing until Phase 3.1 finds cThinkMgr.");

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
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1),
        "Status: STUB. Waiting for WorldScan to hand us cThinkMgr.");

    ImGui::PopID();
}

void Hooks::EnemyAI()
{
    enemyAIEnabled = config.getBool("enemyAI", "enabled", true);
    logFile << "EnemyAI module initialized (LIVE stub, no disk writes)" << std::endl;
    InGameUIAdd(RenderEnemyAIUI);
}

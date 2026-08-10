#include "stdafx.h"
#include "CombatIntel.h"
#include "BestiaryData.h"

#define PLAYER_BASE         0xA7000
#define PAWN_OFFSET         0x7F0
#define MSTUDYFLAG_OFFSET   0x1616
#define MSTUDYFLAG_SIZE     322
#define COMBAT_TIMEOUT_SEC  5.0f

struct CombatEntry { BYTE groupId; DWORD timestamp; };
#define RING_SIZE 32

static CombatEntry g_ring[RING_SIZE];
static int g_head = 0;
static CRITICAL_SECTION g_lock;
static bool g_enabled = true;

static LPBYTE pDmg1, oDmg1, pDmg2, oDmg2, pDmg3, oDmg3;

void __stdcall OnDamage(BYTE* targetBase, float dmg)
{
    BYTE gid = targetBase[0x2D];
    EnterCriticalSection(&g_lock);
    g_ring[g_head].groupId = gid;
    g_ring[g_head].timestamp = GetTickCount();
    g_head = (g_head + 1) % RING_SIZE;
    LeaveCriticalSection(&g_lock);
}

void __declspec(naked) HDmg1()
{
    __asm
    {
        mov eax, [esp]
        pushad
        push eax
        push ebx
        call OnDamage
        popad
        jmp oDmg1
    }
}

void __declspec(naked) HDmg2()
{
    __asm
    {
        mov eax, [esp]
        pushad
        push eax
        push esi
        call OnDamage
        popad
        jmp oDmg2
    }
}

void __declspec(naked) HDmg3()
{
    __asm
    {
        mov eax, [esp]
        pushad
        push eax
        push esi
        call OnDamage
        popad
        jmp oDmg3
    }
}

static float GetKnowledgeLevel(int mStudyIdx)
{
    if (!pBase || !*pBase) return 0.0f;
    if (mStudyIdx < 0 || mStudyIdx >= MSTUDYFLAG_SIZE) return 0.0f;
    BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
    BYTE val = study[mStudyIdx];
    if (val == 0) return 0.0f;
    int bits = 0;
    for (int i = 0; i < 8; i++)
        if (val & (1 << i)) bits++;
    if (bits >= 6) return 0.95f;
    if (bits >= 4) return 0.80f + (bits - 4) * 0.05f;
    if (bits >= 2) return 0.55f + (bits - 2) * 0.075f;
    return 0.35f;
}

static bool PawnKnowsGroup(BYTE gid)
{
    EnemyEntry* e = FindEnemyByGid(gid);
    if (!e || e->mStudyIdx < 0) return false;
    return GetKnowledgeLevel(e->mStudyIdx) > 0.0f;
}

static void AnalyzeCombat(int* total, int* unknown, float* avgKnowledge)
{
    *total = 0; *unknown = 0; *avgKnowledge = 0.0f;
    DWORD now = GetTickCount();
    bool seen[256] = {};
    float sumK = 0.0f;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < RING_SIZE; i++) {
        BYTE gid = g_ring[i].groupId;
        if (gid == 0 || seen[gid]) continue;
        if (now - g_ring[i].timestamp > COMBAT_TIMEOUT_SEC * 1000) continue;
        seen[gid] = true;
        (*total)++;
        EnemyEntry* e = FindEnemyByGid(gid);
        if (e && e->mStudyIdx >= 0) {
            float kl = GetKnowledgeLevel(e->mStudyIdx);
            sumK += kl;
            if (kl < 0.3f) (*unknown)++;
        }
    }
    LeaveCriticalSection(&g_lock);
    if (*total > 0) *avgKnowledge = sumK / (float)(*total);
}

float GetCombatUtilitarianConfidence()
{
    if (!g_enabled) return 0.5f;
    int total, unknown; float avgK;
    AnalyzeCombat(&total, &unknown, &avgK);
    if (total == 0) return 0.5f;
    return 0.25f + avgK * 0.65f;
}

bool IsInCombat()
{
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < RING_SIZE; i++) {
        if (g_ring[i].groupId != 0 && (now - g_ring[i].timestamp) / 1000 < (DWORD)COMBAT_TIMEOUT_SEC) {
            LeaveCriticalSection(&g_lock);
            return true;
        }
    }
    LeaveCriticalSection(&g_lock);
    return false;
}

int GetCombatEnemyCategory()
{
    int bestCat = -1;
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < RING_SIZE; i++) {
        BYTE gid = g_ring[i].groupId;
        if (gid == 0) continue;
        if ((now - g_ring[i].timestamp) / 1000 >= (DWORD)COMBAT_TIMEOUT_SEC) continue;
        int cat = GetEnemyCategory(gid);
        if (cat > bestCat) bestCat = cat;
    }
    LeaveCriticalSection(&g_lock);
    return bestCat;
}

void RenderCombatIntelUI()
{
    if (!ImGui::CollapsingHeader("Combat Intel v2")) return;
    ImGui::PushID("CI");
    if (ImGui::Checkbox("Enable Combat Intel", &g_enabled))
        config.setBool("combatIntel","enabled",g_enabled);
    ImGui::Separator();
    int total, unknown; float avgK;
    AnalyzeCombat(&total, &unknown, &avgK);
    float conf = GetCombatUtilitarianConfidence();
    if (total > 0) {
        ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"IN COMBAT");
        ImGui::Text("Types: %d | Unknown: %d | Know: %.0f%%", total, unknown, avgK*100);
        ImGui::ProgressBar(conf, ImVec2(200,0));
        ImGui::SameLine();
        ImGui::TextDisabled("Util:%.0f%%", conf*100);
    } else {
        ImGui::TextDisabled("No combat");
    }
    ImGui::Separator();
    if (ImGui::TreeNode("Damage Ring Buffer")) {
        DWORD now = GetTickCount();
        EnterCriticalSection(&g_lock);
        for (int i = 0; i < RING_SIZE; i++) {
            if (g_ring[i].groupId == 0) continue;
            DWORD age = (now - g_ring[i].timestamp) / 1000;
            bool active = age < (DWORD)COMBAT_TIMEOUT_SEC;
            EnemyEntry* e = FindEnemyByGid(g_ring[i].groupId);
            ImGui::TextColored(active?ImVec4(1,0.5f,0.3f,1):ImVec4(0.5f,0.5f,0.5f,1),
                "gid=0x%02X %-15s %s %ds", g_ring[i].groupId,
                e?e->name:"???", PawnKnowsGroup(g_ring[i].groupId)?"KNOWN":"unk", age);
        }
        LeaveCriticalSection(&g_lock);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Bestiary (types.tsv + bestiary.py)")) {
        ImGui::TextWrapped("72 enemy types. Sources: Atvaark + chrispurnell.");
        for (auto* e = g_bestiary; e->name; e++) {
            if (e->groupId == 0xFF && e->mStudyIdx < 0) continue;
            ImGui::Text("%02X mIdx=%d %s %.0f%% %s", e->groupId, e->mStudyIdx,
                e->name, GetKnowledgeLevel(e->mStudyIdx)*100,
                e->mStudyIdx>=0?"KNOWN":"?");
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Hooks::CombatIntel()
{
    InitializeCriticalSection(&g_lock);
    memset(g_ring, 0, sizeof(g_ring));
    g_enabled = config.getBool("combatIntel","enabled",true);
    BYTE s1[]={0x51,0xF3,0x0F,0x11,0x0C,0x24,0xE8,0xCC,0xCC,0xCC,0xCC,0x8B,0x13,0x8B,0x82,0xD4,0x01,0x00,0x00};
    BYTE s2[]={0x51,0xF3,0x0F,0x11,0x0C,0x24,0xE8,0xCC,0xCC,0xCC,0xCC,0x8B,0x16,0x8B,0x82,0xD4,0x01,0x00,0x00};
    BYTE s3[]={0x51,0xF3,0x0F,0x11,0x0C,0x24,0xE8,0xCC,0xCC,0xCC,0xCC,0x8B,0x06,0x8B,0x90,0xD4,0x01,0x00,0x00};
    bool o1=Hooks::FindSignature("CI1",s1,&pDmg1);
    bool o2=Hooks::FindSignature("CI2",s2,&pDmg2);
    bool o3=Hooks::FindSignature("CI3",s3,&pDmg3);
    if (o1) Hooks::CreateHook("CI1",pDmg1+=6,HDmg1,(LPVOID*)&oDmg1,true);
    if (o2) Hooks::CreateHook("CI2",pDmg2+=6,HDmg2,(LPVOID*)&oDmg2,true);
    if (o3) Hooks::CreateHook("CI3",pDmg3+=6,HDmg3,(LPVOID*)&oDmg3,true);
    InGameUIAdd(RenderCombatIntelUI);
    logFile << "CombatIntel v2.1: " << (o1?1:0)+(o2?1:0)+(o3?1:0) << "/3 hooks, "
            << BESTIARY_COUNT << " entries (types.tsv)" << std::endl;
}

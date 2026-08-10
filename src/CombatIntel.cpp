#include "stdafx.h"
#include "MinHook/MinHook.h"
extern BYTE *codeBase, *codeEnd;
#include "CombatIntel.h"
#include "BestiaryData.h"
#include "CombatBus.h"
#include "EnemyTypes.Generated.h"

#define PLAYER_BASE         0xA7000
#define PAWN_OFFSET         0x7F0
#define MSTUDYFLAG_OFFSET   0x1616
#define MSTUDYFLAG_SIZE     322
#define COMBAT_TIMEOUT_SEC  5.0f

enum DamageSource : BYTE { SRC_PLAYER=0, SRC_PAWN=1, SRC_OTHER=2 };
struct CombatEntry { BYTE groupId; BYTE source; DWORD timestamp; uint32_t vtableRVA; uintptr_t targetPtr; };
#define RING_SIZE 32


static CombatEntry g_ring[RING_SIZE];
static int g_head = 0;
static CRITICAL_SECTION g_lock;
static bool g_enabled = true;

// Hit counters — тик под капотом, инкремент только на новом попадании (>200мс)
static int g_hitCount[256] = {};
static int g_playerHitsTotal = 0;
static int g_pawnHitsTotal = 0;
static DWORD g_lastHitTick[256] = {};
static int g_totalHits = 0;

static LPBYTE pDmg1, oDmg1, pDmg2, oDmg2, pDmg3, oDmg3;
static void PublishToBus();

// VTable disambiguation: пока raw (бандиты 0x84 уже отдельно от волков 0x08), vtable вернем позже
static BYTE ResolveGid(BYTE* targetBase, BYTE rawGid){
    (void)targetBase;
    return rawGid;
}

// Внутренний хелпер — понятно кто бьет (игрок/пешка), throttle 200мс под капотом
static void OnDamageInternal(BYTE* targetBase, DamageSource src){
    BYTE raw = targetBase[0x2D];
    BYTE gid = ResolveGid(targetBase, raw);
    DWORD now = GetTickCount();
    if(now - g_lastHitTick[gid] < 200 && g_hitCount[gid]>0){
        EnterCriticalSection(&g_lock);
        g_ring[g_head].groupId = gid;
        g_ring[g_head].source = (BYTE)src;
        g_ring[g_head].timestamp = now;
        g_ring[g_head].vtableRVA = 0;
        g_ring[g_head].targetPtr = (uintptr_t)targetBase;
        g_head = (g_head + 1) % RING_SIZE;
        LeaveCriticalSection(&g_lock);
        PublishToBus();
        return;
    }
    g_lastHitTick[gid]=now;
    g_hitCount[gid]++; g_totalHits++;
    if(src==SRC_PAWN) g_pawnHitsTotal++; else if(src==SRC_PLAYER) g_playerHitsTotal++;
    EnterCriticalSection(&g_lock);
    g_ring[g_head].groupId = gid;
    g_ring[g_head].source = (BYTE)src;
    g_ring[g_head].timestamp = now;
    g_ring[g_head].vtableRVA = 0;
    g_ring[g_head].targetPtr = (uintptr_t)targetBase;
    g_head = (g_head + 1) % RING_SIZE;
    LeaveCriticalSection(&g_lock);
    PublishToBus();
}
void __stdcall OnDamage_Player(BYTE* targetBase, float dmg){ (void)dmg; OnDamageInternal(targetBase, SRC_PLAYER); }
void __stdcall OnDamage_Pawn(BYTE* targetBase, float dmg){ (void)dmg; OnDamageInternal(targetBase, SRC_PAWN); }
void __stdcall OnDamage_Other(BYTE* targetBase, float dmg){ (void)dmg; OnDamageInternal(targetBase, SRC_OTHER); }
// Совместимость: старый OnDamage = игрок
void __stdcall OnDamage(BYTE* targetBase, float dmg){ OnDamageInternal(targetBase, SRC_PLAYER); }

void __declspec(naked) HDmg1()
{
    __asm
    {
        mov eax, [esp]
        pushad
        push eax
        push ebx
        call OnDamage_Player
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
        call OnDamage_Pawn
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
        call OnDamage_Other
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


// ========== BUS: тренер с мегафоном (CombatIntel публикует, PawnAI-модули слушают) ==========
static void PublishToBus(){
    CombatReport r{};
    r.timestampMs = GetTickCount();
    // считаем напрямую без рекурсии
    int total=0, unknown=0; float avgK=0.f;
    AnalyzeCombat(&total,&unknown,&avgK);
    r.distinctTypes = total; r.unknownTypes = unknown; r.avgKnowledge01 = avgK;
    if(total==0) r.utilitarianConfidence = 0.5f;
    else r.utilitarianConfidence = 0.25f + avgK * 0.65f;
    // inCombat — напрямую из ring
    r.inCombat = (total>0);
    {
        int bestCat=-1;
        DWORD now = GetTickCount();
        EnterCriticalSection(&g_lock);
        for(int i=0;i<RING_SIZE;i++){
            BYTE gid=g_ring[i].groupId;
            if(gid==0) continue;
            if((now - g_ring[i].timestamp)/1000 >= (DWORD)COMBAT_TIMEOUT_SEC) continue;
            int cat=GetEnemyCategory(gid);
            if(cat>bestCat) bestCat=cat;
        }
        LeaveCriticalSection(&g_lock);
        r.dominantCategory = bestCat;
    }
    DWORD now = GetTickCount();
    bool seen[256]={};
    bool seenPawn[256]={}, seenPlayer[256]={};
    int out=0, outPawn=0;
    int pawnTypes=0, playerTypes=0;
    // Сначала считаем pawn distinct отдельно
    EnterCriticalSection(&g_lock);
    for(int i=0;i<RING_SIZE && out<8;i++){
        BYTE gid=g_ring[i].groupId;
        BYTE src=g_ring[i].source;
        if(gid==0 || seen[gid]) continue;
        if(now - g_ring[i].timestamp > (DWORD)(COMBAT_TIMEOUT_SEC*1000)) continue;
        seen[gid]=true;
        auto* info = FindByGroupId(gid);
        auto* entry = FindEnemyByGid(gid);
        r.enemies[out].groupId = gid;
        r.enemies[out].uEmName = info ? info->uEmName : (entry ? entry->name : nullptr);
        r.enemies[out].vtableRVA = info ? info->vtableRVA : 0;
        r.enemies[out].lastSeenMs = g_ring[i].timestamp;
        r.enemies[out].hitByPawn = false; r.enemies[out].hitByPlayer=false;
        // пометим кто бил
        for(int k=0;k<RING_SIZE;k++) if(g_ring[k].groupId==gid && now - g_ring[k].timestamp <= (DWORD)(COMBAT_TIMEOUT_SEC*1000)){
            if(g_ring[k].source==SRC_PAWN) r.enemies[out].hitByPawn=true;
            if(g_ring[k].source==SRC_PLAYER) r.enemies[out].hitByPlayer=true;
        }
        if(entry && entry->mStudyIdx>=0){
            r.enemies[out].knowledge01 = GetKnowledgeLevel(entry->mStudyIdx);
            r.enemies[out].isUnknown = r.enemies[out].knowledge01 < 0.3f;
        } else {
            r.enemies[out].knowledge01 = 0.f;
            r.enemies[out].isUnknown = true;
        }
        out++;
    }
    // Отдельный список enemiesFromPawns (топ-4)
    for(int i=0;i<RING_SIZE && outPawn<4;i++){
        BYTE gid=g_ring[i].groupId;
        if(gid==0 || seenPawn[gid]) continue;
        if(g_ring[i].source != SRC_PAWN) continue;
        if(now - g_ring[i].timestamp > (DWORD)(COMBAT_TIMEOUT_SEC*1000)) continue;
        seenPawn[gid]=true;
        auto* info = FindByGroupId(gid);
        auto* entry = FindEnemyByGid(gid);
        r.enemiesFromPawns[outPawn].groupId = gid;
        r.enemiesFromPawns[outPawn].uEmName = info ? info->uEmName : (entry ? entry->name : nullptr);
        r.enemiesFromPawns[outPawn].lastSeenMs = g_ring[i].timestamp;
        r.enemiesFromPawns[outPawn].hitByPawn = true;
        r.enemiesFromPawns[outPawn].hitByPlayer = false;
        if(entry && entry->mStudyIdx>=0){
            r.enemiesFromPawns[outPawn].knowledge01 = GetKnowledgeLevel(entry->mStudyIdx);
            r.enemiesFromPawns[outPawn].isUnknown = r.enemiesFromPawns[outPawn].knowledge01 < 0.3f;
        } else { r.enemiesFromPawns[outPawn].knowledge01=0.f; r.enemiesFromPawns[outPawn].isUnknown=true; }
        outPawn++;
    }
    // Посчитаем distinct по источникам
    for(int i=0;i<256;i++){ if(seenPawn[i]) pawnTypes++; }
    // player distinct = total - pawn-only? проще пересчитать
    for(int i=0;i<RING_SIZE;i++) if(g_ring[i].source==SRC_PLAYER && !seenPlayer[g_ring[i].groupId] && now - g_ring[i].timestamp <= (DWORD)(COMBAT_TIMEOUT_SEC*1000) && g_ring[i].groupId!=0){ seenPlayer[g_ring[i].groupId]=true; playerTypes++; }
    LeaveCriticalSection(&g_lock);
    r.enemyCount = out;
    r.pawnEnemyCount = outPawn;
    r.pawnDistinct = pawnTypes;
    r.playerDistinct = playerTypes;
    r.pawnHits = g_pawnHitsTotal;
    r.playerHits = g_playerHitsTotal;
    CombatBus::Instance().Publish(r);
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
    // Human bandits/soldiers -> medium
    // quick check before loop for 0xE0
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
    PublishToBus();//UI periodic
    ImGui::PushID("CI");
    if (ImGui::Checkbox("Enable Combat Intel", &g_enabled))
        config.setBool("combatIntel","enabled",g_enabled);
    int total, unknown; float avgK;
    AnalyzeCombat(&total, &unknown, &avgK);
    float conf = GetCombatUtilitarianConfidence();
    auto &bus2 = CombatBus::Instance().LastReport();
    if (total > 0) {
        ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"IN COMBAT");
        ImGui::Text("Types: %d (P:%d W:%d) | Unknown: %d | Know: %.0f%%", total, bus2.playerDistinct, bus2.pawnDistinct, unknown, avgK*100);
        ImGui::Text("Hits: Player %d | Pawn %d | Total %d", bus2.playerHits, bus2.pawnHits, g_totalHits);
        if(bus2.pawnEnemyCount>0){
            ImGui::TextColored(ImVec4(0.6f,0.9f,1,1),"Pawn hits:");
            for(int i=0;i<bus2.pawnEnemyCount;i++){
                auto &e = bus2.enemiesFromPawns[i];
                ImGui::BulletText("0x%02X %s", e.groupId, e.uEmName?e.uEmName:"???");
            }
        }
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
        // HitCounter (только на новом попадании, throttle 200ms под капотом)
        ImGui::TextDisabled("Total hits: %d", g_totalHits);
        for(int k=0;k<256;k++) if(g_hitCount[k]) ImGui::Text(" gid 0x%02X hits %d", k, g_hitCount[k]);
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
    // Safe pawn hunter: только 51 F3 0F 11 ... E8 + 8B ? 8B ? D4 01 и pawn-like (esi)
    {
        int extraPawn=0;
        for(BYTE* cur=::codeBase; cur+16 < ::codeEnd; cur++){
            if(cur[0]!=0x51 || cur[1]!=0xF3 || cur[2]!=0x0F || cur[3]!=0x11 || cur[4]!=0x0C || cur[5]!=0x24 || cur[6]!=0xE8) continue;
            if(cur[11]!=0x8B) continue;
            if(cur[13]!=0x8B) continue;
            if(cur[15]!=0xD4 || cur[16]!=0x01) continue;
            BYTE* hookAt = cur+6;
            if(hookAt==pDmg1+6 || hookAt==pDmg2+6 || hookAt==pDmg3+6) continue;
            bool isPawnLike = (cur[12]==0x16) || (cur[12]==0x06);
            if(!isPawnLike) continue;
            LPBYTE oTmp=nullptr;
            if(MH_CreateHook(hookAt, HDmg2, (LPVOID*)&oTmp)==MH_OK){
                MH_EnableHook(hookAt);
                extraPawn++;
                logFile << "CombatIntel pawn extra hook at " << (void*)hookAt << std::endl;
                if(extraPawn>=3) break;
            }
        }
        if(extraPawn) logFile << "CombatIntel pawn extra hooks: " << extraPawn << std::endl;
    }
    InGameUIAdd(RenderCombatIntelUI);
    logFile << "CombatIntel v2.4 pawn-fix: " << (o1?1:0)+(o2?1:0)+(o3?1:0) << "/3 hooks, "
            << BESTIARY_COUNT << " entries (types.tsv) pawnHits enabled" << std::endl;
}
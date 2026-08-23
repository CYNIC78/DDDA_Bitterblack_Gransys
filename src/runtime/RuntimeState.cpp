// Runtime — состояние продуктового слоя.
//
// Все разделяемые переменные рантайма собраны в одной единице трансляции,
// чтобы не было вопроса «в каком .cpp живёт эта переменная». Объявления —
// в RuntimeInternal.h.

#include "stdafx.h"
#include "RuntimeInternal.h"

namespace Runtime {

ActorDump g_act[32];
uintptr_t g_pawnCombatTarget = 0;
int       g_nAct = 0;
uintptr_t g_pollAddr = 0x10000000u;
uintptr_t g_lastBand = 0x10000000u;
uint32_t g_actSlotOff  = 0;
bool     g_actFullScan = false;
PartyBodyDump g_party[kPartyMaxBodies];
PartyBodyDump g_partyChosen[kPartyExactSlots];
int            g_nParty = 0;
int            g_partyRawCandidates = 0;
uintptr_t      g_partyPawnMgr[8];
int            g_nPartyPawnMgr = 0;
PartyVtClass   g_partyVtCache[kPartyVtCacheSize];
int            g_partyVtChecked = 0;
int            g_partyVtNamed = 0;
PartyNearType  g_partyNear[kPartyMaxNearTypes];
int            g_nPartyNear = 0;
PartyRuntimeProbe g_partyRuntime[kPartyMaxRuntimeProbes];
int            g_nPartyRuntime = 0;
PawnAiCandidate g_pawnAi[kPawnAiMaxCandidates];
int            g_nPawnAi = 0;
int            g_partySeq = 0;
DWORD          g_partyFindMs = 0;
volatile LONG  g_partyBusy = 0;
char           g_partyStatus[192] = "not scanned";
char           g_partyLastFile[MAX_PATH] = "";
PartyPriorityProfileRule g_priorityProfileRules[kPriorityProfileMaxRules];
int            g_nPriorityProfileRules = 0;
char           g_priorityProfileActive[40] = "vanilla";
uint32_t       g_priorityProfileConfigHash = 0;
bool           g_priorityProfileLoaded = false;
bool           g_priorityProfileFileOk = false;
bool           g_priorityProfileApplied = false;
bool           g_priorityProfileConverged = false;
int            g_priorityProfileWrites = 0;
int            g_priorityProfileRestores = 0;
DWORD          g_priorityProfileLastPoll = 0;
DWORD          g_priorityProfileWorldSince = 0;
DWORD          g_priorityProfileLastDiscover = 0;
char           g_priorityProfileStatus[192] = "Priority profile: vanilla";
bool   g_arisenPosOk = false;
bool   g_pawnPosOk   = false;
bool   g_wasInWorld  = false;
float  g_arisenPosX = 0, g_arisenPosY = 0, g_arisenPosZ = 0;
float  g_pawnPosX = 0, g_pawnPosY = 0, g_pawnPosZ = 0;
DWORD  g_pawnPosLastFailLog = 0;
bool   g_pawnPosWasOk = true;
DWORD g_partyPosLastDiscover = 0;
int   g_partyPosAttempts = 0;
PartyPriorityProfileRule g_guardianFixRule;
bool   g_guardianFixInit = false;
bool   g_guardianFixArmed = false;
bool   g_guardianFixApplied = false;
int    g_guardianFixWrites = 0;
int    g_guardianFixRollbacks = 0;
char   g_guardianFixStatus[160] = "Guardian fix: disabled";

ResearchHooks g_research = {};

void SetResearchHooks(const ResearchHooks& hooks)
{
    g_research = hooks;
}

} // namespace Runtime

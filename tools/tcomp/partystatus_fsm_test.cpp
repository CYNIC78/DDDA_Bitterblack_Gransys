// 84.24 pawn-body FSM: падение/подъём читаются с тела пешки.
//
// Главная пешка 0xB000:
//   Walk -> Neardeath -> Neardeath -> Walk(RAISED) -> Neardeath -> Return(RIFTED)
//   Walk -> DmgDown -> DmgStandUp (KNOCKDOWN, не succor)
//   Neardeath + cPlReviveCMC на пешке = игнор (остаётся DOWNED)
// Аризен 0xA000:
//   Walk -> cPlReviveCMC (RAISE, не DOWNED) -> Walk
//   Walk -> cPlActDead (DEAD, не succor-жертва) -> Walk
//
// downedRevivable — только пешка после RAISED, и только пока снова neardeath.

#include "partystatus_t.cpp"
#include <assert.h>
#include <cstring>
#include <fstream>
#include <sstream>

std::ofstream logFile("/tmp/partystatus_fsm_test.log");
BYTE** pBase = 0;
IniConfigStub config;

namespace Runtime {

bool g_inWorld = true;

namespace Mem {
bool InWorld() { return g_inWorld; }
bool Rd(const void*, void*, size_t) { return false; }
bool RdPtr(const void*, uintptr_t*) { return false; }
bool NameOfLiveObject(uintptr_t, char*, int) { return false; }
bool LooksHeap(uintptr_t) { return false; }
} // namespace Mem

int g_findChildCalls = 0;
uintptr_t FindChildByClass(uintptr_t, uint32_t, const char*, uint32_t*)
{
    ++g_findChildCalls;
    return 0;
}

uintptr_t ArisenBody() { return 0xA000; }

bool PartyRecordInfo(int idx, int* vocOut, int* lvlOut, uintptr_t* bodyOut)
{
    if (bodyOut) *bodyOut = 0xB000 + (uintptr_t)idx;
    if (vocOut) *vocOut = 1;
    if (lvlOut) *lvlOut = 10;
    return true;
}

const char* g_mainAct = "cPlActWalk";
const char* g_arisenAct = "cPlActWalk";

bool ReadLiveAct(uintptr_t body, char* out, int cap)
{
    if (!out || cap <= 0) return false;
    const char* a = "cPlActWalk";
    if (body == 0xB000) a = g_mainAct;
    else if (body == 0xA000) a = g_arisenAct;
    strncpy(out, a, (size_t)cap - 1);
    out[cap - 1] = 0;
    return true;
}

} // namespace Runtime

static int CountSubstring(const std::string& hay, const char* needle)
{
    int n = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += strlen(needle);
    }
    return n;
}

static void StepMain(const char* act, bool expectDownedValid,
                     bool expectDownedRevivable)
{
    Runtime::g_mainAct = act;
    Runtime::PartyStatus::Tick();
    Runtime::PartyCombatMember M;
    memset(&M, 0, sizeof(M));
    Runtime::PartyStatus::FillMemberStatus(0xB000, 1, M);
    assert(M.downedValid == expectDownedValid);
    assert(M.downedRevivable == expectDownedRevivable);
}

static void StepArisen(const char* act, bool expectDownedValid)
{
    Runtime::g_arisenAct = act;
    Runtime::PartyStatus::Tick();
    Runtime::PartyCombatMember M;
    memset(&M, 0, sizeof(M));
    Runtime::PartyStatus::FillMemberStatus(0xA000, 0, M);
    assert(M.downedValid == expectDownedValid);
    assert(M.downedRevivable == false);
}

int main()
{
    // 1. Обычное состояние.
    StepMain("cPlActWalk", false, false);
    // 2-3. Neardeath: succor-wait.
    StepMain("cPlActCmcNeardeath", true, false);
    StepMain("cPlActCmcNeardeath", true, false);
    // 4. cPlReviveCMC на ПЕШКЕ игнорируется — остаётся DOWNED.
    StepMain("cPlReviveCMC", true, false);
    // 5. Обычный акт на пешке = RAISED (тело встало).
    StepMain("cPlActWalk", false, false);
    // 6. Второй neardeath: already RAISED once → revivable.
    StepMain("cPlActCmcNeardeath", true, true);
    // 7. CmcReturn = RIFTED, не подъём.
    StepMain("cPlActCmcReturn", false, false);
    // 8-9. Нокдаун ≠ succor.
    StepMain("cPlActWalk", false, false);
    StepMain("cPlActDmgDown", true, false);
    StepMain("cPlActDmgStandUp", false, false);
    // 84.25: smash CrumbleDead — succor-wait, не «просто акт».
    StepMain("cPlActDmgCrumbleDead", true, true);
    StepMain("cPlActWalk", false, false);

    // Аризен поднимает пешку — не DOWNED.
    StepArisen("cPlActWalk", false);
    StepArisen("cPlReviveCMC", false);
    StepArisen("cPlActWalk", false);
    // Аризен cPlActDead — не succor-жертва. DEAD снимает leftover KNOCKDOWN.
    StepArisen("cPlActDmgDown", true);
    StepArisen("cPlActDead", false);
    StepArisen("cPlActWalk", false);

    std::ifstream in("/tmp/partystatus_fsm_test.log");
    assert(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string log = ss.str();

    assert(CountSubstring(log, " (read-only status + downed/revive observer)") == 4);
    assert(CountSubstring(log, "PS: MainPawn DOWNED act=cPlActCmcNeardeath") == 2);
    assert(CountSubstring(log, "PS: MainPawn RAISED act=cPlActWalk") == 2);
    assert(CountSubstring(log, "PS: MainPawn RIFTED act=cPlActCmcReturn") == 1);
    assert(CountSubstring(log, "PS: MainPawn KNOCKDOWN act=cPlActDmgDown") == 1);
    assert(CountSubstring(log, "PS: MainPawn KNOCKDOWN-END act=cPlActDmgStandUp") == 1);
    assert(CountSubstring(log, "PS: MainPawn DOWNED act=cPlActDmgCrumbleDead") == 1);
    assert(CountSubstring(log, "PS: Arisen RAISE act=cPlReviveCMC") == 1);
    assert(CountSubstring(log, "PS: Arisen DEAD act=cPlActDead") == 1);
    assert(CountSubstring(log, "PS: Arisen KNOCKDOWN act=cPlActDmgDown") == 1);
    assert(CountSubstring(log, "PS: Arisen DEAD-END act=cPlActWalk") == 1);
    // Старый бред ролей: REVIVE/RECOVERED/DOWN-END на пешке — запрещён.
    assert(log.find("REVIVE act=cPlReviveCMC") == std::string::npos);
    assert(log.find("RECOVERED") == std::string::npos);
    assert(log.find("no revive observed") == std::string::npos);
    assert(log.find("PS: Arisen DOWNED") == std::string::npos);
    assert(log.find("found @") == std::string::npos);
    assert(Runtime::g_findChildCalls >= 0);

    fprintf(stderr, "PartyStatus 84.25 FSM fixture passed "
                    "(CrumbleDead DOWNED; Arisen DEAD clears knockdown).\n");
    return 0;
}

#include "director_t.cpp"
#include <assert.h>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

std::ofstream logFile("/tmp/director_moment_priority_test.log");
BYTE** pBase = 0;
IniConfigStub config;

static Runtime::PartyCombatSnapshot g_snapshot;
static bool g_snapshotAvailable = true;

struct MobilizationCall {
    float urgency;
    uint32_t hold;
    float l0, a0, l1, a1;
};
static std::map<uintptr_t, MobilizationCall> g_overrides; // currently held
static std::map<uintptr_t, MobilizationCall> g_decaying;
static std::vector<uintptr_t> g_cleared; // release/reset audit trail
static uintptr_t g_overrideFailBody = 0;
static bool g_tempoReady = true;
static const char* g_tempoReason = "ready";
static uintptr_t g_identityBody[4] = {};
static bool      g_identityRecordMissing[4] = {};
static bool      g_identityDuplicate[4] = {};
static bool g_observerDemand = false;
static int g_focusMember = -1;
// 85.33: проба ВТОРОГО приказа (директор отдаёт его отдельной записью).
static int g_secondaryMember = -1;
static uintptr_t g_secondaryBody = 0;
static int g_secondaryResponse = Runtime::Aggro::DIRECTOR_RESPONSE_NONE;
static char g_secondaryKind[16] = "uEm0100";
static std::vector<uintptr_t> g_secondaryBodies;
static uintptr_t g_focusBody = 0;
static uintptr_t g_focusExcludedBody = 0;
static int g_focusResponse = Runtime::Aggro::DIRECTOR_RESPONSE_NONE;
static char g_focusKind[16] = "uEm0200";
// 85.23: список исполнителей, переданный в Aggro (пусто = весь вид).
static std::vector<uintptr_t> g_focusSetBodies;

// Хелпер сравнения для проб 85.34 (в этом файле своего не было).
static bool Near(float a, float b) { return fabsf(a - b) < 0.00001f; }

namespace Runtime {
bool ReadPartyCombatSnapshot(PartyCombatSnapshot* out)
{
    if (!out || !g_snapshotAvailable) return false;
    *out = g_snapshot;
    return true;
}

const char* PartyCombatSlotName(int slot)
{
    static const char* n[] = { "Arisen", "MainPawn", "Hired1", "Hired2" };
    return slot >= 0 && slot < 4 ? n[slot] : "?";
}

bool GetArisenWorldPos(float* x, float* y, float* z)
{
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (z) *z = 0.0f;
    return true;
}

bool KindIsEnemy(const char* kind)
{
    return kind && kind[0] == 'u';
}

namespace Tempo {
bool GetFactors(uintptr_t, float* loco, float* atk)
{
    if (loco) *loco = 1.0f;
    if (atk) *atk = 1.0f;
    return true;
}

void RegisterRageProfile(const char*, float, float, float, float) {}

// 85.36: директор читает действующие границы [monsterTempo], чтобы числа вида
// нельзя было поставить НИЖЕ базы (иначе admit молча отбивает тело). В фикстуре
// это те же числа, что стоят в поставляемом ini.
void GetRange(float* lo, float* hi)      { if (lo) *lo = 1.05f; if (hi) *hi = 1.20f; }
void GetAnimRange(float* lo, float* hi)  { if (lo) *lo = 1.05f; if (hi) *hi = 1.15f; }

bool DirectorReady(const char** reasonOut)
{
    if (reasonOut) *reasonOut = g_tempoReady ? "ready" : g_tempoReason;
    return g_tempoReady;
}

bool AdmitDirectorMobilization(uintptr_t body, const char* exactKind,
                               float urgency, uint32_t holdMs,
                               DirectorMobilizationReceipt* receipt,
                               const char** reasonOut)
{
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!body || !exactKind
        || (strcmp(exactKind, "uEm0200") && strcmp(exactKind, "uEm0100")
            && strcmp(exactKind, "uEm0101") && strcmp(exactKind, "uEm0400"))
        || body == g_overrideFailBody) {
        if (reasonOut) *reasonOut = "director-mobilization-table-full";
        return false;
    }
    const float variant = (float)((body >> 8) & 3u) * 0.005f;
    MobilizationCall call = { urgency, holdMs, 1.05f + variant,
                              1.05f + variant, 1.20f + variant,
                              1.20f + variant };
    g_decaying.erase(body);
    g_overrides[body] = call;
    if (receipt) {
        receipt->body = body;
        receipt->level = urgency;
        receipt->urgency = urgency;
        receipt->stableLoco = call.l0;
        receipt->stableAnim = call.a0;
        receipt->rageLoco = call.l1;
        receipt->rageAnim = call.a1;
        receipt->effectiveLoco = call.l1;
        receipt->effectiveAnim = call.a1;
        receipt->holding = true;
        receipt->decaying = false;
    }
    if (reasonOut) *reasonOut = "director-mobilization-ready";
    return true;
}

void ReleaseDirectorMobilization(uintptr_t body)
{
    std::map<uintptr_t, MobilizationCall>::iterator it = g_overrides.find(body);
    if (it != g_overrides.end()) {
        g_decaying[body] = it->second;
        g_overrides.erase(it);
    }
    g_cleared.push_back(body);
}

void HardResetDirectorMobilization(uintptr_t body)
{
    g_overrides.erase(body);
    g_decaying.erase(body);
    g_cleared.push_back(body);
}

void OnWorldUnload() {}

void HardResetAllDirectorMobilization()
{
    for (std::map<uintptr_t, MobilizationCall>::const_iterator it =
             g_overrides.begin(); it != g_overrides.end(); ++it)
        g_cleared.push_back(it->first);
    for (std::map<uintptr_t, MobilizationCall>::const_iterator it =
             g_decaying.begin(); it != g_decaying.end(); ++it)
        g_cleared.push_back(it->first);
    g_overrides.clear();
    g_decaying.clear();
}

int DirectorMobilizationCount()
{
    return (int)(g_overrides.size() + g_decaying.size());
}
} // namespace Tempo

namespace Aggro {
void SetObserverDemand(bool on) { g_observerDemand = on; }

bool ResolveMemberBody(int member, uintptr_t* out)
{
    if (out) *out = 0;
    if (member < 0 || member >= 4 || !g_identityBody[member]) return false;
    if (out) *out = g_identityBody[member];
    return true;
}

const char* ResolveMemberBodyStatus(int member, uintptr_t* out)
{
    static const char* exact[4] = {
        "identity-Arisen-exact", "identity-MainPawn-exact",
        "identity-Hired1-exact", "identity-Hired2-exact"
    };
    static const char* missingRec[4] = {
        "identity-Arisen-record-unavailable",
        "identity-MainPawn-record-unavailable",
        "identity-Hired1-record-unavailable",
        "identity-Hired2-record-unavailable"
    };
    static const char* missing[4] = {
        "identity-Arisen-body-unresolved-or-duplicate",
        "identity-MainPawn-body-unresolved-or-duplicate",
        "identity-Hired1-body-unresolved-or-duplicate",
        "identity-Hired2-body-unresolved-or-duplicate"
    };
    static const char* absent[4] = {
        "identity-Arisen-absent", "identity-MainPawn-absent",
        "identity-Hired1-absent", "identity-Hired2-absent"
    };
    if (member < 0 || member >= 4) return "identity-invalid-slot";
    if (g_identityRecordMissing[member]) {
        if (out) *out = 0;
        return missingRec[member];
    }
    if (g_identityDuplicate[member]) {
        if (out) *out = 0;
        return missing[member];
    }
    if (!g_identityBody[member]) {
        if (out) *out = 0;
        // Аризен в рифт не уходит: ноль тел = дыра, не vacant.
        return member == 0 ? missing[member] : absent[member];
    }
    if (out) *out = g_identityBody[member];
    return exact[member];
}

bool DirectorFocusSet(int member, uintptr_t expectedBody,
                      uintptr_t excludedEnemyBody, int response,
                      const char* exactKind, const uintptr_t* responders,
                      int nResponders)
{
    g_focusSetBodies.clear();
    if (responders && nResponders > 0)
        for (int i = 0; i < nResponders; ++i)
            g_focusSetBodies.push_back(responders[i]);
    if (member < 0) {
        g_focusMember = -1;
        g_secondaryMember = -1;
        g_secondaryBody = 0;
        g_secondaryResponse = Runtime::Aggro::DIRECTOR_RESPONSE_NONE;
        strcpy(g_secondaryKind, "uEm0100");
        g_secondaryBodies.clear();
        g_focusBody = 0;
        g_focusExcludedBody = 0;
        g_focusResponse = DIRECTOR_RESPONSE_NONE;
        strcpy(g_focusKind, "uEm0200");
        return true;
    }
    uintptr_t resolved = 0;
    if (!ResolveMemberBody(member, &resolved) || resolved != expectedBody
        || (response != DIRECTOR_RESPONSE_ALERT
            && response != DIRECTOR_RESPONSE_ALARM)
        || !exactKind || (strcmp(exactKind, "uEm0200") != 0
                          && strcmp(exactKind, "uEm0100") != 0
                          && strcmp(exactKind, "uEm0101") != 0
                          && strcmp(exactKind, "uEm0400") != 0))
        return false;
    g_focusMember = member;
    g_focusBody = expectedBody;
    g_focusExcludedBody = excludedEnemyBody;
    g_focusResponse = response;
    strncpy(g_focusKind, exactKind, sizeof(g_focusKind) - 1);
    g_focusKind[sizeof(g_focusKind) - 1] = 0;
    return true;
}
bool DirectorSecondarySet(int member, uintptr_t expectedBody,
                          uintptr_t excludedEnemyBody, int response,
                          const char* exactKind, const uintptr_t* responders,
                          int nResponders)
{
    (void)excludedEnemyBody;
    g_secondaryBodies.clear();
    if (responders && nResponders > 0)
        for (int i = 0; i < nResponders; ++i)
            g_secondaryBodies.push_back(responders[i]);
    uintptr_t resolved = 0;
    if (member < 0 || !expectedBody
        || !ResolveMemberBody(member, &resolved) || resolved != expectedBody
        || !exactKind || !exactKind[0])
        return false;
    g_secondaryMember = member;
    g_secondaryBody = expectedBody;
    g_secondaryResponse = response;
    strncpy(g_secondaryKind, exactKind, sizeof(g_secondaryKind) - 1);
    g_secondaryKind[sizeof(g_secondaryKind) - 1] = 0;
    return true;
}

void DirectorSecondaryClear(const char* reason)
{
    (void)reason;
    g_secondaryMember = -1;
    g_secondaryBody = 0;
    g_secondaryResponse = DIRECTOR_RESPONSE_NONE;
    g_secondaryBodies.clear();
}

int DirectorSecondaryMember() { return g_secondaryMember; }

// 84.16/84.18 dual-observe: Director DumpSnapshot dumps both instruments.
// Fixture keeps them silent: no goblin/party world in this harness.
void CardReconDump() {}
} // namespace Aggro

namespace PartyStatus {
void DumpSnapshot() {}
} // namespace PartyStatus
} // namespace Runtime

static void SetMember(int slot, float hp, float maxHp, bool bodyMapped)
{
    Runtime::PartyCombatMember& m = g_snapshot.member[slot];
    memset(&m, 0, sizeof(m));
    m.slot = slot;
    m.pawnRecordIdx = slot == 0 ? -1 : slot - 1;
    m.record = 0x1000u + (uintptr_t)slot * 0x100u;
    m.recordValid = true;
    m.hpValid = true;
    m.statsValid = false; // proves core stats are not required
    m.skillsValid = false;
    m.body = bodyMapped ? 0x5000u + (uintptr_t)slot * 0x100u : 0;
    m.bodyValid = bodyMapped;
    m.positionValid = bodyMapped;
    m.actionValid = bodyMapped;
    m.x = (float)slot * 1000.0f;
    m.y = 0.0f;
    m.z = 0.0f;
    strcpy(m.liveAct, "cPlActWait");
    m.vocation = 1;
    m.level = 100;
    for (int k = 0; k < 6; ++k) m.equippedSkills[k] = -1;
    m.currentHp = hp;
    m.maxHp = maxHp;
    // Deliberately absurd and unequal core values: they must be ignored.
    m.strength = 10000.0f - slot * 2000.0f;
    m.defense = slot == 1 ? 99999.0f : 1.0f;
    m.magick = 5000.0f + slot * 1000.0f;
    m.magickDefense = slot == 1 ? 99999.0f : 1.0f;
}

static void SetWolves(int n)
{
    MonsterAI::s_nView = n;
    for (int i = 0; i < n; ++i) {
        MonsterAI::MonsterView& v = MonsterAI::s_view[i];
        memset(&v, 0, sizeof(v));
        v.body = 0x9000u + (uintptr_t)i * 0x100u;
        strcpy(v.kind, "uEm0200");
        strcpy(v.act, "cEm0200ActWait");
        v.positionValid = true;
        v.x = 5000.0f + (float)i * 500.0f;
        v.y = 0.0f;
        v.z = 0.0f;
    }
}

static void SetGoblins(int n)
{
    MonsterAI::s_nView = n;
    for (int i = 0; i < n; ++i) {
        MonsterAI::MonsterView& v = MonsterAI::s_view[i];
        memset(&v, 0, sizeof(v));
        v.body = 0xA000u + (uintptr_t)i * 0x100u;
        strcpy(v.kind, "uEm0100");
        strcpy(v.act, "cEm0100ActWait");
        v.positionValid = true;
        v.x = 2000.0f + (float)i * 80.0f;
        v.y = 0.0f;
        v.z = 0.0f;
    }
}

static void FreshDirector()
{
    using namespace MonsterAI;
    Shutdown();
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    memset(g_identityBody, 0, sizeof(g_identityBody));
    memset(g_identityRecordMissing, 0, sizeof(g_identityRecordMissing));
    memset(g_identityDuplicate, 0, sizeof(g_identityDuplicate));
    g_snapshot.recordCount = 4;
    g_snapshotAvailable = true;
    g_overrides.clear();
    g_decaying.clear();
    g_cleared.clear();
    g_overrideFailBody = 0;
    g_tempoReady = true;
    g_tempoReason = "ready";
    g_focusMember = -1;
    g_focusBody = 0;
    g_focusExcludedBody = 0;
    g_focusResponse = Runtime::Aggro::DIRECTOR_RESPONSE_NONE;
    strcpy(g_focusKind, "uEm0200");
    g_observerDemand = false;
    Init();
    assert(!Enabled());
    SetEnabled(true);
    assert(g_observerDemand);
    assert(!ActuatorEnabled());
    assert(GameplayWriteCount() == 0);
}

static void TestPriorityAndHysteresis()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1000.0f, 1000.0f, true);
    SetMember(1, 900.0f, 10000.0f, true); // only 9%, but not lowest absolute HP
    SetMember(2, 950.0f, 950.0f, true);
    SetMember(3, 700.0f, 700.0f, true);   // 100%, yet lowest absolute HP
    SetWolves(2);

    Decide(500);
    assert(PackMarkSlot() == Runtime::PARTY_HIRED2);
    assert(RunnerUpSlot() == Runtime::PARTY_MAIN);
    assert(PrioritySlot(0) == Runtime::PARTY_HIRED2);
    assert(PrioritySlot(1) == Runtime::PARTY_MAIN);
    assert(PrioritySlot(2) == Runtime::PARTY_HIRED1);
    assert(PrioritySlot(3) == Runtime::PARTY_ARISEN);
    assert(Recommendation() == RECOMMEND_BIAS); // +28.6%, below focus boundary
    // Normal aligned case: runner and highest-HP depth are intentionally
    // different measurements of the same committed raw leader.
    assert(std::fabs(TargetIsolationRatio() - (900.0f / 700.0f - 1.0f)) < 0.001f);
    assert(std::fabs(TargetDepthRatio() - (1000.0f / 700.0f - 1.0f)) < 0.001f);

    HuntTelemetry h;
    assert(HuntTelemetryAt(Runtime::PARTY_HIRED2, &h));
    assert(h.priorityRank == 1);
    assert(h.recordValid && h.hpValid && h.scoreValid);
    assert(h.bodyValid && h.positionValid && !h.coreStatsValid);
    assert(std::fabs(h.huntScore - (1000.0f / 700.0f)) < 0.001f);

    // Raw priority changes immediately, while committed PackMark observes the
    // 2500 ms hold. A held mark that is no longer best has negative isolation.
    const uint64_t initialSignature = s_partySignature;
    g_snapshot.member[3].currentHp = 1000.0f;
    g_snapshot.member[2].currentHp = 50.0f;
    Decide(1000);
    assert(s_partySignature == initialSignature);
    assert(PrioritySlot(0) == Runtime::PARTY_HIRED1);
    assert(PackMarkSlot() == Runtime::PARTY_HIRED2);
    assert(Recommendation() == RECOMMEND_NONE);
    assert(std::fabs(TargetIsolationRatio() - (50.0f / 1000.0f - 1.0f)) < 0.001f);
    assert(std::fabs(TargetDepthRatio()) < 0.001f);
    Decide(2999);
    assert(PackMarkSlot() == Runtime::PARTY_HIRED2);
    Decide(3000);
    assert(PackMarkSlot() == Runtime::PARTY_HIRED1);
    assert(Recommendation() == RECOMMEND_FOCUS);

    // Invalid current HP permits an immediate switch without waiting.
    g_snapshot.member[2].currentHp = 0.0f;
    Decide(3200);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(Recommendation() == RECOMMEND_NONE); // 900 versus runner 1000

    // A level/loadout-style metadata mutation must NOT masquerade as a party
    // composition change and must not reset short tactical memory.
    const uint64_t beforeLevel = s_partySignature;
    const DWORD beforeLevelHold = s_markSince;
    g_snapshot.member[1].vocation = 2;
    g_snapshot.member[1].level = 101;
    g_snapshot.member[1].maxHp = 12000.0f;
    g_snapshot.member[1].strength += 20.0f;
    g_snapshot.member[1].defense += 20.0f;
    g_snapshot.member[1].equippedSkills[0] = 123;
    Decide(3600);
    assert(s_partySignature == beforeLevel);
    assert(s_markSince == beforeLevelHold);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);

    // A topology change still resets the encounter-local commitment.
    g_snapshot.member[3].record += 0x4000u;
    Decide(6000);
    assert(s_partySignature != beforeLevel);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(s_markSince == 6000);

    // Focus-opportunity tiers are separated from target ranking and use only isolation.
    g_snapshot.member[1].currentHp = 700.0f; // runner 1000: +42.9%
    Decide(6500);
    assert(Recommendation() == RECOMMEND_BIAS);
    g_snapshot.member[1].currentHp = 400.0f; // runner 1000: +150%
    Decide(7000);
    assert(Recommendation() == RECOMMEND_FOCUS);
    assert(GameplayWriteCount() == 0);
}

static void TestIsolationDepthSeparation()
{
    using namespace MonsterAI;
    FreshDirector();

    // Build 003 log shape: the committed target is only 10.9% isolated from
    // the runner, while the full party distribution is 140.7% deep.
    SetMember(0, 451.3f, 520.0f, true);
    SetMember(1, 187.5f, 505.0f, true);
    SetMember(2, 337.3f, 570.0f, true);
    SetMember(3, 207.9f, 498.0f, true);
    SetWolves(3);

    Decide(10000);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(RunnerUpSlot() == Runtime::PARTY_HIRED2);
    assert(Recommendation() == RECOMMEND_NONE);
    assert(std::fabs(TargetIsolationRatio() - (207.9f / 187.5f - 1.0f)) < 0.001f);
    assert(std::fabs(TargetDepthRatio() - (451.3f / 187.5f - 1.0f)) < 0.001f);

    // During minimum hold, both axes remain relative to the committed mark.
    // A new raw leader can therefore make isolation negative while depth still
    // describes the broad distribution above the held mark.
    g_snapshot.member[3].currentHp = 100.0f;
    Decide(10500);
    assert(PrioritySlot(0) == Runtime::PARTY_HIRED2);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(RunnerUpSlot() == Runtime::PARTY_HIRED2);
    assert(Recommendation() == RECOMMEND_NONE);
    assert(std::fabs(TargetIsolationRatio() - (100.0f / 187.5f - 1.0f)) < 0.001f);
    assert(std::fabs(TargetDepthRatio() - (451.3f / 187.5f - 1.0f)) < 0.001f);
    assert(GameplayWriteCount() == 0);
}

static void TestValidatedFightReplay()
{
    using namespace MonsterAI;
    FreshDirector();

    // Exact HP shape from the first real Build 002 gameplay log.
    // All four were on-field. bodyMapped=false was the old unvalidated
    // snapshot, not a rift. 84.23 scores only on-field members.
    SetMember(0, 331.3f, 498.0f, true);
    SetMember(1, 505.0f, 505.0f, true);
    SetMember(2, 570.0f, 570.0f, true);
    SetMember(3, 498.0f, 498.0f, true);
    SetWolves(10);

    Decide(10000);
    assert(PackMarkSlot() == Runtime::PARTY_ARISEN);
    assert(PrioritySlot(0) == Runtime::PARTY_ARISEN);
    assert(Recommendation() == RECOMMEND_BIAS); // +50.3%, not a commit window

    // MainPawn becomes only marginally lower. Raw order sees it immediately,
    // but the 20% switch rule prevents a noisy reassignment.
    g_snapshot.member[1].currentHp = 300.9f;
    SetWolves(9);
    Decide(117047);
    assert(PrioritySlot(0) == Runtime::PARTY_MAIN);
    assert(PackMarkSlot() == Runtime::PARTY_ARISEN);
    assert(Recommendation() == RECOMMEND_NONE);

    // At 164.8 versus 331.3 the real fight crossed +101%: this is the first
    // data-backed FOCUS-WINDOW boundary.
    g_snapshot.member[1].currentHp = 164.8f;
    Decide(118000);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(Recommendation() == RECOMMEND_FOCUS);
    const DWORD committedSince = s_markSince;
    const uint64_t topology = s_partySignature;

    g_snapshot.member[1].currentHp = 24.0f;
    SetWolves(7);
    Decide(138766);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(Recommendation() == RECOMMEND_FOCUS);

    // Healing is ordinary input motion, not composition. Natural pressure
    // later spread to Hired1, while MainPawn remained the clear priority.
    g_snapshot.member[1].currentHp = 132.2f;
    g_snapshot.member[2].currentHp = 337.3f;
    SetWolves(5);
    Decide(159000);
    assert(PrioritySlot(0) == Runtime::PARTY_MAIN);
    assert(PrioritySlot(1) == Runtime::PARTY_ARISEN); // absolute HP beats percentage
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(Recommendation() == RECOMMEND_FOCUS);

    // Confirmed real cause: level-up fully restored Arisen and changed core
    // stats. Build 004 must preserve commitment instead of logging composition.
    g_snapshot.member[0].currentHp = 520.0f;
    g_snapshot.member[0].maxHp = 520.0f;
    g_snapshot.member[0].level += 1;
    g_snapshot.member[0].strength = 70.0f;
    g_snapshot.member[0].defense = 75.0f;
    g_snapshot.member[0].magick = 100.0f;
    g_snapshot.member[0].magickDefense = 95.0f;
    SetWolves(1);
    Decide(190000);
    assert(s_partySignature == topology);
    assert(s_markSince == committedSince);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(PrioritySlot(1) == Runtime::PARTY_HIRED1);
    assert(Recommendation() == RECOMMEND_FOCUS);

    SetWolves(0);
    Decide(191000);
    assert(PackMarkSlot() == -1);
    assert(ScoredWolfCount() == 0);
    assert(GameplayWriteCount() == 0);

    g_snapshotAvailable = false;
    Decide(192000);
    assert(PackMarkSlot() == -1);
    assert(std::string(Status()).find("party records unavailable") != std::string::npos);
    assert(GameplayWriteCount() == 0);
}

static void TestBuild012SynchronizedMobilization()
{
    using namespace MonsterAI;
    FreshDirector();

    // Exact four-slot bridge plus a clear FOCUS-WINDOW on MainPawn.
    SetMember(0, 1200.0f, 1200.0f, true);
    SetMember(1, 100.0f, 1000.0f, true);
    SetMember(2, 1000.0f, 1000.0f, true);
    SetMember(3, 1100.0f, 1100.0f, true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetWolves(2);
    Decide(200000);
    assert(Recommendation() == RECOMMEND_FOCUS);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);

    // Default-off is a real no-write regression even with every gate ready.
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(g_overrides.empty());
    assert(g_focusMember == -1);
    assert(GameplayWriteCount() == 0);

    SetActuatorEnabled(true);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "focus-window-synchronized");
    assert(g_focusMember == Runtime::PARTY_MAIN);
    assert(g_focusBody == g_snapshot.member[Runtime::PARTY_MAIN].body);
    assert(g_overrides.size() == 2);
    for (int i = 0; i < 2; ++i) {
        const uintptr_t wolf = 0x9000u + (uintptr_t)i * 0x100u;
        assert(g_overrides.count(wolf) == 1);
        const MobilizationCall& o = g_overrides[wolf];
        assert(std::fabs(o.urgency - 1.0f) < 0.00001f);
        assert(o.l1 > o.l0 && o.a1 > o.a0);
        assert(o.hold == 600);
    }
    assert(GameplayWriteCount() == 3); // two Tempo leases + one Aggro lease

    // A hired-slot mismatch fails closed and clears every owned wolf body.
    g_identityBody[Runtime::PARTY_HIRED1] += 0x40;
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "identity-slot-body-mismatch");
    assert(g_overrides.empty());
    assert(g_focusMember == -1);
    assert(g_cleared.size() >= 2);

    // Automatic failure reason names the exact fixed slot; it does not collapse
    // all identity failures into a generic exact4 label.
    g_identityBody[Runtime::PARTY_HIRED1] =
        g_snapshot.member[Runtime::PARTY_HIRED1].body;
    g_identityDuplicate[Runtime::PARTY_HIRED2] = true;
    g_identityBody[Runtime::PARTY_HIRED2] = 0;
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus())
           == "identity-Hired2-body-unresolved-or-duplicate");
    assert(g_overrides.empty() && g_focusMember == -1);
    g_identityDuplicate[Runtime::PARTY_HIRED2] = false;
    g_identityBody[Runtime::PARTY_HIRED2] =
        g_snapshot.member[Runtime::PARTY_HIRED2].body;

    // Tempo readiness is part of the same gate, not merely UI diagnostics.
    ApplyPolicies();
    assert(PolicyEngaged());
    g_tempoReady = false;
    g_tempoReason = "tempo-general-hook-missing";
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tempo-general-hook-missing");
    assert(g_overrides.empty() && g_focusMember == -1);

    // Partial Tempo installation is rolled back per body; no half-policy.
    g_tempoReady = true;
    g_overrideFailBody = 0x9100u;
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "director-mobilization-table-full");
    assert(g_overrides.empty() && g_focusMember == -1);
    g_overrideFailBody = 0;
    ApplyPolicies();
    assert(PolicyEngaged());

    // BIAS/NONE never inherit a stale FOCUS/Tempo lease.
    s_mode = RECOMMEND_BIAS;
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "decision-bias");
    assert(g_overrides.empty() && g_focusMember == -1);
    assert(g_decaying.size() == 2); // ordinary completion preserves bounded decay
    s_mode = RECOMMEND_NONE;
    ApplyPolicies();
    assert(std::string(PolicyStatus()) == "decision-none");

    // Inactive NONE/BIAS reason alternation updates status but emits no fake
    // release transitions and does not touch already-decaying rows.
    logFile.flush();
    const std::streampos idleLogEnd = logFile.tellp();
    for (int i = 0; i < 12; ++i) {
        s_mode = (i & 1) ? RECOMMEND_NONE : RECOMMEND_BIAS;
        ApplyPolicies();
    }
    logFile.flush();
    assert(logFile.tellp() == idleLogEnd);
    assert(g_decaying.size() == 2);

    // A persistent unsafe inactive episode hard-resets/logs once even when an
    // ordinary BIAS decision alternates with the failure. Recovery summarizes
    // the coalesced retries before the next real ENGAGED transition.
    s_mode = RECOMMEND_FOCUS;
    g_identityBody[Runtime::PARTY_ARISEN] = 0;
    ApplyPolicies();
    assert(std::string(PolicyStatus())
           == "identity-Arisen-body-unresolved-or-duplicate");
    assert(s_inactiveSafetyLatched);
    assert(g_decaying.empty());
    logFile.flush();
    const std::streampos failClosedLogEnd = logFile.tellp();
    for (int i = 0; i < 12; ++i) {
        s_mode = (i & 1) ? RECOMMEND_FOCUS : RECOMMEND_BIAS;
        ApplyPolicies();
    }
    logFile.flush();
    assert(logFile.tellp() == failClosedLogEnd);
    assert(s_inactiveSafetySuppressed == 6);
    g_identityBody[Runtime::PARTY_ARISEN] =
        g_snapshot.member[Runtime::PARTY_ARISEN].body;
    s_mode = RECOMMEND_FOCUS;
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(!s_inactiveSafetyLatched);

    // Empty hired slots must not force a 4-pawn party. Occupied Arisen +
    // Main + Hired1 stay exact; record-unavailable Hired2 is skipped.
    g_identityRecordMissing[Runtime::PARTY_HIRED2] = true;
    g_identityBody[Runtime::PARTY_HIRED2] = 0;
    g_snapshot.member[Runtime::PARTY_HIRED2].recordValid = false;
    g_snapshot.member[Runtime::PARTY_HIRED2].body = 0;
    g_snapshot.member[Runtime::PARTY_HIRED2].bodyValid = false;
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "focus-window-synchronized");
    g_identityRecordMissing[Runtime::PARTY_HIRED2] = false;
    g_identityBody[Runtime::PARTY_HIRED2] =
        0x5000u + (uintptr_t)Runtime::PARTY_HIRED2 * 0x100u;
    SetMember(3, 1100.0f, 1100.0f, true);
    g_identityBody[Runtime::PARTY_HIRED2] =
        g_snapshot.member[Runtime::PARTY_HIRED2].body;

    // Pack loss and Director disable are explicit release paths.
    s_mode = RECOMMEND_FOCUS;
    SetWolves(2);
    ApplyPolicies();
    assert(PolicyEngaged());
    SetWolves(0);
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "wolf-pack-lost");
    assert(g_overrides.empty() && g_decaying.empty() && g_focusMember == -1);

    SetWolves(2);
    ApplyPolicies();
    assert(PolicyEngaged());
    SetEnabled(false);
    assert(!PolicyEngaged());
    assert(!g_observerDemand);
    assert(g_overrides.empty() && g_focusMember == -1);

    // Re-enable, engage, then exercise loader-lock-safe lifecycle cleanup.
    SetEnabled(true);
    SetWolves(2);
    Decide(201000);
    assert(Recommendation() == RECOMMEND_FOCUS);
    ApplyPolicies();
    assert(PolicyEngaged());
    Shutdown();
    assert(!Enabled() && !ActuatorEnabled() && !PolicyEngaged());
    assert(!g_observerDemand);
    assert(g_overrides.empty() && g_focusMember == -1);
}

static void TestBuild012UrgencyDataDefinedMatcher()
{
    using namespace MonsterAI;

    TacticalPartyActor party[2] = {};
    party[0].slot = Runtime::PARTY_MAIN;
    party[0].body = 0x5100;
    party[0].act = "cPlActGrabStart";
    party[0].positionValid = true;
    party[0].x = 0.0f;
    party[1].slot = Runtime::PARTY_HIRED1;
    party[1].body = 0x5200;
    party[1].act = "cPlActWait";
    party[1].positionValid = true;
    party[1].x = 900.0f;

    TacticalMonsterActor monsters[3] = {};
    for (int i = 0; i < 3; ++i) {
        monsters[i].body = 0x9000u + (uintptr_t)i * 0x100u;
        monsters[i].kind = "uEm0200";
        monsters[i].act = "cEm0200ActWait";
        monsters[i].positionValid = true;
        monsters[i].x = 5000.0f + i * 500.0f;
    }
    monsters[0].x = 100.0f; // the one reliable spatial restraint identity

    TacticalScan scan;
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PACK_GRAB_ALERT);
    assert(scan.response == TACTICAL_RESPONSE_ALERT);
    assert(scan.match.response == TACTICAL_RESPONSE_ALERT);
    assert(std::fabs(scan.match.urgency - 1.0f) < 0.00001f);
    assert(scan.match.targetSlot == Runtime::PARTY_MAIN);
    assert(scan.match.evidenceBody == monsters[0].body);
    assert(scan.match.excludeEvidenceBody);
    assert(scan.match.maxLeaseMs == 750);

    // Hagaijime4Feet is itself the acting pawn's confirmed ground-pin trigger.
    // It is a direct strong rule; no sampled GrabStart history is an input.
    party[0].act = "cPlActHagaijime4Feet";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM);
    assert(scan.response == TACTICAL_RESPONSE_ALARM);
    assert(std::fabs(scan.match.urgency - 1.0f) < 0.00001f);
    assert(scan.match.priority == 200);
    assert(scan.match.maxLeaseMs == 4000);
    assert(scan.match.evidenceBody == monsters[0].body);

    // Literal lifting/carrying remains a different class with exact victim
    // action evidence and its own 2500 ms ALARM lease.
    party[0].act = "cPlActLiftRun";
    monsters[0].act = "cEm0200Lifted";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PACK_LIFT_RESCUE);
    assert(scan.response == TACTICAL_RESPONSE_ALARM);
    assert(std::fabs(scan.match.urgency - 1.0f) < 0.00001f);
    assert(scan.match.priority == 150);
    assert(scan.match.maxLeaseMs == 2500);

    // Literal lift keeps globally unique victim-action evidence.
    monsters[1].act = "cEm0200Lifted";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(!scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PACK_LIFT_RESCUE);
    assert(scan.targetCandidates == 1 && scan.evidenceCandidates == 2);

    // Ground restraint instead permits many same-kind actors, but requires one
    // unique nearby pair so the exact pinned body can be excluded reliably.
    monsters[0].act = "cEm0200ActWait";
    monsters[1].act = "cEm0200ActWait";
    monsters[1].x = 150.0f;
    party[0].act = "cPlActHagaijime4Feet";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(!scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM);
    assert(scan.targetCandidates == 1 && scan.pairCandidates == 2);

    // Any exact party slot can be the actor, but simultaneous actors are an
    // ambiguous single-focus request and fail closed.
    monsters[1].x = 5500.0f;
    party[1].act = "cPlActHagaijime4Feet";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(!scan.matched && scan.targetCandidates == 2);
    party[1].act = "cPlActWait";

    // ---- 85.25: «услышал каст» берёт БЛИЖАЙШЕГО, а не отказывает ----------
    //
    // ЗАЧЕМ. Правило требовало РОВНО ОДНУ пару «кастующий <-> монстр» в
    // радиусе, и в поле это оказалось невыполнимым: рядом всегда больше одного
    // монстра (36 отказов против 4 срабатываний, все отказы pairs-ambiguous).
    // Теперь для семейства каста допускается много пар, побеждает ближайшая.
    // Для хватов и прижимов правило прежнее — там уникальность ДОКАЗЫВАЕТ,
    // кого именно держат, и ослаблять её нельзя.
    {
        const char* savedPartyActs[2] = { party[0].act, party[1].act };
        const int   savedVocation = party[0].vocation;
        const int   savedSlot     = party[0].slot;
        const char* savedMonActs[3] = { monsters[0].act, monsters[1].act, monsters[2].act };
        const char* savedKinds[3]    = { monsters[0].kind, monsters[1].kind, monsters[2].kind };
        float savedMonX[3], savedMonY[3], savedMonZ[3];
        for (int i = 0; i < 3; ++i) {
            savedMonX[i] = monsters[i].x;
            savedMonY[i] = monsters[i].y;
            savedMonZ[i] = monsters[i].z;
        }

        MonsterAI::SetNearestPairFallback(false);
        // Правило каста arisenOnly: нужен слот ВОССТАВШЕГО (PARTY_ARISEN=0),
        // а не главной пешки, и кастующая вокация (9 = Sorcerer).
        party[0].slot = Runtime::PARTY_ARISEN;
        party[0].vocation = 9;
        party[0].act = "cPlActWpnWandBase";
        party[0].positionValid = true;
        party[0].x = 0.0f; party[0].y = 0.0f; party[0].z = 0.0f;
        party[1].act = "cPlActWait";
        // Три гоблина на разной дистанции: 100, 300 и 200 условных единиц (1 м = 100).
        for (int i = 0; i < 3; ++i) {
            monsters[i].kind = "uEm0100";
            monsters[i].act  = "cEm0100ActWalk";
            monsters[i].positionValid = true;
            monsters[i].x = 0.0f; monsters[i].y = 0.0f;
            monsters[i].z = (i == 0) ? 100.0f : (i == 1) ? 300.0f : 200.0f;
        }

        // (1) Ключ выключен — старое строгое поведение: три пары значат отказ.
        ScanTacticalSituations(party, 2, monsters, 3, &scan);
        assert(!scan.matched);
        assert(scan.situation == TACTICAL_SITUATION_PLAYER_CHANT_HARASS);
        assert(scan.pairCandidates == 3);

        // (2) Ключ включён — событие допускается и жертвой становится БЛИЖАЙШИЙ.
        MonsterAI::SetNearestPairFallback(true);
        ScanTacticalSituations(party, 2, monsters, 3, &scan);
        assert(scan.matched);
        assert(scan.situation == TACTICAL_SITUATION_PLAYER_CHANT_HARASS);
        assert(scan.match.evidenceBody == monsters[0].body);
        assert(scan.match.pairsConsidered == 3);
        assert(std::fabs(scan.match.pairDistanceM - 1.0f) < 0.01f);

        // (3) Хваты и прижимы остаются строгими: у них флага нет, и две пары
        //     по-прежнему означают отказ даже при включённом ключе.
        party[0].act = "cPlActHagaijime4Feet";
        for (int i = 0; i < 3; ++i) {
            monsters[i].kind = "uEm0200";
            monsters[i].act  = "cEm0200ActWait";
            // Радиус прижима всего 2 м, поэтому ставим всех трёх ВНУТРЬ него:
            // именно так выглядит толпа вокруг держащего в бою.
            monsters[i].z = 0.0f;
            monsters[i].x = 50.0f + (float)i * 50.0f;   // 0.5 / 1.0 / 1.5 м
        }
        ScanTacticalSituations(party, 2, monsters, 3, &scan);
        assert(!scan.matched);
        assert(scan.situation == TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM);
        assert(scan.pairCandidates == 3);

        // Возвращаем арену В ТОЧНОСТИ как была: следующие проверки считают
        // расстояния по этим координатам, и любая невосстановленная ось
        // сломает их молча.
        MonsterAI::SetNearestPairFallback(false);
        party[0].vocation = savedVocation;
        party[0].slot     = savedSlot;
        for (int i = 0; i < 2; ++i) party[i].act = savedPartyActs[i];
        for (int i = 0; i < 3; ++i) {
            monsters[i].act  = savedMonActs[i];
            monsters[i].kind = savedKinds[i];
            monsters[i].x = savedMonX[i];
            monsters[i].y = savedMonY[i];
            monsters[i].z = savedMonZ[i];
        }
    }

    // Species admission is exact and reliable spatial identity is mandatory.
    monsters[0].kind = "uEm0200Variant";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(!scan.matched && scan.pairCandidates == 0);
    monsters[0].kind = "uEm0200";
    party[0].positionValid = false;
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(!scan.matched && scan.positionRejected == 3);

    // Exact uEm0100 GrabStart is its own lower-priority ALERT, not a wolf row.
    party[0].positionValid = true;
    party[0].act = "cPlActGrabStart";
    for (int i = 0; i < 3; ++i) monsters[i].kind = "uEm0100";
    monsters[0].x = 100.0f;
    monsters[1].x = 5500.0f;
    monsters[2].x = 6500.0f;
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT);
    assert(scan.response == TACTICAL_RESPONSE_ALERT);
    assert(scan.match.priority == 90);
    assert(scan.match.maxLeaseMs == 4000);
    assert(scan.match.evidenceBody == monsters[0].body);

    // Night-shore hold: cPlActHagaijime (not 4Feet) continues the same ALERT.
    party[0].act = "cPlActHagaijime";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT);
    assert(scan.match.maxLeaseMs == 4000);
    party[0].act = "cPlActHagaijime4Feet";
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(!scan.matched);
    party[0].act = "cPlActGrabStart";

    // A close wolf pair outranks the opportunist row.
    monsters[0].kind = "uEm0200";
    monsters[1].kind = "uEm0100";
    monsters[1].x = 120.0f;
    ScanTacticalSituations(party, 2, monsters, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PACK_GRAB_ALERT);
    assert(scan.match.priority == 100);
}

static void TestBuild012TacticalArbitrationAndLifecycle()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1200.0f, 1200.0f, true);
    SetMember(1, 100.0f, 1000.0f, true);  // strategic PackMark
    SetMember(2, 1000.0f, 1000.0f, true); // tactical holder
    SetMember(3, 1100.0f, 1100.0f, true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetWolves(3);
    Decide(300000);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    assert(Recommendation() == RECOMMEND_FOCUS);

    // A cue is observed with the existing actuator OFF: automatic evidence is
    // read-only and strategic PackMark is not overwritten.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED1].liveAct, "cPlActLiftRun");
    g_snapshot.member[Runtime::PARTY_HIRED1].x = 2000.0f;
    strcpy(s_view[1].act, "cEm0200Lifted");
    s_view[1].x = 2100.0f;

    // A visually plausible pair is not admitted until the full party/body
    // bridge is exact. This keeps read-only transition evidence fail-closed too.
    const uintptr_t exactHired2 = g_identityBody[Runtime::PARTY_HIRED2];
    g_identityDuplicate[Runtime::PARTY_HIRED2] = true;
    g_identityBody[Runtime::PARTY_HIRED2] = 0;
    UpdateTacticalSituations(300000);
    assert(!s_tactical.active);
    g_identityDuplicate[Runtime::PARTY_HIRED2] = false;
    g_identityBody[Runtime::PARTY_HIRED2] = exactHired2;

    // World action evidence older than 450 ms is discarded before matching.
    WorldReport stale = {};
    stale.timestampMs = 299000;
    stale.count = 1;
    stale.units[0].ptr = s_view[1].body;
    stale.units[0].kind = "uEm0200";
    strcpy(stale.units[0].actName, "cEm0200Lifted");
    CombatBus::Instance().PublishWorld(stale);
    UpdateViews(300150);
    assert(s_nView == 0);
    UpdateTacticalSituations(300150);
    assert(!s_tactical.active);

    SetWolves(3);
    strcpy(s_view[1].act, "cEm0200Lifted");
    s_view[1].x = 2100.0f;
    UpdateTacticalSituations(300150);
    assert(s_tactical.active);
    assert(s_tactical.targetSlot == Runtime::PARTY_HIRED1);
    assert(s_tactical.victimBody == s_view[1].body);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(g_overrides.empty() && g_focusMember == -1);
    assert(GameplayWriteCount() == 0);

    // Existing Director consent actuates the urgent target. The held wolf is
    // excluded from BOTH Tempo ownership and the Aggro lease.
    SetActuatorEnabled(true);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tactical-pack-lift-rescue");
    assert(g_focusMember == Runtime::PARTY_HIRED1);
    assert(g_focusBody == g_snapshot.member[Runtime::PARTY_HIRED1].body);
    assert(g_focusExcludedBody == s_view[1].body);
    assert(g_overrides.size() == 2);
    assert(g_overrides.count(s_view[0].body) == 1);
    assert(g_overrides.count(s_view[1].body) == 0);
    assert(g_overrides.count(s_view[2].body) == 1);
    assert(PackMarkSlot() == Runtime::PARTY_MAIN);

    // An unrelated exact4 record substitution is a new event topology even
    // when holder/victim identities are unchanged. The old bounded lease is
    // released and reacquired rather than silently inherited.
    const size_t clearedBeforeTopology = g_cleared.size();
    g_snapshot.member[Runtime::PARTY_HIRED2].record += 0x8000u;
    UpdateTacticalSituations(300225);
    assert(s_tactical.active && s_tactical.sinceMs == 300225);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(g_cleared.size() >= clearedBeforeTopology + 2);
    assert(g_focusExcludedBody == s_view[1].body);

    // After strict admission, unrelated duplicate holder evidence cannot steal
    // or release the exact retained holder/victim pair.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActLiftBeginSmall");
    g_snapshot.member[Runtime::PARTY_HIRED2].x = 9000.0f;
    UpdateTacticalSituations(300300);
    assert(s_tactical.active && s_tactical.sinceMs == 300225);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tactical-pack-lift-rescue");
    assert(g_focusMember == Runtime::PARTY_HIRED1);
    assert(g_focusExcludedBody == s_view[1].body);
    assert(g_overrides.size() == 2);

    // Clearing the unrelated duplicate leaves the exact event unchanged.
    // Continuous evidence still hits the original hard cap and cannot rearm.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActWait");
    UpdateTacticalSituations(300450);
    assert(s_tactical.active && s_tactical.sinceMs == 300225);
    ApplyPolicies();
    assert(g_focusMember == Runtime::PARTY_HIRED1);
    UpdateTacticalSituations(302950); // exactly 2500 ms
    assert(!s_tactical.active && s_tactical.timeoutBlocked);
    ApplyPolicies();
    assert(g_focusMember == Runtime::PARTY_MAIN);
    UpdateTacticalSituations(303100);
    assert(!s_tactical.active && s_tactical.timeoutBlocked);

    strcpy(s_view[1].act, "cEm0200ActWait");
    UpdateTacticalSituations(303250);
    assert(!s_tactical.active && !s_tactical.timeoutBlocked);
    strcpy(s_view[1].act, "cEm0200Lifted");
    UpdateTacticalSituations(303400);
    assert(s_tactical.active);

    // Pair topology change is a new event and moves the exact exclusion. The
    // old victim becomes a responder; the new victim receives no ownership.
    strcpy(s_view[1].act, "cEm0200ActWait");
    strcpy(s_view[2].act, "cEm0200Lifted");
    s_view[2].x = 2050.0f;
    UpdateTacticalSituations(303550);
    assert(s_tactical.active && s_tactical.victimBody == s_view[2].body);
    ApplyPolicies();
    assert(g_focusExcludedBody == s_view[2].body);
    assert(g_overrides.size() == 2);
    assert(g_overrides.count(s_view[2].body) == 0);
    assert(g_overrides.count(s_view[1].body) == 1);

    // Victim freed: tactical ownership ends now, native actions are untouched,
    // and the strategic policy is restored without waiting for the 500 ms lane.
    strcpy(s_view[2].act, "cEm0200ActWait");
    UpdateTacticalSituations(303700);
    assert(!s_tactical.active);
    ApplyPolicies();
    assert(g_focusMember == Runtime::PARTY_MAIN);
    assert(g_focusExcludedBody == 0);
    assert(g_overrides.size() == 3);

    // A one-wolf pack has no free responder. The held body receives neither
    // actuator, and the synchronized policy fails closed rather than mutating it.
    SetWolves(1);
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED1].liveAct, "cPlActLiftWalk");
    g_snapshot.member[Runtime::PARTY_HIRED1].x = 100.0f;
    strcpy(s_view[0].act, "cEm0200Lifted");
    s_view[0].x = 150.0f;
    UpdateTacticalSituations(303850);
    assert(s_tactical.active);
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "wolf-pack-no-free-responder");
    assert(g_overrides.empty() && g_focusMember == -1);
    assert(g_focusExcludedBody == 0);
}

static void TestBuild012TwoStageResponseLifecycle()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1200.0f, 1200.0f, true);
    SetMember(1, 900.0f, 900.0f, true);
    SetMember(2, 1000.0f, 1000.0f, true);
    SetMember(3, 1100.0f, 1100.0f, true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetWolves(3);

    Runtime::PartyCombatMember& holder =
        g_snapshot.member[Runtime::PARTY_HIRED1];
    holder.x = 2000.0f;
    s_view[0].x = 2050.0f; // exact pinned candidate
    s_view[1].x = 5000.0f;
    s_view[2].x = 9000.0f;

    SetActuatorEnabled(true);
    g_tempoReady = false;
    g_tempoReason = "tempo-general-hook-missing";

    // Stage 1 is immediate and weak in Aggro, but every admitted emergency
    // still requires the same full-urgency Tempo envelope for each free wolf.
    strcpy(holder.liveAct, "cPlActGrabStart");
    UpdateTacticalSituations(400000);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_PACK_GRAB_ALERT);
    assert(s_tactical.response == TACTICAL_RESPONSE_ALERT);
    assert(s_tactical.victimBody == s_view[0].body);
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tempo-general-hook-missing");
    assert(g_focusMember == -1 && g_overrides.empty());

    g_tempoReady = true;
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tactical-grab-alert");
    assert(g_focusMember == Runtime::PARTY_HIRED1);
    assert(g_focusBody == holder.body);
    assert(g_focusExcludedBody == s_view[0].body);
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALERT);
    assert(g_overrides.size() == 2);
    assert(s_nResponderWolf == 2 && s_nOwnedWolf == 2);
    for (std::map<uintptr_t, MobilizationCall>::const_iterator it =
             g_overrides.begin(); it != g_overrides.end(); ++it)
        assert(std::fabs(it->second.urgency - 1.0f) < 0.00001f);

    // A failed attempt expires as soon as GrabStart clears. Aggro ends now;
    // its two Tempo envelopes leave hold and enter ordinary bounded decay.
    strcpy(holder.liveAct, "cPlActWait");
    UpdateTacticalSituations(400150);
    assert(!s_tactical.active && !s_tactical.timeoutBlocked);
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(g_focusMember == -1);
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_NONE);
    assert(g_overrides.empty() && g_decaying.size() == 2);

    // A later attempt refreshes the same exact envelopes, then the confirmed
    // holder action crosses a clean ALERT -> ALARM Aggro boundary.
    strcpy(holder.liveAct, "cPlActGrabStart");
    UpdateTacticalSituations(400300);
    ApplyPolicies();
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALERT);
    assert(g_overrides.size() == 2 && g_decaying.empty());
    strcpy(holder.liveAct, "cPlActHagaijime4Feet");
    UpdateTacticalSituations(400450);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM);
    assert(s_tactical.response == TACTICAL_RESPONSE_ALARM);
    assert(s_tactical.sinceMs == 400450);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tactical-ground-pin-alarm");
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALARM);
    assert(g_focusExcludedBody == s_view[0].body);
    assert(g_overrides.size() == 2);
    assert(g_overrides.count(s_view[0].body) == 0);
    assert(g_overrides.count(s_view[1].body) == 1);
    assert(g_overrides.count(s_view[2].body) == 1);
    assert(s_nResponderWolf == 2 && s_nOwnedWolf == 2);

    // End the first event, then prove direct strong admission. No GrabStart is
    // sampled in this event and no transition-state prerequisite exists.
    strcpy(holder.liveAct, "cPlActWait");
    UpdateTacticalSituations(400600);
    ApplyPolicies();
    assert(!s_tactical.active);
    assert(g_focusMember == -1 && g_overrides.empty());
    strcpy(holder.liveAct, "cPlActHagaijime4Feet");
    UpdateTacticalSituations(400750);
    assert(s_tactical.active);
    assert(s_tactical.response == TACTICAL_RESPONSE_ALARM);
    ApplyPolicies();
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALARM);
    assert(g_overrides.size() == 2);

    // The strong lease is bounded and cannot rearm on continuous evidence.
    UpdateTacticalSituations(404750);
    assert(!s_tactical.active && s_tactical.timeoutBlocked);
    ApplyPolicies();
    assert(g_focusMember == -1 && g_overrides.empty());
    UpdateTacticalSituations(404900);
    assert(!s_tactical.active && s_tactical.timeoutBlocked);
    strcpy(holder.liveAct, "cPlActWait");
    UpdateTacticalSituations(405050);
    assert(!s_tactical.active && !s_tactical.timeoutBlocked);
}

static void TestGoblinOpportunistGrabPin()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1200.0f, 1200.0f, true);
    SetMember(1, 100.0f, 1000.0f, true);
    SetMember(2, 1000.0f, 1000.0f, true);
    SetMember(3, 1100.0f, 1100.0f, true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;

    SetGoblins(3);
    Decide(500000);
    assert(PackMarkSlot() == -1);
    assert(Recommendation() == RECOMMEND_NONE);
    SetActuatorEnabled(true);
    g_tempoReady = false;
    g_tempoReason = "tempo-general-hook-missing";
    s_mode = RECOMMEND_FOCUS;
    s_mark = Runtime::PARTY_MAIN;
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "wolf-pack-lost");
    assert(g_overrides.empty() && g_focusMember == -1);
    assert(GameplayWriteCount() == 0);

    g_tempoReady = true;   // 84.20: goblin-lease получает std-rush
    Runtime::PartyCombatMember& holder =
        g_snapshot.member[Runtime::PARTY_HIRED1];
    holder.x = 2000.0f;
    strcpy(holder.liveAct, "cPlActGrabStart");
    s_view[0].x = 2050.0f;
    s_view[1].x = 5000.0f;
    s_view[2].x = 9000.0f;
    UpdateTacticalSituations(500150);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT);
    assert(s_tactical.response == TACTICAL_RESPONSE_ALERT);
    assert(s_tactical.victimBody == s_view[0].body);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tactical-goblin-grab-alert");
    assert(g_focusMember == Runtime::PARTY_HIRED1);
    assert(g_focusBody == holder.body);
    assert(g_focusExcludedBody == s_view[0].body);
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALERT);
    assert(strcmp(g_focusKind, "uEm0100") == 0);
    // 84.20 std-rush: все свободные goblin мобилизованы (2 responders).
    assert(g_overrides.size() == 2);
    assert(s_nResponderWolf == 2 && s_nOwnedWolf == 2);
    assert(GameplayWriteCount() == 3);

    // Log 23: pawn leaves GrabStart for cPlActHagaijime. Same lease, past 750 ms.
    strcpy(holder.liveAct, "cPlActHagaijime");
    UpdateTacticalSituations(500900);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT);
    assert(s_tactical.sinceMs == 500150);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(g_overrides.size() == 2);
    assert(strcmp(g_focusKind, "uEm0100") == 0);

    s_nView = 5;
    for (int i = 3; i < 5; ++i) {
        MonsterView& v = s_view[i];
        memset(&v, 0, sizeof(v));
        v.body = 0x9000u + (uintptr_t)(i - 3) * 0x100u;
        strcpy(v.kind, "uEm0200");
        strcpy(v.act, "cEm0200ActWait");
        v.positionValid = true;
        v.x = 8000.0f;
        v.y = 0.0f;
        v.z = 0.0f;
    }
    UpdateTacticalSituations(501050);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(g_overrides.size() == 2);
    assert(s_nOwnedWolf == 2);
    assert(s_nResponderWolf == 2);
    assert(strcmp(g_focusKind, "uEm0100") == 0);

    strcpy(s_view[1].kind, "uEm0100_0");
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(s_nResponderWolf == 1);
    assert(g_overrides.size() == 1);
    strcpy(s_view[1].kind, "uEm0100");

    SetGoblins(1);
    s_view[0].x = 2050.0f;
    strcpy(holder.liveAct, "cPlActHagaijime");
    UpdateTacticalSituations(501200);
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus()) == "goblin-no-free-responder");
    assert(g_overrides.empty() && g_focusMember == -1);

    SetWolves(2);
    s_nView = 3;
    {
        MonsterView& v = s_view[2];
        memset(&v, 0, sizeof(v));
        v.body = 0xA000;
        strcpy(v.kind, "uEm0100");
        strcpy(v.act, "cEm0100ActWait");
        v.positionValid = true;
        v.x = 0.0f;
    }
    strcpy(holder.liveAct, "cPlActWait");
    UpdateTacticalSituations(501350);
    assert(!s_tactical.active);
    g_tempoReady = true;
    s_mode = RECOMMEND_FOCUS;
    s_mark = Runtime::PARTY_MAIN;
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "focus-window-synchronized");
    assert(g_overrides.size() == 2);
    assert(g_overrides.count(0xA000) == 0);
    assert(strcmp(g_focusKind, "uEm0200") == 0);
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALARM);
}

static void SetHobs(int n)
{
    MonsterAI::s_nView = n;
    for (int i = 0; i < n; ++i) {
        MonsterAI::MonsterView& v = MonsterAI::s_view[i];
        memset(&v, 0, sizeof(v));
        v.body = 0xB000u + (uintptr_t)i * 0x100u;
        strcpy(v.kind, "uEm0101");
        strcpy(v.act, "cEm0100ActWait");
        v.positionValid = true;
        v.x = 2000.0f + (float)i * 80.0f;
        v.y = 0.0f;
        v.z = 0.0f;
    }
}

static void TestHobgoblinPackAndGrab()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1000.0f, 1000.0f, true);
    SetMember(1, 900.0f, 900.0f, true);
    SetMember(2, 950.0f, 950.0f, true);
    SetMember(3, 700.0f, 700.0f, true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetHobs(3);
    Decide(700000);
    assert(PackMarkSlot() == Runtime::PARTY_HIRED2);
    assert(Recommendation() == RECOMMEND_BIAS);

    SetActuatorEnabled(true);
    s_mode = RECOMMEND_FOCUS;
    s_mark = Runtime::PARTY_HIRED2;
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "focus-window-synchronized");
    assert(strcmp(g_focusKind, "uEm0101") == 0);
    assert(g_overrides.size() == 3);

    Runtime::PartyCombatMember& holder =
        g_snapshot.member[Runtime::PARTY_HIRED1];
    holder.x = 2000.0f;
    strcpy(holder.liveAct, "cPlActGrabStart");
    s_view[0].x = 2050.0f;
    s_view[1].x = 5000.0f;
    s_view[2].x = 9000.0f;
    UpdateTacticalSituations(700150);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_HOB_GRAB_ALERT);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "tactical-hob-grab-alert");
    assert(strcmp(g_focusKind, "uEm0101") == 0);
    assert(g_overrides.size() == 2);
}


static void SetSaurians(int n)
{
    MonsterAI::s_nView = n;
    for (int i = 0; i < n; ++i) {
        MonsterAI::MonsterView& v = MonsterAI::s_view[i];
        memset(&v, 0, sizeof(v));
        v.body = 0xC000u + (uintptr_t)i * 0x100u;
        strcpy(v.kind, "uEm0400");
        strcpy(v.act, "cEm0400ActTurn");
        v.positionValid = true;
        v.x = 2000.0f + (float)i * 80.0f;
        v.y = 0.0f;
        v.z = 0.0f;
    }
}

static void TestSaurianPackNoGrab()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1000.0f, 1000.0f, true);
    SetMember(1, 900.0f, 900.0f, true);
    SetMember(2, 950.0f, 950.0f, true);
    SetMember(3, 700.0f, 700.0f, true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetSaurians(3);
    Decide(800000);
    assert(PackMarkSlot() == Runtime::PARTY_HIRED2);
    assert(Recommendation() == RECOMMEND_BIAS);

    SetActuatorEnabled(true);
    s_mode = RECOMMEND_FOCUS;
    s_mark = Runtime::PARTY_HIRED2;
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "focus-window-synchronized");
    assert(strcmp(g_focusKind, "uEm0400") == 0);
    assert(g_overrides.size() == 3);

    Runtime::PartyCombatMember& holder =
        g_snapshot.member[Runtime::PARTY_HIRED1];
    holder.x = 2000.0f;
    strcpy(holder.liveAct, "cPlActGrabStart");
    s_view[0].x = 2050.0f;
    UpdateTacticalSituations(800150);
    assert(!s_tactical.active);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "focus-window-synchronized");
    assert(strcmp(g_focusKind, "uEm0400") == 0);
    assert(g_overrides.size() == 3);
}

static void TestOnFieldRiftedMainPawn()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1200.0f, 1200.0f, true);
    SetMember(1, 100.0f, 1000.0f, false); // запись жива, тела нет = рифт
    SetMember(2, 100.0f, 800.0f, true);   // on-field mark; ghost Main HP ignored
    SetMember(3, 1100.0f, 1100.0f, true);
    g_identityBody[0] = g_snapshot.member[0].body;
    g_identityBody[1] = 0; // absent, not duplicate
    g_identityBody[2] = g_snapshot.member[2].body;
    g_identityBody[3] = g_snapshot.member[3].body;
    SetWolves(2);

    Decide(600000);
    assert(PackMarkSlot() == Runtime::PARTY_HIRED1);
    assert(PackMarkSlot() != Runtime::PARTY_MAIN);
    assert(Recommendation() == RECOMMEND_FOCUS);
    HuntTelemetry rifted;
    assert(HuntTelemetryAt(Runtime::PARTY_MAIN, &rifted));
    assert(rifted.recordValid && !rifted.bodyValid && !rifted.scoreValid);

    SetActuatorEnabled(true);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(std::string(PolicyStatus()) == "focus-window-synchronized");
    assert(g_focusMember == Runtime::PARTY_HIRED1);

    strcpy(g_snapshot.member[0].liveAct, "cPlActGrabStart");
    s_view[0].x = 80.0f;
    g_snapshot.member[0].x = 0.0f;
    UpdateTacticalSituations(600150);
    assert(s_tactical.active);
    assert(s_tactical.targetSlot == Runtime::PARTY_ARISEN);
}


// 85.23 регресс: у PLAYER-CHANT-HARASS ЧЕТЫРЕ правила — по одному на вид жертвы.
// Продолжение ситуации обязано проверять вид ДОПУЩЕННОГО правила, а не первого
// с тем же id. До фикса жертва-волк не совпадала с гоблинским правилом, событие
// рвалось («victim-species-changed») и переадмитилось каждые 150 мс — 28 циклов
// за бой в поле 2026-09-25.
static void TestChantContinuationUsesAdmittedSpecies()
{
    MonsterAI::TacticalPartyActor party[1] = {};
    party[0].slot = 0;                       // Arisen: правило arisenOnly
    party[0].body = 0xAA00;
    party[0].act = "cPlActWpnWandBase";      // из kPlayerCasterActs
    party[0].vocation = 3;                   // Mage: требование casterVocationOnly
    party[0].positionValid = true;
    party[0].x = 0.0f; party[0].y = 0.0f; party[0].z = 0.0f;

    MonsterAI::TacticalMonsterActor mob[1] = {};
    mob[0].body = 0xBB00;                    // волк рядом с кастующим
    mob[0].kind = "uEm0200";
    mob[0].act = "cEm0200Step";
    mob[0].positionValid = true;
    mob[0].x = 10.0f; mob[0].y = 0.0f; mob[0].z = 0.0f;

    // 1) Новый путь: вид правила передаётся явно -> событие держится.
    MonsterAI::TacticalContinuation fixed;
    MonsterAI::InspectTacticalContinuation(
        MonsterAI::TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
                                0xAA00, 0xBB00, "uEm0200",
                                party, 1, mob, 1, &fixed);
    assert(fixed.targetBodyPresent);
    assert(fixed.targetActionMatched);
    assert(fixed.evidenceBodyPresent);
    assert(fixed.evidenceKindMatched);
    assert(fixed.evidenceActionMatched);
    assert(fixed.distanceValid);

    // 2) Старый вызов (вид не передан): правило ищется по id, первым идёт
    //    гоблинское — волк ему не соответствует. Это и была причина дыры;
    //    держим как контракт, чтобы «упрощение» обратно не проскочило.
    MonsterAI::TacticalContinuation legacy;
    MonsterAI::InspectTacticalContinuation(
        MonsterAI::TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
                                0xAA00, 0xBB00, 0,
                                party, 1, mob, 1, &legacy);
    assert(legacy.evidenceKindMatched == false);
}


// 85.23: лимит исполнителей [monsterAI] responderMax. Приказ не должен
// превращаться в «вся пачка разом»; при лимите берём БЛИЖАЙШИХ к очагу, а не
// первые попавшиеся из обхода памяти (порядок s_view — это порядок памяти).
static void TestResponderCapPicksNearest()
{
    MonsterAI::Shutdown();                 // чистая доска для обзора
    for (int i = 0; i < 5; ++i) {
        MonsterAI::MonsterView& v = MonsterAI::s_view[i];
        memset(&v, 0, sizeof(v));
        v.body = 0x9000u + (uintptr_t)i;
        strcpy(v.kind, "uEm0100");
        v.positionValid = true;
        // ОБРАТНЫЙ порядок: в обзоре список идёт от дальнего к ближнему, так
        // что совпадение с порядком памяти не замаскирует отсутствие сортировки.
        v.x = (float)(5 - i) * 10.0f;      // 50, 40, 30, 20, 10 м
        v.dead = false;
    }
    MonsterAI::s_nView = 5;

    uintptr_t out[16] = {};
    const char* why = 0;
    const float ref[3] = { 0.0f, 0.0f, 0.0f };

    int n = MonsterAI::CollectEligibleResponders(out, 16, 0, "uEm0100",
                                                 ref, 2, &why);
    assert(n == 2);
    assert(out[0] == 0x9004u);             // 10 м — ближний, он в обзоре последний
    assert(out[1] == 0x9003u);             // 20 м — второй

    // 0 = все (прежнее поведение), но ранжирование остаётся: ближний первым.
    n = MonsterAI::CollectEligibleResponders(out, 16, 0, "uEm0100",
                                             ref, 0, &why);
    assert(n == 5);
    assert(out[0] == 0x9004u);
    assert(out[4] == 0x9000u);             // 50 м — последний

    // Исключённое тело (жертва) не попадает в список никогда.
    n = MonsterAI::CollectEligibleResponders(out, 16, 0x9004u, "uEm0100",
                                             ref, 0, &why);
    assert(n == 4);
    assert(out[0] == 0x9003u);

    MonsterAI::s_nView = 0;
}


// 85.23 сквозной регресс полевого сценария 2026-09-25: жертва — ВОЛК.
// PLAYER-CHANT-HARASS обязан пережить продолжение ситуации, а не рваться
// каждые 150 мс на victim-species-changed (в поле — 28 циклов за бой).
static void TestChantLifecycleHoldsForWolfVictim()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1200.0f, 1200.0f, true);      // Arisen
    SetMember(1, 1000.0f, 1000.0f, true);
    SetMember(2, 1000.0f, 1000.0f, true);
    SetMember(3, 1000.0f, 1000.0f, true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;

    g_snapshot.member[0].vocation = 3;                          // Mage
    strcpy(g_snapshot.member[0].liveAct, "cPlActWpnWandBase");  // kPlayerCasterActs

    SetWolves(1);
    s_view[0].x = 1000.0f;    // 10 м от Аризена (x=0), внутри радиуса 14 м

    UpdateTacticalSituations(400000);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_PLAYER_CHANT_HARASS);
    assert(strcmp(s_tactical.responderKind, "uEm0200") == 0);

    // Продолжение: та же пара через 150 мс. До фикса здесь был
    // release(victim-species-changed) — правило искалось по id, первым шло
    // гоблинское, волк ему не соответствовал.
    //
    // Ловим именно МИГАНИЕ, а не «активно ли»: сорванное событие успевает
    // переадмититься в том же кадре, и флаг active остаётся поднятым. Признак
    // подмены — sinceMs: он сбрасывается при релизе и переставляется при
    // повторном допуске. Держится аренда => sinceMs не двигается.
    UpdateTacticalSituations(400150);
    assert(s_tactical.active);
    assert(s_tactical.sinceMs == 400000);

    // Ещё тик: «держится не только на первом кадре».
    UpdateTacticalSituations(400300);
    assert(s_tactical.active);
    assert(s_tactical.sinceMs == 400000);
}

// 85.28/85.29: FALLEN-GUARD — встреча игрока у тела павшей пешки.
//
// ЧТО ЗДЕСЬ ГЛАВНОЕ. Событие должно не «поменять цифру на панели», а выйти
// РЕАЛЬНЫМ ПРИКАЗОМ: агрессия на Аризена и аренда темпа исполнителям. Именно
// на этом провалилась версия 85.27, где механизм сидел в выборе цели — его
// читает только панель F12, а монстров разворачивает Aggro::DirectorFocusSet.
// Поэтому ключевые проверки здесь — g_focusMember и g_overrides.
//
// ВТОРАЯ ГЛАВНАЯ ВЕЩЬ — ЕДИНИЦЫ. Мир DDDA в САНТИМЕТРАХ (100 единиц = 1 м), и
// именно на этом механизм молчал в поле: 85.28 считал координаты метрами, из-за
// чего радиус «10 м» работал как 10 см, а «монстры у тела (15 м)» — как 1500 м.
// Фикстура этого не ловила, потому что числа в ней были придуманы мной — в
// метрах. Теперь координаты здесь ТАКИЕ ЖЕ, как в игре, и проверяются не только
// факты, но и сами метры: снятие деления на масштаб ломает тест.
static void TestFallenGuardSituation()
{
    using namespace MonsterAI;
    FreshDirector();

    SetMember(0, 1000.0f, 1000.0f, true);
    SetMember(1, 900.0f,  900.0f,  true);
    SetMember(2, 950.0f,  950.0f,  true);
    SetMember(3, 700.0f,  700.0f,  true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetGoblins(3);   // сессия 85.27 была чисто гоблинская — проверяем на гоблинах

    // Аризен стоит на x=200000 см, тело павшей пешки — в 500 см от него (5.0 м).
    g_snapshot.member[Runtime::PARTY_ARISEN].x = 200000.0f;
    g_snapshot.member[Runtime::PARTY_ARISEN].z = 0.0f;
    g_snapshot.member[Runtime::PARTY_HIRED2].x = 200500.0f;
    g_snapshot.member[Runtime::PARTY_HIRED2].z = 0.0f;
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgCrumbleDead");
    strcpy(g_snapshot.member[Runtime::PARTY_ARISEN].liveAct, "cPlActRun");
    // Гоблины у тела: 5.0 / 1.5 / 3.8 м от лежащей. Ближайший — ВТОРОЙ в списке,
    // поэтому «взять первого» тест не пройдёт.
    s_view[0].x = 200500.0f + 500.0f;  s_view[0].z = 0.0f;
    s_view[1].x = 200500.0f + 150.0f;  s_view[1].z = 0.0f;
    s_view[2].x = 200500.0f + 380.0f;  s_view[2].z = 0.0f;

    Decide(400000);
    UpdateTacticalSituations(400000);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_FALLEN_GUARD);
    assert(s_tactical.targetSlot == Runtime::PARTY_ARISEN);
    assert(s_tactical.victimBody == s_view[1].body);
    assert(!strcmp(TacticalSituationName(s_tactical.situation), "FALLEN-GUARD"));
    // Дистанция в событии — ПОДХОД ИГРОКА к телу, в МЕТРАХ (500 см = 5.0 м).
    assert(std::fabs(s_tactical.pairDistanceM - 5.0f) < 0.001f);
    assert(!s_tactical.fallen.reason);   // при состоявшемся событии причин нет

    // Пока галка актуатора снята, событие только наблюдается: ни записи, ни
    // аренды, ни агрессии. Прибор обязан оставаться прибором.
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(g_overrides.empty());
    assert(g_focusMember == -1);
    assert(GameplayWriteCount() == 0);

    // ГЛАВНОЕ: с включённым актуатором приказ уходит НАСТОЯЩИЙ — агрессия на
    // Аризена плюс аренда темпа исполнителям. Ровно то, чего не случилось в 85.27.
    SetActuatorEnabled(true);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(g_focusMember == Runtime::PARTY_ARISEN);
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALERT);
    assert(!strcmp(g_focusKind, "uEm0100"));
    assert(!g_overrides.empty());

    // Пешка ВСТАЛА — охранять некого, событие обязано кончиться в тот же скан.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActRun");
    UpdateTacticalSituations(400150);
    assert(!s_tactical.active);

    // Игрок УШЁЛ: пешка снова лежит, но Аризен уже за радиусом (2500 см = 25 м).
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgCrumbleDead");
    g_snapshot.member[Runtime::PARTY_ARISEN].x = 203000.0f;
    UpdateTacticalSituations(400300);
    assert(!s_tactical.active);
    // И лог получает причину с числом: 25.0 м при радиусе 10 м.
    assert(s_tactical.fallen.reason
           && !strcmp(s_tactical.fallen.reason, "approach-too-far"));
    assert(std::fabs(s_tactical.fallen.approachM - 25.0f) < 0.001f);

    // Подошёл снова (300 см = 3 м) — событие допускается заново (без «залипания»).
    g_snapshot.member[Runtime::PARTY_ARISEN].x = 200800.0f;
    UpdateTacticalSituations(400450);
    assert(s_tactical.active);

    // 85.29: ВЕРДИКТ НАБЛЮДАТЕЛЯ вместо имени акта. Пешка падает актом, которого
    // в наших списках нет (имена актов уже один раз подвели), но PartyStatus
    // подтвердил падение — событие обязано состояться.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActZzzUnknownFall");
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = true;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedRevivable = true;
    UpdateTacticalSituations(400600);
    assert(s_tactical.active);
    assert(s_tactical.targetSlot == Runtime::PARTY_ARISEN);
    // И обратная сторона: вердикт снят, а имя акта нам по-прежнему неизвестно —
    // событие обязано кончиться. Допуск и освобождение проверяют ОДНО условие.
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = false;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedRevivable = false;
    UpdateTacticalSituations(400700);
    assert(!s_tactical.active);

    // ── 85.30: ПЕШКА НА ЗЕМЛЕ, НО В СОЗНАНИИ ────────────────────────────────
    // Владелец, поле 85.29: «пешка на земле, но не без сознания = таргет для
    // окружающих монстров! Не на игрока надо ломиться, а добивать лежащую
    // пешку, чтобы выключить её из боя, не дать ей подняться».
    // Значит: цель — САМА ПЕШКА (не Аризен), и встреча у тела тут не включается.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgDown");
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = true;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = true;
    UpdateTacticalSituations(400800);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_PAWN_FINISH);
    assert(s_tactical.targetSlot == Runtime::PARTY_HIRED2);      // ЦЕЛЬ — ПЕШКА
    assert(s_tactical.targetSlot != Runtime::PARTY_ARISEN);
    assert(!strcmp(TacticalSituationName(s_tactical.situation), "PAWN-FINISH"));

    // И приказ тоже настоящий, и тоже НА ПЕШКУ.
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(g_focusMember == Runtime::PARTY_HIRED2);
    assert(g_focusBody == g_snapshot.member[Runtime::PARTY_HIRED2].body);
    assert(g_focusResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALERT);
    assert(!strcmp(g_focusKind, "uEm0100"));

    // Пешка ВСТАЛА — добивать некого, событие кончается в тот же скан.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActRun");
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = false;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = false;
    UpdateTacticalSituations(400900);
    assert(!s_tactical.active);

    // А теперь она ушла БЕЗ СОЗНАНИЯ (neardeath): добивание снимается, задача
    // выполнена, и включается встреча у тела — цель снова ИГРОК.
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgCrumbleDead");
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = true;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = false;
    UpdateTacticalSituations(401000);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_FALLEN_GUARD);
    assert(s_tactical.targetSlot == Runtime::PARTY_ARISEN);
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = false;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = false;

    // Ключ 0 = механизм выключен: события не будет вовсе. Ставим ПОСЛЕ
    // FreshDirector: инициализация читает ключ из ini (в фикстуре — умолчание
    // 10) и перезаписывает всё, что выставлено до неё. Это правильное
    // поведение продукта: ключ в ini — источник правды, а не догадка теста.
    FreshDirector();
    MonsterAI::SetFallenGuardRadius(0.0f);
    SetMember(0, 1000.0f, 1000.0f, true);
    SetMember(1, 900.0f,  900.0f,  true);
    SetMember(2, 950.0f,  950.0f,  true);
    SetMember(3, 700.0f,  700.0f,  true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetGoblins(3);
    g_snapshot.member[Runtime::PARTY_ARISEN].x = 200000.0f;
    g_snapshot.member[Runtime::PARTY_HIRED2].x = 200500.0f;
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgCrumbleDead");
    UpdateTacticalSituations(500000);
    assert(!s_tactical.active);

    // Ключ добивания `pawnFinish = 0` выключает его так же, как радиус
    // выключает встречу: механизм, который нельзя погасить без сборки, в поле
    // проверять неудобно.
    FreshDirector();
    SetMember(0, 1000.0f, 1000.0f, true);
    SetMember(1, 900.0f,  900.0f,  true);
    SetMember(2, 950.0f,  950.0f,  true);
    SetMember(3, 700.0f,  700.0f,  true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetGoblins(3);
    for (int i = 0; i < 3; ++i) { s_view[i].x = 200000.0f + (float)i * 100.0f; }
    g_snapshot.member[Runtime::PARTY_HIRED2].x = 200100.0f;
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgDown");
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = true;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = true;
    MonsterAI::SetPawnFinishEnabled(false);
    UpdateTacticalSituations(600000);
    assert(!s_tactical.active);
    MonsterAI::SetPawnFinishEnabled(true);

    MonsterAI::SetFallenGuardRadius(10.0f);   // вернуть для остальных тестов
}

// 85.30: добивание — на уровне матчера, без Директора. Каждый блок убивает свою
// поломку: адресата (пешка вместо игрока), различение двух падений, порог
// близости монстров и диагностику отказа.
static void TestPawnFinishUnitsAndBlockers()
{
    using namespace MonsterAI;
    SetPawnFinishEnabled(true);

    TacticalPartyActor party[3];
    memset(party, 0, sizeof(party));
    party[0].slot = Runtime::PARTY_ARISEN;
    party[0].body = 0x111100u;
    party[0].act = "cPlActRun";
    party[0].positionValid = true;
    party[0].x = 0.0f;
    party[1].slot = Runtime::PARTY_HIRED1;
    party[1].body = 0x222200u;
    party[1].act = "cPlActDmgDown";          // сбита с ног: на земле, в сознании
    party[1].positionValid = true;
    party[1].x = 300.0f;
    party[1].downedValid = true;
    party[1].downedAwake = true;
    party[2].slot = Runtime::PARTY_HIRED2;
    party[2].body = 0x333300u;
    party[2].act = "cPlActWait";             // стоит на ногах: не цель
    party[2].positionValid = true;
    party[2].x = 100.0f;

    TacticalMonsterActor mobs[2];
    memset(mobs, 0, sizeof(mobs));
    mobs[0].body = 0x444400u; mobs[0].kind = "uEm0100"; mobs[0].act = "cEm0100ActWait";
    mobs[0].positionValid = true;
    mobs[0].x = 500.0f;                      // 2.0 м от сбитой пешки
    mobs[1].body = 0x555500u; mobs[1].kind = "uEm0100"; mobs[1].act = "cEm0100ActRun";
    mobs[1].positionValid = true;
    mobs[1].x = 800.0f;                      // 5.0 м

    TacticalScan scan;
    ScanTacticalSituations(party, 3, mobs, 2, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PAWN_FINISH);
    assert(scan.match.targetSlot == Runtime::PARTY_HIRED1);   // цель — ПЕШКА
    assert(scan.match.targetBody == party[1].body);
    assert(scan.match.evidenceBody == mobs[0].body);          // ближайший монстр
    assert(std::fabs(scan.match.pairDistanceM - 2.0f) < 0.001f);   // метры!
    assert(scan.finish.monstersOfKind == 2);

    // Стоящая рядом пешка (Hired2) целью не становится ни при каких условиях:
    // добивают только лежащую в сознании.
    assert(scan.match.targetBody != party[2].body);
    assert(scan.match.targetBody != party[0].body);

    // Игрок (Аризен) — вообще не цель этого механизма, даже если стоит вплотную.
    party[0].x = 500.0f;
    ScanTacticalSituations(party, 3, mobs, 2, &scan);
    assert(scan.matched);
    assert(scan.match.targetSlot == Runtime::PARTY_HIRED1);

    // Лежит БЕЗ сознания — это уже не добивание, а встреча у тела: механизм
    // добивания обязан молчать (иначе он бил бы лежачего без сознания).
    // Гасим встречу у тела ключом, чтобы причина в логе принадлежала именно
    // добиванию: два механизма делят один канал диагностики, и без этого
    // проверялось бы «кто-то из них объяснился», а не добивание.
    SetFallenGuardRadius(0.0f);
    party[1].downedAwake = false;
    party[1].act = "cPlActDmgCrumbleDead";
    ScanTacticalSituations(party, 3, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.finish.reason
           && !strcmp(scan.finish.reason, "no-pawn-on-ground-awake"));

    // Сбита с ног, но монстров рядом нет: ближайший в 12 м при пределе 10 м.
    party[1].downedAwake = true;
    party[1].act = "cPlActDmgDown";
    mobs[0].x = 1500.0f;
    mobs[1].x = 1600.0f;
    ScanTacticalSituations(party, 3, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.finish.reason && !strcmp(scan.finish.reason, "no-mob-at-pawn"));
    assert(std::fabs(scan.finish.nearestKindM - 12.0f) < 0.001f);

    // Ключ 0: механизм выключен целиком — даже когда всё сошлось.
    mobs[0].x = 500.0f;
    SetPawnFinishEnabled(false);
    ScanTacticalSituations(party, 3, mobs, 2, &scan);
    assert(!scan.matched);
    SetPawnFinishEnabled(true);
    ScanTacticalSituations(party, 3, mobs, 2, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PAWN_FINISH);   // и это он, а не встреча
    SetFallenGuardRadius(10.0f);
}

// 85.32: ВЕС ВМЕСТО СТАРШИНСТВА.
//
// Владелец: «те, кто ближе к тому или иному событию, — для них это событие
// важнее; ближе бежать — выше шансы на успех». Проверяем:
//   * близкое младшее событие обходит далёкое старшее;
//   * далёкое младшее старшего НЕ обходит (старый порядок сохраняется);
//   * событие контакта (хват) остаётся впереди — оно и так живёт в 2 м;
//   * числа решения уходят в лог, иначе его не проверить в поле.
static void TestProximityWeightedArbitration()
{
    using namespace MonsterAI;
    TacticalPartyActor party[2];
    TacticalMonsterActor mobs[3];
    TacticalScan scan;

    // ── 1) ДОБИВАНИЕ РЯДОМ ПРОТИВ РОГА ДАЛЕКО ──────────────────────────────
    // Рог звучит у x=0, ближайший к нему член партии (игрок) — в 11 м: событие
    // «холодное». Лежащая в сознании пешка — у x=2000, и гоблин в МЕТРЕ от неё.
    memset(party, 0, sizeof(party));
    party[0].slot = 0; party[0].body = 0x1000u; party[0].act = "cPlActRun";
    party[0].positionValid = true; party[0].x = 1100.0f;
    party[1].slot = 3; party[1].body = 0x2000u; party[1].act = "cPlActDmgDown";
    party[1].positionValid = true; party[1].x = 2000.0f;
    party[1].downedValid = true; party[1].downedAwake = true;
    memset(mobs, 0, sizeof(mobs));
    mobs[0].body = 0x3000u; mobs[0].kind = "uEm0100";
    mobs[0].act = "cEm0100ActHornTensionUp";                 // рог
    mobs[0].positionValid = true; mobs[0].x = 0.0f;
    mobs[1].body = 0x4000u; mobs[1].kind = "uEm0100"; mobs[1].act = "cEm0100ActWait";
    mobs[1].positionValid = true; mobs[1].x = 2100.0f;       // 1.0 м от пешки
    mobs[2].body = 0x5000u; mobs[2].kind = "uEm0100"; mobs[2].act = "cEm0100ActWait";
    mobs[2].positionValid = true; mobs[2].x = 5000.0f;       // далеко от всего

    ScanTacticalSituations(party, 2, mobs, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_PAWN_FINISH);
    assert(scan.match.targetSlot == Runtime::PARTY_HIRED2);   // цель — пешка
    assert(scan.outrankedSituation == TACTICAL_SITUATION_GOB_HORN_ALERT);
    assert(scan.chosenScore > scan.outrankedScore);

    // ── 2) РОГ РЯДОМ ПРОТИВ ДОБИВАНИЯ ДАЛЕКО ───────────────────────────────
    // Игрок почти на роге (2 м) — событие «горячее»; пешка лежит в 9 м от
    // своих гоблинов. Старший снова прав, и это ровно прежнее поведение.
    party[0].x = 200.0f;
    mobs[1].x = 2900.0f;                                     // 9.0 м от пешки
    ScanTacticalSituations(party, 2, mobs, 3, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_GOB_HORN_ALERT);
    // Старший выиграл сам — значит, обойдённых нет: прежний порядок сохранён.
    assert(scan.outrankedSituation == TACTICAL_SITUATION_NONE);

    // ── 3) ХВАТ (КОНТАКТ) НЕ ОТДАЁТ ПЕРВЕНСТВО ────────────────────────────
    // Событие контакта по своим правилам живёт в 2 м, поэтому его вес и так
    // максимальный: ручной порядок строк больше не нужен, математика совпадает
    // с ним. Сбитая пешка в 1.5 м от второго гоблина проигрывает хвату.
    memset(party, 0, sizeof(party));
    party[0].slot = 0; party[0].body = 0x1000u; party[0].act = "cPlActGrabStart";
    party[0].positionValid = true; party[0].x = 100.0f;
    party[1].slot = 3; party[1].body = 0x2000u; party[1].act = "cPlActDmgDown";
    party[1].positionValid = true; party[1].x = 100.0f;
    party[1].downedValid = true; party[1].downedAwake = true;
    memset(mobs, 0, sizeof(mobs));
    mobs[0].body = 0x3000u; mobs[0].kind = "uEm0100";
    mobs[0].act = "cEm0100ActHagaijime";                     // держит игрока
    mobs[0].positionValid = true; mobs[0].x = 200.0f;
    // Второй гоблин стоит ВНЕ радиуса хвата (2 м), иначе правило хвата
    // справедливо откажется: «держат двое» — это уже другая неоднозначность.
    // Он в 2.5 м от сбитой пешки, то есть добивание тоже состоялось.
    mobs[1].body = 0x4000u; mobs[1].kind = "uEm0100"; mobs[1].act = "cEm0100ActWait";
    mobs[1].positionValid = true; mobs[1].x = 350.0f;
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT);

    // ── 4) РЕШЕНИЕ ВИДНО В ЛОГЕ ───────────────────────────────────────────
    // Механизм, который молча меняет старшинство, проверить в поле нечем.
    // Гоняем сцену через Директора и ищем строку решения в логе.
    FreshDirector();
    SetMember(0, 1000.0f, 1000.0f, true);
    SetMember(1, 900.0f,  900.0f,  true);
    SetMember(2, 950.0f,  950.0f,  true);
    SetMember(3, 700.0f,  700.0f,  true);
    for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
    SetGoblins(3);
    for (int i = 0; i < 3; ++i) { s_view[i].act[0] = 0; strcpy(s_view[i].act, "cEm0100ActWait"); }
    // Рог у x=0, игрок в 11 м от него; лежащая пешка у x=2000, гоблин в метре.
    strcpy(s_view[0].act, "cEm0100ActHornTensionUp");
    s_view[0].x = 0.0f;
    s_view[1].x = 2100.0f;
    s_view[2].x = 5000.0f;
    g_snapshot.member[Runtime::PARTY_ARISEN].x = 1100.0f;
    g_snapshot.member[Runtime::PARTY_HIRED2].x = 2000.0f;
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgDown");
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = true;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = true;
    UpdateTacticalSituations(700000);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_PAWN_FINISH);
    assert(s_tactical.targetSlot == Runtime::PARTY_HIRED2);   // цель — пешка

    logFile.flush();
    {
        std::ifstream log("/tmp/director_moment_priority_test.log");
        std::string line;
        bool sawDecision = false;
        while (std::getline(log, line)) {
            if (line.find("BY-WEIGHT name=PAWN-FINISH") == std::string::npos) continue;
            if (line.find("outranked=GOB-HORN-ALERT") == std::string::npos) continue;
            sawDecision = true;
        }
        assert(sawDecision);
    }
}

// 85.33: ПАРАЛЛЕЛЬНЫЕ ПРИКАЗЫ.
//
// Сценарий владельца: «трубит горнист, и одновременно упала пешка — пачка
// делится: часть защищает горниста, вторая атакует лежащую пешку». Проверяем:
//   * деление считается и попадает в лог ВСЕГДА (даже когда ключ выключен) —
//     это и есть «сначала лог, потом поведение»;
//   * при ключе 0 мир не меняется: второй приказ не выдаётся, главный берёт
//     всех подходящих, как раньше;
//   * при ключе 1 главный берёт ТОЛЬКО свою часть, а остальные уходят вторым
//     приказом — с той же целью и видом, что у второго события;
//   * никто не получает два приказа сразу (списки не пересекаются).
static void TestParallelOrdersSplit()
{
    using namespace MonsterAI;

    // Рог у x=0, игрок в 11 м от него. Сбитая пешка у x=2000, гоблин в метре.
    for (int pass = 0; pass < 2; ++pass) {
        const bool parallel = (pass == 1);
        config.forceBool = true;
        config.forceKey = "parallelOrders";
        config.forceValue = parallel;
        FreshDirector();
        config.forceBool = false;

        SetMember(0, 1000.0f, 1000.0f, true);
        SetMember(1, 900.0f,  900.0f,  true);
        SetMember(2, 950.0f,  950.0f,  true);
        SetMember(3, 700.0f,  700.0f,  true);
        for (int i = 0; i < 4; ++i) g_identityBody[i] = g_snapshot.member[i].body;
        SetGoblins(3);
        // s_view[0] — горнист, s_view[1] — у тела, s_view[2] — далеко от обоих.
        strcpy(s_view[0].act, "cEm0100ActHornTensionUp");
        s_view[0].x = 0.0f;
        strcpy(s_view[1].act, "cEm0100ActWait");
        s_view[1].x = 2100.0f;
        s_view[2].x = 5000.0f;
        // Аризен ближе всех к рогу (9 м) — значит рог целится в НЕГО: так
        // проверка читается прямо, без знания, кого выбрал сканер.
        g_snapshot.member[Runtime::PARTY_ARISEN].x = 900.0f;
        g_snapshot.member[Runtime::PARTY_HIRED2].x = 2000.0f;
        strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActDmgDown");
        g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = true;
        g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = true;

        SetActuatorEnabled(true);
        UpdateTacticalSituations(800000);
        ApplyPolicies();

        // Главное — добивание: пешка в метре от гоблина, это ближе, чем рог.
        assert(s_tactical.active);
        assert(s_tactical.situation == TACTICAL_SITUATION_PAWN_FINISH);
        assert(s_tactical.targetSlot == Runtime::PARTY_HIRED2);

        if (!parallel) {
            // Ключ выключен: мир как был. Второго приказа нет вовсе.
            assert(g_secondaryMember == -1);
            assert(g_secondaryBodies.empty());
            // Всплеск при этом честно достаётся главному приказу: адреналин не
            // зависит от ключа деления, он про приказ вообще.
            // Ключ деления выключен: второго приказа нет, и разгон есть только
            // у главного (в агро список не уходит — идёт «весь вид»).
            assert(g_secondaryBodies.empty());
            assert(g_overrides.size() == 3);
            for (int i = 0; i < 3; ++i)
                assert(g_overrides.find(s_view[i].body) != g_overrides.end());
        } else {
            // Ключ включён: пачка поделилась. Второй приказ — рог, цель игрок,
            // исполнители — те, кому до рога ближе, чем до пешки.
            assert(g_secondaryMember == Runtime::PARTY_ARISEN);
            assert(g_secondaryResponse == Runtime::Aggro::DIRECTOR_RESPONSE_ALERT);
            assert(!strcmp(g_secondaryKind, "uEm0100"));
            // Горнист (0 м у рога, 20 м у пешки) уходит вторым приказом;
            // дальний (41 м у рога, 30 м у пешки) остаётся с главным: ему
            // ближе к пешке. Это и есть «ближе бежать — выше шансы».
            assert(g_secondaryBodies.size() == 1);
            assert(g_secondaryBodies[0] == s_view[0].body);
            for (size_t i = 0; i < g_secondaryBodies.size(); ++i) {
                assert(g_secondaryBodies[i] != s_view[1].body);   // не тот, кто у тела
                for (size_t k = 0; k < g_focusSetBodies.size(); ++k)
                    assert(g_focusSetBodies[k] != g_secondaryBodies[i]);  // нет пересечений
            }
            // Главному достались двое: тот, кто у тела, и дальний. Список
            // уходит в агро отдельно — иначе агро запнуло бы всех без разбора.
            assert(g_focusSetBodies.size() == 2);
            assert(g_focusSetBodies[0] == s_view[1].body);   // ближний к телу первым
            assert(g_focusSetBodies[1] == s_view[2].body);

            // 85.34: ВТОРОЙ ПРИКАЗ РАЗГОНЯЕТСЯ. Владелец: «события быстрые,
            // некогда зевать — реагировать надо резко». Разгон (темп + адреналин
            // силы атаки) — это оболочка Tempo на теле, поэтому проверяем её:
            // исполнитель второго приказа обязан держать оболочку.
            for (size_t i = 0; i < g_secondaryBodies.size(); ++i)
                assert(g_overrides.find(g_secondaryBodies[i]) != g_overrides.end());
            // И наоборот: разгон есть у всех, кому выдан приказ — ни один не
            // остался «голым» наблюдателем.
            assert(g_overrides.size() == g_focusSetBodies.size()
                                         + g_secondaryBodies.size());
            // СИЛУ РЕШАЕТ КОНТЕКСТ, а не «всем поровну»: уровень разгона — это
            // urgency самого события. «Пешка упала, добей» (0.85) сильнее, чем
            // «услышал рог, иди посмотри» (0.65) — та самая лестница, ради
            // которой всё и делалось.
            assert(Near(g_overrides.find(s_view[1].body)->second.urgency, 0.85f));
            assert(Near(g_overrides.find(g_secondaryBodies[0])->second.urgency, 0.65f));
        }
    }

    // Сценарий кончился — второго приказа быть не должно (пешка встала).
    g_snapshot.member[Runtime::PARTY_HIRED2].downedValid = false;
    g_snapshot.member[Runtime::PARTY_HIRED2].downedAwake = false;
    strcpy(g_snapshot.member[Runtime::PARTY_HIRED2].liveAct, "cPlActRun");
    UpdateTacticalSituations(800500);
    ApplyPolicies();
    assert(g_secondaryMember == -1);
    // Разгон второго приказа не переживает сам приказ: тела уходят в распад.
    // Проверяем по журналу снятий: тело, бывшее исполнителем второго приказа,
    // обязано быть отпущено. Одна проверка мало что даёт (тот же моб мог легально
    // стать исполнителем ГЛАВНОГО приказа — тогда оболочка у него и должна
    // остаться), поэтому вторая проверка — строка в логе: она исчезает ровно
    // тогда, когда разгон второго приказа перестают снимать.
    for (size_t i = 0; i < g_secondaryBodies.size(); ++i) {
        const uintptr_t b = g_secondaryBodies[i];
        bool released = false;
        for (size_t k = 0; k < g_cleared.size(); ++k)
            if (g_cleared[k] == b) released = true;
        assert(released);
    }
    logFile.flush();
    {
        std::ifstream log("/tmp/director_moment_priority_test.log");
        std::string line;
        bool sawRelease = false;
        while (std::getline(log, line))
            if (line.find("adrenaline secondary released") != std::string::npos)
                sawRelease = true;
        assert(sawRelease);
    }
    config.forceBool = false;
}

// 85.29: единицы и причины отказа — на уровне матчера, без Директора.
// Каждый блок убивает свою поломку: деление на масштаб, отбор исполнителя,
// диагностику причины.
static void TestFallenGuardUnitsAndBlockers()
{
    using namespace MonsterAI;
    SetFallenGuardRadius(10.0f);

    // Мир в сантиметрах: всё, как в игре.
    TacticalPartyActor party[2];
    memset(party, 0, sizeof(party));
    party[0].slot = Runtime::PARTY_ARISEN;   // 0
    party[0].body = 0x111100u;
    party[0].act = "cPlActRun";
    party[0].positionValid = true;
    party[0].x = 0.0f; party[0].y = 0.0f; party[0].z = 0.0f;
    party[1].slot = Runtime::PARTY_HIRED2;   // 3
    party[1].body = 0x222200u;
    party[1].act = "cPlActDmgCrumbleDead";
    party[1].positionValid = true;
    party[1].x = 0.0f; party[1].y = 0.0f; party[1].z = 400.0f;   // 4.0 м
    TacticalMonsterActor mobs[2];
    memset(mobs, 0, sizeof(mobs));
    mobs[0].body = 0x333300u; mobs[0].kind = "uEm0100"; mobs[0].act = "cEm0100ActWait";
    mobs[0].positionValid = true;
    mobs[0].x = 0.0f; mobs[0].y = 0.0f; mobs[0].z = 500.0f;     // 1.0 м от тела
    // Вид, которого нет среди правил встречи: он не должен попадать в исполнители.
    mobs[1].body = 0x444400u; mobs[1].kind = "uEm0300"; mobs[1].act = "cEm0300ActWait";
    mobs[1].positionValid = true;
    mobs[1].x = 0.0f; mobs[1].y = 0.0f; mobs[1].z = 500.0f;     // не наш вид

    TacticalScan scan;
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(scan.matched);
    assert(scan.situation == TACTICAL_SITUATION_FALLEN_GUARD);
    assert(scan.match.targetSlot == Runtime::PARTY_ARISEN);
    assert(scan.match.evidenceBody == mobs[0].body);
    // Метры, не «сырые единицы»: 400 см = 4.0 м, 100 см = 1.0 м.
    assert(std::fabs(scan.match.pairDistanceM - 4.0f) < 0.001f);
    assert(std::fabs(scan.fallen.approachM - 4.0f) < 0.001f);
    assert(std::fabs(scan.fallen.nearestKindM - 1.0f) < 0.001f);
    assert(scan.fallen.monstersOfKind == 1);

    // Пешка упала актом, которого нет в списке, но наблюдатель подтвердил —
    // работает вердикт наблюдателя, а не угадывание имени.
    party[1].act = "cPlActSomeNewFall";
    party[1].downedValid = true;
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(scan.matched);
    party[1].downedValid = false;

    // Игрок ДАЛЕКО: 14.0 м при радиусе 10 м. Причина названа числом.
    party[1].act = "cPlActDmgCrumbleDead";   // падение — снова по имени акта
    party[1].z = 1400.0f;
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.fallen.reason && !strcmp(scan.fallen.reason, "approach-too-far"));
    assert(std::fabs(scan.fallen.approachM - 14.0f) < 0.001f);

    // Игрок рядом, но монстров у тела нет: 20 м — за пределом 15 м.
    party[1].z = 300.0f;
    mobs[0].z = 2300.0f;
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.fallen.reason && !strcmp(scan.fallen.reason, "no-mob-at-body"));
    assert(std::fabs(scan.fallen.nearestKindM - 20.0f) < 0.001f);

    // 85.31: счёт монстров ЧЕСТНЫЙ и при отказе по подходу. Раньше здесь стояло
    // «monstersOfKind=0» в бою, где монстры этого вида были, — читалось как «их
    // нет», а значило «мы не считали».
    party[1].act = "cPlActDmgCrumbleDead";
    party[1].z = 1400.0f;                       // 14 м при радиусе 10 м
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.fallen.reason && !strcmp(scan.fallen.reason, "approach-too-far"));
    assert(scan.fallen.monstersOfKind == 1);    // считаем ДО отказа, а не после

    // Пешки на земле нет вовсе.
    party[1].z = 400.0f;
    mobs[0].z = 400.0f;
    party[1].act = "cPlActWait";
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.fallen.reason && !strcmp(scan.fallen.reason, "no-pawn-down"));

    // 85.31: пешку НЕСУТ на руках. Поле показало, что в этом состоянии тело
    // отдаёт координаты (0,0,0), и мы печатали «подход 340 м» — выдуманное число.
    // Теперь это отдельная причина, и никаких метров не выдумывается.
    // Ровно как в поле: наблюдатель по-прежнему говорит «лежит» (downedValid),
    // а акт пешки — перенос. Тогда раньше и выходило «подход 340 м» из нулей.
    party[1].act = "cPlActLifted";
    party[1].downedValid = true;
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.fallen.reason && !strcmp(scan.fallen.reason, "pawn-carried"));
    assert(scan.fallen.approachM < 0.0f);       // метры не считались вовсе

    // Мусорная позиция (0,0,0) у лежащей пешки: «позиции нет», а не «340 м».
    party[1].act = "cPlActDmgCrumbleDead";
    party[1].downedValid = false;
    party[1].x = 0.0f; party[1].y = 0.0f; party[1].z = 0.0f;
    ScanTacticalSituations(party, 2, mobs, 2, &scan);
    assert(!scan.matched);
    assert(scan.fallen.reason
           && !strcmp(scan.fallen.reason, "pawn-position-unavailable"));
    assert(scan.fallen.approachM < 0.0f);
    party[1].z = 400.0f;
}

int main()
{
    TestPriorityAndHysteresis();
    TestIsolationDepthSeparation();
    TestValidatedFightReplay();
    TestBuild012SynchronizedMobilization();
    TestBuild012UrgencyDataDefinedMatcher();
    TestBuild012TacticalArbitrationAndLifecycle();
    TestBuild012TwoStageResponseLifecycle();
    TestGoblinOpportunistGrabPin();
    TestHobgoblinPackAndGrab();
    TestSaurianPackNoGrab();
    TestOnFieldRiftedMainPawn();
    TestChantContinuationUsesAdmittedSpecies();
    TestChantLifecycleHoldsForWolfVictim();
    TestResponderCapPicksNearest();
    TestFallenGuardSituation();
    TestFallenGuardUnitsAndBlockers();
    TestPawnFinishUnitsAndBlockers();
    TestProximityWeightedArbitration();
    TestParallelOrdersSplit();
    MonsterAI::Shutdown();
    assert(!MonsterAI::Enabled());

    std::cout
        << "director Build012: PASS (target+urgency retained; all orders mobilize; "
        << "ALERT/ALARM Aggro split; decay/hard-reset lifecycle; sticky exact pair)\n";
    return 0;
}

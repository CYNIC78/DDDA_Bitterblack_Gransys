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
static bool g_observerDemand = false;
static int g_focusMember = -1;
static uintptr_t g_focusBody = 0;
static uintptr_t g_focusExcludedBody = 0;
static int g_focusResponse = Runtime::Aggro::DIRECTOR_RESPONSE_NONE;
static char g_focusKind[16] = "uEm0200";

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
    if (!body || !exactKind || strcmp(exactKind, "uEm0200")
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
    if (member < 0 || member >= 4) return "identity-invalid-slot";
    if (g_identityRecordMissing[member]) {
        if (out) *out = 0;
        return missingRec[member];
    }
    const bool ok = ResolveMemberBody(member, out);
    return ok ? exact[member] : missing[member];
}

bool DirectorFocusSet(int member, uintptr_t expectedBody,
                      uintptr_t excludedEnemyBody, int response,
                      const char* exactKind)
{
    if (member < 0) {
        g_focusMember = -1;
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
                          && strcmp(exactKind, "uEm0100") != 0))
        return false;
    g_focusMember = member;
    g_focusBody = expectedBody;
    g_focusExcludedBody = excludedEnemyBody;
    g_focusResponse = response;
    strncpy(g_focusKind, exactKind, sizeof(g_focusKind) - 1);
    g_focusKind[sizeof(g_focusKind) - 1] = 0;
    return true;
}
} // namespace Aggro
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
    SetMember(1, 900.0f, 10000.0f, false); // only 9%, but not lowest absolute HP
    SetMember(2, 950.0f, 950.0f, false);
    SetMember(3, 700.0f, 700.0f, false);   // 100%, yet lowest absolute HP
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
    assert(!h.bodyValid && !h.positionValid && !h.coreStatsValid);
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
    SetMember(1, 187.5f, 505.0f, false);
    SetMember(2, 337.3f, 570.0f, false);
    SetMember(3, 207.9f, 498.0f, false);
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
    SetMember(0, 331.3f, 498.0f, true);
    SetMember(1, 505.0f, 505.0f, false);
    SetMember(2, 570.0f, 570.0f, false);
    SetMember(3, 498.0f, 498.0f, false);
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
    g_identityBody[Runtime::PARTY_HIRED2] = 0;
    ApplyPolicies();
    assert(!PolicyEngaged());
    assert(std::string(PolicyStatus())
           == "identity-Hired2-body-unresolved-or-duplicate");
    assert(g_overrides.empty() && g_focusMember == -1);
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
    g_identityBody[Runtime::PARTY_HIRED2] = 0;
    UpdateTacticalSituations(300000);
    assert(!s_tactical.active);
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
    assert(g_overrides.empty());
    assert(s_nResponderWolf == 2 && s_nOwnedWolf == 0);
    assert(GameplayWriteCount() == 1);

    // Log 23: pawn leaves GrabStart for cPlActHagaijime. Same lease, past 750 ms.
    strcpy(holder.liveAct, "cPlActHagaijime");
    UpdateTacticalSituations(500900);
    assert(s_tactical.active);
    assert(s_tactical.situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT);
    assert(s_tactical.sinceMs == 500150);
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(g_overrides.empty());
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
    assert(g_overrides.empty());
    assert(s_nOwnedWolf == 0);
    assert(s_nResponderWolf == 2);
    assert(strcmp(g_focusKind, "uEm0100") == 0);

    strcpy(s_view[1].kind, "uEm0100_0");
    ApplyPolicies();
    assert(PolicyEngaged());
    assert(s_nResponderWolf == 1);
    assert(g_overrides.empty());
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
    MonsterAI::Shutdown();
    assert(!MonsterAI::Enabled());

    std::cout
        << "director Build012: PASS (target+urgency retained; all orders mobilize; "
        << "ALERT/ALARM Aggro split; decay/hard-reset lifecycle; sticky exact pair)\n";
    return 0;
}

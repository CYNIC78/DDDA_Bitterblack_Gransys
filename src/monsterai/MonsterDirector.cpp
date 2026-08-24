// MonsterAI::Director — Build 84 / session Build 012.
//
// Absolute-current-HP PackMark and table-driven restraint cues remain intact.
// Every admitted order now retains target plus normalized urgency. Aggro owns
// target response strength; Tempo consumes urgency through one non-stacking
// immutable-endpoint mobilization envelope per exact free uEm0200 responder.
// Ordinary evidence completion decays; unsafe identity/readiness/topology,
// timeout, disable, shutdown, rollback, or stale-world failures reset at once.

#include "stdafx.h"
#include "MonsterDirector.h"
#include "TacticalCues.h"
#include "SpeciesCard.h"
#include "PackObserve.h"
#include "../runtime/Runtime.h"
#include "../CombatBus.h"
#include "../runtime/MonsterTempo.h"
#include "../runtime/AggroWatch.h"
#include "../ActMap.Generated.h"
#include <math.h>
#include <string.h>

namespace MonsterAI {

static const int   kMaxViews       = 32;
static const DWORD kTickMs         = 150;
static const DWORD kWorldFreshMs   = 450; // three situation scans; stale fails closed
static const DWORD kDecisionMs     = 500;
static const DWORD kMinHoldMs      = 2500;
static const DWORD kHeartbeatMs    = 10000;
static const float kSwitchMargin   = 1.20f;
static const float kBiasIsolation  = 0.20f;
// Data-driven first commit boundary: in the validated fight MainPawn became a
// genuine focus opportunity at 164.8 HP versus a 331.3 HP runner (+101%).
static const float kFocusIsolation = 1.00f;

static bool        s_enabled = false;
static MonsterView s_view[kMaxViews];
static int         s_nView = 0;
static DWORD       s_lastTick = 0;
static DWORD       s_lastDecision = 0;
static DWORD       s_lastLog = 0;
static char        s_status[420] = "Monster Director: disabled";

struct TargetScore {
    bool  valid;
    bool  hpValid;
    float lowAbsoluteHp;
    float huntScore;
};

static Runtime::PartyCombatSnapshot s_party;
static Runtime::PartyCombatSnapshot s_cueParty;
static TargetScore s_score[Runtime::PARTY_COMBAT_SLOTS];
static int         s_order[Runtime::PARTY_COMBAT_SLOTS] = { -1, -1, -1, -1 };
static int         s_wolfCount = 0;
static int         s_mark = -1;
static int         s_runner = -1;
static DWORD       s_markSince = 0;
static uint64_t    s_partySignature = 0;
static bool        s_havePartySignature = false;
static int         s_mode = RECOMMEND_NONE; // advisory focus opportunity only
static int         s_lastLoggedMode = -1;
static char        s_reason[96] = "waiting";

struct TacticalRuntime {
    bool      active;
    bool      timeoutBlocked;
    bool      partialLogged;
    uint64_t  partialSignature;
    int       situation;
    int       response;
    float     urgency;
    int       targetSlot;
    uintptr_t targetBody;
    uintptr_t victimBody;
    bool      excludeVictim;
    DWORD     sinceMs;
    uint32_t  maxLeaseMs;
    uint64_t  topologySignature;
    float     pairDistanceM;
    char      targetAct[64];
    char      victimAct[64];
};
static TacticalRuntime s_tactical;
static bool             s_policyHardResetPending = false;
static char             s_policyHardResetReason[96] = {};

// Ground restraint is no longer a special observer. Its proved exact party
// actions live in TacticalCues beside every other data-driven situation.

// Every admitted Director order is an emergency. ALERT and ALARM remain
// tactically distinct in Aggro and cue lease, but both request full normalized
// mobilization from the same bounded per-body Tempo envelope.
static const int   kMaxPolicyWolves = 16; // matches Tempo's bounded table
static const float kEmergencyUrgency = 1.0f;
static const DWORD kPolicyTtlMs = 600;    // fail-safe; normal release is explicit
static bool        s_actuatorEnabled = false;
static bool        s_policyEngaged = false;
static uintptr_t   s_ownedWolf[kMaxPolicyWolves] = {};
static int         s_nOwnedWolf = 0;
static int         s_policyTarget = -1;
static uintptr_t   s_policyTargetBody = 0;
static int         s_policySituation = TACTICAL_SITUATION_NONE;
static int         s_policyResponse = TACTICAL_RESPONSE_NONE;
static float       s_policyUrgency = 0.0f;
static float       s_policyL0Lo = 0.0f, s_policyL0Hi = 0.0f;
static float       s_policyA0Lo = 0.0f, s_policyA0Hi = 0.0f;
static float       s_policyL1Lo = 0.0f, s_policyL1Hi = 0.0f;
static float       s_policyA1Lo = 0.0f, s_policyA1Hi = 0.0f;
static uintptr_t   s_policyExcludedBody = 0;
static uint64_t    s_policyEventTopology = 0;
static uintptr_t   s_responderWolf[kMaxPolicyWolves] = {};
static int         s_nResponderWolf = 0;
static int         s_gameplayWrites = 0;
static char        s_policyStatus[128] = "actuator-off";
// Release coalescing is deliberately separate from UI/status text. NONE/BIAS
// may alternate with a fail-closed reason while no row is owned; that must not
// replay actuator cleanup or print a fake gameplay transition every 150 ms.
static bool        s_inactiveResetLatched = false;
static bool        s_inactiveSafetyLatched = false;
static char        s_inactiveSafetyReason[96] = {};
static uint32_t    s_inactiveSafetySuppressed = 0;

static float Clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static const char* ModeName(int mode)
{
    if (mode == RECOMMEND_BIAS) return "BIAS";
    if (mode == RECOMMEND_FOCUS) return "FOCUS-WINDOW";
    return "NONE";
}

static bool IsWolf(const MonsterView& v)
{
    return !strcmp(v.kind, "uEm0200");
}

struct FirstSeen { uintptr_t body; DWORD ms; };
static FirstSeen s_seen[kMaxViews];
static int       s_nSeen = 0;

static DWORD FirstSeenMs(uintptr_t body, DWORD now)
{
    for (int i = 0; i < s_nSeen; ++i)
        if (s_seen[i].body == body) return s_seen[i].ms;
    if (s_nSeen < kMaxViews) {
        s_seen[s_nSeen].body = body;
        s_seen[s_nSeen].ms = now;
        ++s_nSeen;
    }
    return now;
}

static void ForgetMissing()
{
    int out = 0;
    for (int i = 0; i < s_nSeen; ++i) {
        bool alive = false;
        for (int k = 0; k < s_nView; ++k) {
            if (s_view[k].body == s_seen[i].body) {
                alive = true;
                break;
            }
        }
        if (!alive) continue;
        if (out != i) s_seen[out] = s_seen[i];
        ++out;
    }
    s_nSeen = out;
}

static void UpdateViews(DWORD now)
{
    const WorldReport w = CombatBus::Instance().LastWorld();
    s_nView = 0;
    if (!w.timestampMs || now - w.timestampMs > kWorldFreshMs) {
        ForgetMissing();
        return;
    }

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    const bool haveArisen = Runtime::GetArisenWorldPos(&ax, &ay, &az);

    for (int i = 0; i < w.count && s_nView < kMaxViews; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;

        MonsterView& v = s_view[s_nView++];
        memset(&v, 0, sizeof(v));
        v.body = u.ptr;
        lstrcpynA(v.kind, u.kind, sizeof(v.kind));
        lstrcpynA(v.act, u.actName, sizeof(v.act));
        v.dead = false; // dead bodies are excluded from WorldReport::units
        v.attacking = ActMap::NameIsAttack(v.act);
        v.x = u.x;
        v.y = u.y;
        v.z = u.z;
        v.positionValid = (v.x == v.x && v.y == v.y && v.z == v.z);
        v.distM = -1.0f;
        if (haveArisen && v.positionValid) {
            const float dx = v.x - ax;
            const float dy = v.y - ay;
            const float dz = v.z - az;
            v.distM = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
        }
        v.locoFactor = 1.0f;
        v.atkFactor = 1.0f;
        Runtime::Tempo::GetFactors(v.body, &v.locoFactor, &v.atkFactor);
        v.seenMs = (uint32_t)FirstSeenMs(v.body, now);
    }
    ForgetMissing();
}

static void TacticalRelease(const char* reason, bool timeoutBlock,
                            bool hardReset, DWORD now)
{
    if (s_tactical.active) {
        logFile << "Monster Director: situation RELEASED name="
                << TacticalSituationName(s_tactical.situation)
                << " response=" << TacticalResponseName(s_tactical.response)
                << " urgency=" << s_tactical.urgency
                << " reason=" << (reason ? reason : "unknown")
                << " actuation=" << (hardReset ? "HARD-RESET" : "DECAY")
                << " target=" << Runtime::PartyCombatSlotName(s_tactical.targetSlot)
                << " victim=0x" << std::hex << s_tactical.victimBody << std::dec
                << " dur=" << (now - s_tactical.sinceMs) << "ms"
                << std::endl;
    }
    if (hardReset) {
        s_policyHardResetPending = true;
        lstrcpynA(s_policyHardResetReason, reason ? reason : "tactical-unsafe-release",
                  sizeof(s_policyHardResetReason));
    }
    s_tactical.active = false;
    s_tactical.timeoutBlocked = timeoutBlock;
    if (!timeoutBlock) {
        s_tactical.situation = TACTICAL_SITUATION_NONE;
        s_tactical.response = TACTICAL_RESPONSE_NONE;
        s_tactical.urgency = 0.0f;
        s_tactical.targetSlot = -1;
        s_tactical.targetBody = 0;
        s_tactical.victimBody = 0;
        s_tactical.excludeVictim = false;
        s_tactical.sinceMs = 0;
        s_tactical.maxLeaseMs = 0;
        s_tactical.topologySignature = 0;
        s_tactical.pairDistanceM = -1.0f;
        s_tactical.targetAct[0] = 0;
        s_tactical.victimAct[0] = 0;
    }
}

static void TacticalEnter(const TacticalMatch& m, DWORD now,
                          uint64_t topologySignature)
{
    s_tactical.active = true;
    s_tactical.timeoutBlocked = false;
    s_tactical.partialLogged = false;
    s_tactical.partialSignature = 0;
    s_tactical.situation = m.situation;
    s_tactical.response = m.response;
    s_tactical.urgency = m.urgency;
    s_tactical.targetSlot = m.targetSlot;
    s_tactical.targetBody = m.targetBody;
    s_tactical.victimBody = m.evidenceBody;
    s_tactical.excludeVictim = m.excludeEvidenceBody;
    s_tactical.sinceMs = now;
    s_tactical.maxLeaseMs = m.maxLeaseMs;
    s_tactical.topologySignature = topologySignature;
    s_tactical.pairDistanceM = m.pairDistanceM;
    lstrcpynA(s_tactical.targetAct, m.targetAct ? m.targetAct : "?",
              sizeof(s_tactical.targetAct));
    lstrcpynA(s_tactical.victimAct, m.evidenceAct ? m.evidenceAct : "?",
              sizeof(s_tactical.victimAct));
    logFile << "Monster Director: situation ENGAGED name=" << m.name
            << " response=" << TacticalResponseName(m.response)
            << " urgency=" << m.urgency
            << " target=" << Runtime::PartyCombatSlotName(m.targetSlot)
            << " holderAct=" << (m.targetAct ? m.targetAct : "?")
            << " victim=0x" << std::hex << m.evidenceBody << std::dec
            << " victimAct=" << (m.evidenceAct ? m.evidenceAct : "?")
            << " distance=" << m.pairDistanceM << "m"
            << " leaseMax=" << m.maxLeaseMs << "ms"
            << std::endl;
}

static uint64_t TacticalPartialSignature(const TacticalScan& scan)
{
    uint64_t h = 1469598103934665603ULL;
#define TACTICAL_HASH_VALUE(v) do { h ^= (uint64_t)(v); h *= 1099511628211ULL; } while (0)
    TACTICAL_HASH_VALUE(scan.situation);
    TACTICAL_HASH_VALUE(scan.response);
    TACTICAL_HASH_VALUE(scan.targetCandidates);
    TACTICAL_HASH_VALUE(scan.evidenceCandidates);
    TACTICAL_HASH_VALUE(scan.pairCandidates);
    TACTICAL_HASH_VALUE(scan.positionRejected);
    TACTICAL_HASH_VALUE(scan.firstTargetSlot);
    TACTICAL_HASH_VALUE(scan.firstTargetBody);
    TACTICAL_HASH_VALUE(scan.firstEvidenceBody);
    const char* acts[2] = { scan.firstTargetAct, scan.firstEvidenceAct };
    for (int a = 0; a < 2; ++a) {
        const char* p = acts[a];
        if (!p) { TACTICAL_HASH_VALUE(0); continue; }
        while (*p) TACTICAL_HASH_VALUE((unsigned char)*p++);
        TACTICAL_HASH_VALUE(0xFF);
    }
#undef TACTICAL_HASH_VALUE
    return h;
}

static uint64_t TacticalEventTopology(
    const Runtime::PartyCombatSnapshot& snapshot)
{
    // Unlike strategic hold memory, a tactical event owns exact live bodies.
    // Any party record/body substitution must end that event and its lease.
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = snapshot.member[i];
        h ^= (uint64_t)m.slot; h *= 1099511628211ULL;
        h ^= (uint64_t)m.pawnRecordIdx; h *= 1099511628211ULL;
        h ^= (uint64_t)m.record; h *= 1099511628211ULL;
        h ^= (uint64_t)m.body; h *= 1099511628211ULL;
    }
    return h;
}

static bool ExactPartyIdentity(const Runtime::PartyCombatSnapshot& snapshot,
                               const char** reasonOut);

static void UpdateTacticalSituations(DWORD now)
{
    Runtime::PartyCombatSnapshot fresh;
    memset(&fresh, 0, sizeof(fresh));
    if (!Runtime::ReadPartyCombatSnapshot(&fresh)) {
        TacticalRelease("party-snapshot-unavailable", false, true, now);
        s_tactical.partialLogged = false;
        s_tactical.partialSignature = 0;
        memset(&s_cueParty, 0, sizeof(s_cueParty));
        return;
    }
    s_cueParty = fresh;

    // Observation and writes share the same occupied-exact admission. Do not
    // report a tactical event as proven when a required party/body identity
    // is incomplete or ambiguous, even though the downstream actuator
    // repeats this gate. Empty hired slots are not a missing party.
    const char* identityReason = 0;
    if (!ExactPartyIdentity(fresh, &identityReason)) {
        TacticalRelease(identityReason ? identityReason
                                       : "identity-snapshot-unavailable",
                        false, true, now);
        s_tactical.partialLogged = false;
        s_tactical.partialSignature = 0;
        return;
    }
    const uint64_t eventTopology = TacticalEventTopology(fresh);

    TacticalPartyActor party[Runtime::PARTY_COMBAT_SLOTS];
    int nParty = 0;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = fresh.member[i];
        if (!m.recordValid || !m.bodyValid || !m.body || !m.actionValid) continue;
        TacticalPartyActor& a = party[nParty++];
        a.slot = i;
        a.body = m.body;
        a.act = m.liveAct;
        a.positionValid = m.positionValid;
        a.x = m.x; a.y = m.y; a.z = m.z;
    }

    TacticalMonsterActor monsters[kMaxViews];
    int nMonster = 0;
    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        if (!v.body || v.dead || !v.act[0]) continue;
        TacticalMonsterActor& a = monsters[nMonster++];
        a.body = v.body;
        a.kind = v.kind;
        a.act = v.act;
        a.positionValid = v.positionValid;
        a.x = v.x; a.y = v.y; a.z = v.z;
    }

    TacticalScan scan;
    ScanTacticalSituations(party, nParty, monsters, nMonster, &scan);
    const bool anyEvidence = scan.targetCandidates > 0
                          || scan.evidenceCandidates > 0;

    // Strict unique spatial admission is paid once. While the same exact pair
    // remains on the same table recipe, unrelated wolves and noisy coordinates
    // cannot steal/release it. Missing identity/topology still fails hard.
    if (s_tactical.active) {
        if (s_tactical.topologySignature != eventTopology) {
            TacticalRelease("party-topology-changed", false, true, now);
        } else if (s_tactical.targetSlot < 0
                   || s_tactical.targetSlot >= Runtime::PARTY_COMBAT_SLOTS
                   || !fresh.member[s_tactical.targetSlot].actionValid) {
            TacticalRelease("holder-action-state-unavailable", false, true, now);
        } else {
            TacticalContinuation continuation;
            InspectTacticalContinuation(s_tactical.situation,
                                        s_tactical.targetBody,
                                        s_tactical.victimBody,
                                        party, nParty, monsters, nMonster,
                                        &continuation);
            if (continuation.targetActionMatched
                && continuation.evidenceKindMatched
                && continuation.evidenceActionMatched) {
                if (continuation.distanceValid)
                    s_tactical.pairDistanceM = continuation.distanceM;
                s_tactical.partialLogged = false;
                s_tactical.partialSignature = 0;
                if (now - s_tactical.sinceMs >= s_tactical.maxLeaseMs) {
                    logFile << "Monster Director: situation TIMEOUT name="
                            << TacticalSituationName(s_tactical.situation)
                            << " response=" << TacticalResponseName(s_tactical.response)
                            << " urgency=" << s_tactical.urgency
                            << " target="
                            << Runtime::PartyCombatSlotName(s_tactical.targetSlot)
                            << " victim=0x" << std::hex << s_tactical.victimBody
                            << std::dec << " max=" << s_tactical.maxLeaseMs
                            << "ms no-rearm-until-clear" << std::endl;
                    TacticalRelease("hard-timeout", true, true, now);
                }
                return;
            }

            if (!continuation.targetBodyPresent)
                TacticalRelease("holder-identity-lost", false, true, now);
            else if (!continuation.targetActionMatched)
                TacticalRelease("holder-action-ended", false, false, now);
            else if (!continuation.evidenceBodyPresent)
                TacticalRelease("victim-topology-lost", false, true, now);
            else if (!continuation.evidenceKindMatched)
                TacticalRelease("victim-species-changed", false, true, now);
            else
                TacticalRelease("victim-action-ended", false, false, now);
        }
    }

    if (!scan.matched) {
        // Timeout blocks only the same continuously correlated event. If
        // either side or spatial overlap clears, a later pair is a new event.
        if (s_tactical.timeoutBlocked
            && (scan.targetCandidates == 0 || scan.evidenceCandidates == 0
                || scan.pairCandidates == 0))
            s_tactical.timeoutBlocked = false;
        if (!anyEvidence) {
            s_tactical.partialLogged = false;
            s_tactical.partialSignature = 0;
            return;
        }
        const uint64_t partialSignature = TacticalPartialSignature(scan);
        if (!s_tactical.partialLogged
            || s_tactical.partialSignature != partialSignature) {
            const char* partialReason = "pair-not-unique";
            if (scan.targetCandidates == 0) partialReason = "holder-action-absent";
            else if (scan.targetCandidates > 1) partialReason = "holders-ambiguous";
            else if (scan.pairCandidates == 0) partialReason = "no-spatial-pair";
            else if (scan.pairCandidates > 1) partialReason = "pairs-ambiguous";
            logFile << "Monster Director: situation PARTIAL name=" << scan.name
                    << " response=" << TacticalResponseName(scan.response)
                    << " reason=" << partialReason
                    << " holders=" << scan.targetCandidates
                    << " victims=" << scan.evidenceCandidates
                    << " pairs=" << scan.pairCandidates
                    << " positionRejected=" << scan.positionRejected
                    << " holder="
                    << (scan.firstTargetSlot >= 0
                        ? Runtime::PartyCombatSlotName(scan.firstTargetSlot) : "none")
                    << " holderAct=" << (scan.firstTargetAct ? scan.firstTargetAct : "?")
                    << " victim=0x" << std::hex << scan.firstEvidenceBody << std::dec
                    << " victimAct=" << (scan.firstEvidenceAct
                                          ? scan.firstEvidenceAct : "?")
                    << " nearest=" << scan.nearestDistanceM << "m"
                    << " no-write" << std::endl;
            s_tactical.partialLogged = true;
            s_tactical.partialSignature = partialSignature;
        }
        return;
    }

    const TacticalMatch& m = scan.match;
    const bool samePair = s_tactical.situation == m.situation
                       && s_tactical.response == m.response
                       && s_tactical.targetSlot == m.targetSlot
                       && s_tactical.targetBody == m.targetBody
                       && s_tactical.victimBody == m.evidenceBody
                       && s_tactical.topologySignature == eventTopology;

    if (s_tactical.timeoutBlocked && samePair) return;
    if (s_tactical.timeoutBlocked && !samePair)
        s_tactical.timeoutBlocked = false;
    if (!s_tactical.active)
        TacticalEnter(m, now, eventTopology);
}

static uint64_t HashAdd(uint64_t h, uint64_t value)
{
    h ^= value;
    return h * 1099511628211ULL;
}

static uint64_t PartyTopologySignature(const Runtime::PartyCombatSnapshot& p)
{
    // Build 002 treated level/maxHP/core/loadout changes as party composition.
    // A real level-up therefore reset the hold. Build 003+ hashes topology only:
    // slot presence, record address and stable record index. Current HP, max HP,
    // level, stats, skills, body and position cannot reset tactical memory.
    //
    // Exact occupant identity is still unvalidated. That is acceptable for an
    // encounter-local observer: party hiring normally occurs outside a live
    // wolf encounter, and the mark is cleared when the pack disappears.
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = p.member[i];
        h = HashAdd(h, (uint64_t)(m.recordValid ? 1 : 0));
        if (!m.recordValid) continue;
        h = HashAdd(h, (uint64_t)m.slot);
        h = HashAdd(h, (uint64_t)m.pawnRecordIdx);
        h = HashAdd(h, (uint64_t)m.record);
    }
    return h;
}

static void ResetDecisionMemory(const char* reason)
{
    s_mark = -1;
    s_runner = -1;
    s_markSince = 0;
    s_mode = RECOMMEND_NONE;
    lstrcpynA(s_reason, reason ? reason : "reset", sizeof(s_reason));
}

static void LogPartyRaw(const char* event)
{
    logFile << "MD: PARTY " << (event ? event : "snapshot")
            << " policy=MOMENT-HP records=" << s_party.recordCount
            << " body/position=NATIVE-MAP-UNVALIDATED/ignored"
            << " core/loadout/skills/vocation/status/downed=ignored"
            << std::endl;

    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = s_party.member[i];
        char line[640];
        sprintf_s(line,
            "MD: raw %-8s rec=%d hpValid=%d hp=%.1f maxHp=%.1f(diag-only) "
            "body=%d pos=%d action=%d nativeIdentity=UNVALIDATED "
            "CORE/UNVALIDATED str=%.1f def=%.1f mag=%.1f mdef=%.1f "
            "loadoutTotals=UNKNOWN allNonHpInputs=ignored topologyOnlyReset=1",
            Runtime::PartyCombatSlotName(i), m.recordValid ? 1 : 0,
            m.hpValid ? 1 : 0, m.currentHp, m.maxHp,
            m.bodyValid ? 1 : 0, m.positionValid ? 1 : 0,
            m.actionValid ? 1 : 0, m.strength, m.defense,
            m.magick, m.magickDefense);
        logFile << line << std::endl;
    }
}

static void ScoreParty()
{
    memset(s_score, 0, sizeof(s_score));
    s_wolfCount = 0;
    for (int i = 0; i < s_nView; ++i)
        if (IsWolf(s_view[i])) ++s_wolfCount;

    float highestHp = 0.0f;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = s_party.member[i];
        TargetScore& q = s_score[i];
        q.hpValid = m.recordValid && m.hpValid && m.currentHp > 0.0f;
        if (q.hpValid && m.currentHp > highestHp)
            highestHp = m.currentHp;
    }

    if (highestHp <= 0.0f) return;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = s_party.member[i];
        TargetScore& q = s_score[i];
        if (!q.hpValid) continue;

        // Both values are derived from ABSOLUTE current HP. maxHp is absent.
        q.lowAbsoluteHp = Clamp01(1.0f - m.currentHp / highestHp);
        q.huntScore = highestHp / m.currentHp;
        q.valid = true;
    }
}

static void ClearPriorityOrder()
{
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) s_order[i] = -1;
}

static void BuildPriorityOrder()
{
    ClearPriorityOrder();
    bool used[Runtime::PARTY_COMBAT_SLOTS] = {};
    for (int rank = 0; rank < Runtime::PARTY_COMBAT_SLOTS; ++rank) {
        int best = -1;
        for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
            if (used[i] || !s_score[i].valid) continue;
            if (best < 0 || s_score[i].huntScore > s_score[best].huntScore)
                best = i;
        }
        if (best < 0) break;
        used[best] = true;
        s_order[rank] = best;
    }
}

static int PriorityRankOf(int slot)
{
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i)
        if (s_order[i] == slot) return i + 1;
    return 0;
}

static const char* PriorityCode(int rank)
{
    if (rank < 0 || rank >= Runtime::PARTY_COMBAT_SLOTS) return "-";
    const int slot = s_order[rank];
    if (slot == Runtime::PARTY_ARISEN) return "A";
    if (slot == Runtime::PARTY_MAIN) return "M";
    if (slot == Runtime::PARTY_HIRED1) return "H1";
    if (slot == Runtime::PARTY_HIRED2) return "H2";
    return "-";
}

static int BestScoreExcept(int except)
{
    int best = -1;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        if (i == except || !s_score[i].valid) continue;
        if (best < 0 || s_score[i].huntScore > s_score[best].huntScore)
            best = i;
    }
    return best;
}

static float IsolationFor(int mark, int runner)
{
    if (mark < 0 || !s_score[mark].valid) return 0.0f;
    if (runner < 0 || !s_score[runner].valid) return 0.0f;
    const float r = s_score[runner].huntScore;
    if (r <= 0.0001f) return 0.0f;
    // Algebraically runnerCurrentHp / markCurrentHp - 1. A negative result
    // is meaningful: hysteresis is holding a mark that is no longer raw #1.
    return (s_score[mark].huntScore - r) / r;
}

static float DepthFor(int mark)
{
    if (mark < 0 || !s_score[mark].valid) return 0.0f;
    // huntScore already is highestCurrentHp / markCurrentHp. Reuse it rather
    // than inventing another score or another memory read.
    const float depth = s_score[mark].huntScore - 1.0f;
    return depth > 0.0f ? depth : 0.0f;
}

static const char* HpText(int slot, char* out, int cap)
{
    if (!out || cap <= 0) return "?";
    if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS
        || !s_score[slot].valid) {
        lstrcpynA(out, "?", cap);
        return out;
    }
    sprintf_s(out, cap, "%.1f", s_party.member[slot].currentHp);
    return out;
}

static void LogScoreLine(int slot, const char* label)
{
    if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS) {
        logFile << "MD: " << label << "=none" << std::endl;
        return;
    }

    const Runtime::PartyCombatMember& m = s_party.member[slot];
    const TargetScore& q = s_score[slot];
    char line[640];
    sprintf_s(line,
        "MD: %-6s %-8s policy=MOMENT-HP rank=%d eligible=%d rec=%d hpValid=%d "
        "hpAbs=%.1f score=%.3f lowHpNorm=%.3f maxHp=%.1f(diag-only) "
        "body=%d pos=%d(ignored) CORE/UNVALIDATED def=%.1f mdef=%.1f(ignored) "
        "nativeNamed=UNVALIDATED actuator=%s observerOnly=%d writes=%d",
        label, Runtime::PartyCombatSlotName(slot), PriorityRankOf(slot),
        q.valid ? 1 : 0, m.recordValid ? 1 : 0, m.hpValid ? 1 : 0, m.currentHp,
        q.huntScore, q.lowAbsoluteHp, m.maxHp, m.bodyValid ? 1 : 0,
        m.positionValid ? 1 : 0, m.defense, m.magickDefense,
        s_actuatorEnabled ? "ON" : "OFF", s_actuatorEnabled ? 0 : 1,
        s_gameplayWrites);
    logFile << line << std::endl;
}

static void LogDecision(const char* event, DWORD now, bool withScores)
{
    const float isolation = IsolationFor(s_mark, s_runner);
    const float depth = DepthFor(s_mark);
    char markHp[32], runnerHp[32];
    char line[1180];
    sprintf_s(line,
        "MD: %s policy=MOMENT-HP mark=%s runner=%s rawPriority=[%s>%s>%s>%s] "
        "markHp=%s runnerHp=%s isolation=%+.1f%% targetDepth=%+.1f%% "
        "focusIntent=%s reason=%s "
        "hold=%lu/%lu ms decision=%lu ms switchMargin=20%% isolationBias=20%% isolationFocus=100%% "
        "wolves=%d hp=[A:%.1f,M:%.1f,H1:%.1f,H2:%.1f] "
        "eligible=[%d,%d,%d,%d] body/position/native=UNVALIDATED/ignored "
        "DEF/ATK/maxHP%%/skills/vocation/status/downed=ignored "
        "focus/tempo=synchronized-if-gated actuator=%s observerOnly=%d writes=%d",
        event ? event : "DECISION",
        s_mark >= 0 ? Runtime::PartyCombatSlotName(s_mark) : "none",
        s_runner >= 0 ? Runtime::PartyCombatSlotName(s_runner) : "none",
        PriorityCode(0), PriorityCode(1), PriorityCode(2), PriorityCode(3),
        HpText(s_mark, markHp, sizeof(markHp)),
        HpText(s_runner, runnerHp, sizeof(runnerHp)),
        isolation * 100.0f, depth * 100.0f, ModeName(s_mode), s_reason,
        (unsigned long)(s_markSince ? now - s_markSince : 0),
        (unsigned long)kMinHoldMs, (unsigned long)kDecisionMs,
        s_wolfCount,
        s_party.member[0].currentHp, s_party.member[1].currentHp,
        s_party.member[2].currentHp, s_party.member[3].currentHp,
        s_score[0].valid ? 1 : 0, s_score[1].valid ? 1 : 0,
        s_score[2].valid ? 1 : 0, s_score[3].valid ? 1 : 0,
        s_actuatorEnabled ? "ON" : "OFF", s_actuatorEnabled ? 0 : 1,
        s_gameplayWrites);
    logFile << line << std::endl;
    if (withScores) {
        LogScoreLine(s_mark, "mark");
        LogScoreLine(s_runner, "runner");
    }
    s_lastLog = now;
    s_lastLoggedMode = s_mode;
}

static void UpdateStatus(DWORD now)
{
    const char* actuator = s_actuatorEnabled ? "ON" : "OFF";
    const char* situation = s_tactical.active
        ? TacticalSituationName(s_tactical.situation)
        : (s_tactical.timeoutBlocked ? "timeout-blocked" : "-");
    if (!strcmp(s_reason, "party-unavailable")) {
        sprintf_s(s_status,
            "Monster Director: PackMark+tactics | party records unavailable | "
            "situation %s | actuator %s | policy %s | writes %d",
            situation, actuator, s_policyStatus, s_gameplayWrites);
        return;
    }
    if (s_wolfCount <= 0) {
        sprintf_s(s_status,
            "Monster Director: PackMark+tactics | no uEm0200 wolves | "
            "situation %s | actuator %s | policy %s | writes %d",
            situation, actuator, s_policyStatus, s_gameplayWrites);
        return;
    }
    if (s_mark < 0) {
        sprintf_s(s_status,
            "Monster Director: PackMark+tactics | no eligible positive-current-HP record | "
            "%d wolves | situation %s | actuator %s | policy %s | writes %d",
            s_wolfCount, situation, actuator, s_policyStatus, s_gameplayWrites);
        return;
    }

    char runnerHp[32];
    const float isolation = IsolationFor(s_mark, s_runner) * 100.0f;
    const float depth = DepthFor(s_mark) * 100.0f;
    sprintf_s(s_status,
        "Monster Director: PackMark+tactics | PackMark %s HP %.1f | runner %s HP %s | "
        "isolation %+.1f%% | depth %+.1f%% | focus %s (%s) | "
        "raw %s>%s>%s>%s | hold %.1f/2.5 s | situation %s | "
        "actuator %s | policy %s | writes %d",
        Runtime::PartyCombatSlotName(s_mark), s_party.member[s_mark].currentHp,
        s_runner >= 0 ? Runtime::PartyCombatSlotName(s_runner) : "none",
        HpText(s_runner, runnerHp, sizeof(runnerHp)), isolation, depth,
        ModeName(s_mode), s_reason,
        PriorityCode(0), PriorityCode(1), PriorityCode(2), PriorityCode(3),
        s_markSince ? (now - s_markSince) / 1000.0f : 0.0f,
        situation, actuator, s_policyStatus, s_gameplayWrites);
}

static void Decide(DWORD now)
{
    if (!Runtime::ReadPartyCombatSnapshot(&s_party)) {
        memset(&s_party, 0, sizeof(s_party));
        memset(s_score, 0, sizeof(s_score));
        ClearPriorityOrder();
        ResetDecisionMemory("party-unavailable");
        s_wolfCount = 0;
        UpdateStatus(now);
        return;
    }

    const uint64_t signature = PartyTopologySignature(s_party);
    if (!s_havePartySignature || signature != s_partySignature) {
        s_partySignature = signature;
        s_havePartySignature = true;
        ResetDecisionMemory("party-topology-reset");
        LogPartyRaw("record-topology-change");
    }

    ScoreParty();
    BuildPriorityOrder();
    if (s_wolfCount <= 0) {
        if (s_mark >= 0) {
            ResetDecisionMemory("wolf-pack-gone");
            LogDecision("CLEAR", now, false);
        }
        UpdateStatus(now);
        return;
    }

    const int rawBest = s_order[0];
    const int oldMark = s_mark;
    if (rawBest < 0) {
        ResetDecisionMemory("no-valid-hp-record");
    } else if (s_mark < 0 || !s_score[s_mark].valid) {
        s_mark = rawBest;
        s_markSince = now;
        lstrcpynA(s_reason, oldMark < 0 ? "initial-lowest-hp" : "current-hp-invalid",
                  sizeof(s_reason));
    } else if (rawBest == s_mark) {
        lstrcpynA(s_reason, "lowest-hp-stable", sizeof(s_reason));
    } else if (now - s_markSince < kMinHoldMs) {
        lstrcpynA(s_reason, "minimum-hold", sizeof(s_reason));
    } else {
        const float current = s_score[s_mark].huntScore;
        const float challenger = s_score[rawBest].huntScore;
        if (challenger >= current * kSwitchMargin) {
            s_mark = rawBest;
            s_markSince = now;
            lstrcpynA(s_reason, "challenger-plus-20pct", sizeof(s_reason));
        } else {
            lstrcpynA(s_reason, "switch-margin-hold", sizeof(s_reason));
        }
    }

    s_runner = BestScoreExcept(s_mark);
    const float isolation = IsolationFor(s_mark, s_runner);
    if (s_mark >= 0 && s_runner >= 0 && isolation >= kFocusIsolation)
        s_mode = RECOMMEND_FOCUS;
    else if (s_mark >= 0 && s_runner >= 0 && isolation >= kBiasIsolation)
        s_mode = RECOMMEND_BIAS;
    else
        s_mode = RECOMMEND_NONE;
    // This is a focus-opportunity band only. DepthFor(s_mark) is a separate
    // continuous output; neither signal has a consumer in this observer build.

    const bool markChanged = oldMark != s_mark;
    const bool modeChanged = s_mode != s_lastLoggedMode;
    if (markChanged) {
        LogDecision(s_mark < 0 ? "CLEAR" : (oldMark < 0 ? "SELECT" : "SWITCH"),
                    now, true);
    } else if (modeChanged) {
        LogDecision("MODE", now, true);
    } else if (!s_lastLog || now - s_lastLog >= kHeartbeatMs) {
        LogDecision("HEARTBEAT", now, false);
    }

    UpdateStatus(now);
}

static bool InactiveControlReason(const char* reason)
{
    return reason && (!strcmp(reason, "actuator-off")
                   || !strcmp(reason, "actuator-disabled")
                   || !strcmp(reason, "director-disabled")
                   || !strcmp(reason, "shutdown")
                   || !strcmp(reason, "waiting-for-intent"));
}

static void SetPolicyStatus(const char* reason, bool engaged,
                            const char* mobilization = 0)
{
    if (!reason) reason = "unknown";
    const bool changed = strcmp(s_policyStatus, reason) != 0
                      || s_policyEngaged != engaged;

    if (engaged && s_inactiveSafetyLatched) {
        logFile << "Monster Director: policy RECOVERED priorFailClosed="
                << (s_inactiveSafetyReason[0] ? s_inactiveSafetyReason : "unknown")
                << " coalesced=" << s_inactiveSafetySuppressed
                << " target=" << (s_policyTarget >= 0
                                    ? Runtime::PartyCombatSlotName(s_policyTarget)
                                    : "none")
                << " responders=" << s_nResponderWolf
                << " tempoOwned=" << s_nOwnedWolf
                << " writes=" << s_gameplayWrites << std::endl;
        s_inactiveSafetyLatched = false;
        s_inactiveSafetyReason[0] = 0;
        s_inactiveSafetySuppressed = 0;
    }
    if (engaged) s_inactiveResetLatched = false;

    s_policyEngaged = engaged;
    lstrcpynA(s_policyStatus, reason, sizeof(s_policyStatus));
    if (changed) {
        logFile << "Monster Director: policy " << (engaged ? "ENGAGED" : "RELEASED")
                << " reason=" << reason
                << " target=" << (s_policyTarget >= 0
                                    ? Runtime::PartyCombatSlotName(s_policyTarget)
                                    : "none")
                << " targetBody=0x" << std::hex << s_policyTargetBody << std::dec
                << " situation=" << TacticalSituationName(s_policySituation)
                << " response=" << TacticalResponseName(s_policyResponse)
                << " urgency=" << s_policyUrgency
                << " excluded=0x" << std::hex << s_policyExcludedBody << std::dec
                << " responders=" << s_nResponderWolf
                << " tempoOwned=" << s_nOwnedWolf
                << " mobilization=" << (mobilization ? mobilization
                                                       : (engaged ? "HOLD" : "NONE"));
        if (s_nOwnedWolf > 0) {
            logFile << " endpoints{L0=" << s_policyL0Lo << ".." << s_policyL0Hi
                    << ",A0=" << s_policyA0Lo << ".." << s_policyA0Hi
                    << ",L1=" << s_policyL1Lo << ".." << s_policyL1Hi
                    << ",A1=" << s_policyA1Lo << ".." << s_policyA1Hi << "}";
        }
        logFile << " writes=" << s_gameplayWrites << std::endl;
    }
}

static void ClearPolicyOwnershipState()
{
    memset(s_ownedWolf, 0, sizeof(s_ownedWolf));
    s_nOwnedWolf = 0;
    memset(s_responderWolf, 0, sizeof(s_responderWolf));
    s_nResponderWolf = 0;
    s_policyTarget = -1;
    s_policyTargetBody = 0;
    s_policySituation = TACTICAL_SITUATION_NONE;
    s_policyResponse = TACTICAL_RESPONSE_NONE;
    s_policyUrgency = 0.0f;
    s_policyL0Lo = s_policyL0Hi = 0.0f;
    s_policyA0Lo = s_policyA0Hi = 0.0f;
    s_policyL1Lo = s_policyL1Hi = 0.0f;
    s_policyA1Lo = s_policyA1Hi = 0.0f;
    s_policyExcludedBody = 0;
    s_policyEventTopology = 0;
}

static void ReleasePolicy(const char* reason, bool hardReset = false)
{
    if (!reason) reason = "unknown";
    const bool hadPolicyState = s_policyEngaged || s_nOwnedWolf > 0
                             || s_nResponderWolf > 0 || s_policyTarget >= 0;

    // No actuator ownership means no gameplay transition. Keep the current UI
    // reason, but coalesce ordinary NONE/BIAS churn. Unsafe cleanup executes
    // once per inactive episode, then remains latched until a real engagement
    // recovers. This also clears rows already decaying after an earlier normal
    // completion without repeatedly calling the actuator.
    if (!hadPolicyState) {
        if (hardReset && !s_inactiveResetLatched) {
            Runtime::Tempo::HardResetAllDirectorMobilization();
            Runtime::Aggro::DirectorFocusSet(
                -1, 0, 0, Runtime::Aggro::DIRECTOR_RESPONSE_NONE);
            s_inactiveResetLatched = true;
        }

        if (hardReset && !InactiveControlReason(reason)) {
            if (!s_inactiveSafetyLatched) {
                s_inactiveSafetyLatched = true;
                lstrcpynA(s_inactiveSafetyReason, reason,
                          sizeof(s_inactiveSafetyReason));
                s_inactiveSafetySuppressed = 0;
                logFile << "Monster Director: policy FAIL-CLOSED reason=" << reason
                        << " active=0 responders=0 tempoOwned=0"
                        << " mobilization=HARD-RESET-ONCE"
                        << " writes=" << s_gameplayWrites << std::endl;
            } else {
                ++s_inactiveSafetySuppressed;
            }
        }

        s_policyEngaged = false;
        lstrcpynA(s_policyStatus, reason, sizeof(s_policyStatus));
        ClearPolicyOwnershipState();
        return;
    }

    // Ordinary evidence completion releases only current owners into Tempo's
    // bounded decay. Unsafe release clears every Director-owned row, including
    // rows already decaying from a prior command. Generic overrides are never
    // touched here.
    if (hardReset) {
        Runtime::Tempo::HardResetAllDirectorMobilization();
        s_inactiveResetLatched = true;
    } else {
        for (int i = 0; i < s_nOwnedWolf; ++i)
            if (s_ownedWolf[i])
                Runtime::Tempo::ReleaseDirectorMobilization(s_ownedWolf[i]);
        // A later unsafe transition still has to clear these decaying rows.
        s_inactiveResetLatched = false;
    }
    Runtime::Aggro::DirectorFocusSet(-1, 0, 0,
                                      Runtime::Aggro::DIRECTOR_RESPONSE_NONE);
    SetPolicyStatus(reason, false, hardReset ? "HARD-RESET" : "DECAY");
    ClearPolicyOwnershipState();
}

static const char* SnapshotSlotFailure(int slot, bool recordMissing)
{
    static const char* kRecord[Runtime::PARTY_COMBAT_SLOTS] = {
        "identity-Arisen-snapshot-record-unavailable",
        "identity-MainPawn-snapshot-record-unavailable",
        "identity-Hired1-snapshot-record-unavailable",
        "identity-Hired2-snapshot-record-unavailable"
    };
    static const char* kBody[Runtime::PARTY_COMBAT_SLOTS] = {
        "identity-Arisen-snapshot-body-unresolved",
        "identity-MainPawn-snapshot-body-unresolved",
        "identity-Hired1-snapshot-body-unresolved",
        "identity-Hired2-snapshot-body-unresolved"
    };
    if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS)
        return "identity-invalid-slot";
    return recordMissing ? kRecord[slot] : kBody[slot];
}

static bool ExactPartyIdentity(const Runtime::PartyCombatSnapshot& snapshot,
                               const char** reasonOut)
{
    uintptr_t seen[Runtime::PARTY_COMBAT_SLOTS] = {};
    int nSeen = 0;
    for (int slot = 0; slot < Runtime::PARTY_COMBAT_SLOTS; ++slot) {
        // Ask Aggro's independently resolved fixed-slot bridge first so the
        // automatic policy log names the unavailable slot, not merely
        // occupied-exact. Empty hired record-unavailable is not a missing
        // party: skip, do not force exact4.
        uintptr_t resolved = 0;
        const char* bridge = Runtime::Aggro::ResolveMemberBodyStatus(slot,
                                                                     &resolved);
        if (!bridge) {
            if (reasonOut) *reasonOut = "identity-bridge-error";
            return false;
        }
        const bool skipEmptyHired =
            (slot == Runtime::PARTY_HIRED1 || slot == Runtime::PARTY_HIRED2)
            && strstr(bridge, "-record-unavailable") != 0;
        if (skipEmptyHired) continue;
        if (!strstr(bridge, "-exact")) {
            if (reasonOut) *reasonOut = bridge;
            return false;
        }

        const Runtime::PartyCombatMember& m = snapshot.member[slot];
        if (!m.recordValid) {
            if (reasonOut) *reasonOut = SnapshotSlotFailure(slot, true);
            return false;
        }
        if (!m.bodyValid || !m.body) {
            if (reasonOut) *reasonOut = SnapshotSlotFailure(slot, false);
            return false;
        }
        if (resolved != m.body) {
            if (reasonOut) *reasonOut = "identity-slot-body-mismatch";
            return false;
        }
        for (int k = 0; k < nSeen; ++k) {
            if (seen[k] == m.body) {
                if (reasonOut) *reasonOut = "identity-body-not-unique";
                return false;
            }
        }
        seen[nSeen++] = m.body;
    }
    if (reasonOut) *reasonOut = "identity-occupied-exact";
    return true;
}

static const char* PolicyResponderKind(int situation)
{
    return situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT
        ? "uEm0100" : "uEm0200";
}

static int CollectEligibleResponders(uintptr_t* out, int cap,
                                     uintptr_t excludedBody, const char* kind,
                                     const char** reasonOut)
{
    const bool goblin = kind && !strcmp(kind, "uEm0100");
    int n = 0;
    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        if (!v.body || v.dead || v.body == excludedBody) continue;
        if (!kind || strcmp(v.kind, kind) != 0) continue;
        for (int k = 0; k < n; ++k) {
            if (out[k] == v.body) {
                if (reasonOut)
                    *reasonOut = goblin ? "goblin-duplicate-body"
                                        : "wolf-pack-duplicate-body";
                return -1;
            }
        }
        if (n >= cap) {
            if (reasonOut)
                *reasonOut = goblin ? "goblin-too-large" : "wolf-pack-too-large";
            return -1;
        }
        out[n++] = v.body;
    }
    if (!n) {
        if (reasonOut)
            *reasonOut = goblin ? "goblin-no-free-responder" : "wolf-pack-lost";
        return 0;
    }
    if (reasonOut)
        *reasonOut = goblin ? "goblin-eligible" : "wolf-pack-eligible";
    return n;
}

static bool SamePack(const uintptr_t* pack, int n,
                         const uintptr_t* owned, int nOwned)
{
    if (n != nOwned) return false;
    for (int i = 0; i < n; ++i) {
        bool found = false;
        for (int k = 0; k < nOwned; ++k)
            if (pack[i] == owned[k]) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

static bool ResolvePolicyIntent(int* targetSlot, uintptr_t* targetBody,
                                uintptr_t* excludedBody, int* situation,
                                int* response, float* urgency,
                                const char** engageReason,
                                const Runtime::PartyCombatSnapshot** snapshot)
{
    if (targetSlot) *targetSlot = -1;
    if (targetBody) *targetBody = 0;
    if (excludedBody) *excludedBody = 0;
    if (situation) *situation = TACTICAL_SITUATION_NONE;
    if (response) *response = TACTICAL_RESPONSE_NONE;
    if (urgency) *urgency = 0.0f;
    if (engageReason) *engageReason = "decision-none";
    if (snapshot) *snapshot = &s_party;

    // Fast tactical interrupts outrank the slow strategic PackMark but never
    // overwrite it. A higher-priority ALARM may replace an active ALERT on the
    // next 150 ms scan; direct ALARM admission does not require prior ALERT.
    if (s_tactical.active) {
        const int slot = s_tactical.targetSlot;
        if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS) {
            if (engageReason) *engageReason = "situation-target-invalid";
            return false;
        }
        if (s_tactical.response != TACTICAL_RESPONSE_ALERT
            && s_tactical.response != TACTICAL_RESPONSE_ALARM) {
            if (engageReason) *engageReason = "situation-response-invalid";
            return false;
        }
        if (!(s_tactical.urgency == s_tactical.urgency)
            || s_tactical.urgency <= 0.0f || s_tactical.urgency > 1.0f) {
            if (engageReason) *engageReason = "situation-urgency-invalid";
            return false;
        }
        const Runtime::PartyCombatMember& m = s_cueParty.member[slot];
        if (!m.recordValid || !m.bodyValid || !m.body
            || m.body != s_tactical.targetBody) {
            if (engageReason) *engageReason = "situation-holder-identity-lost";
            return false;
        }
        if (targetSlot) *targetSlot = slot;
        if (targetBody) *targetBody = m.body;
        if (excludedBody)
            *excludedBody = s_tactical.excludeVictim ? s_tactical.victimBody : 0;
        if (situation) *situation = s_tactical.situation;
        if (response) *response = s_tactical.response;
        if (urgency) *urgency = s_tactical.urgency;
        if (engageReason)
            *engageReason = TacticalSituationPolicyReason(s_tactical.situation);
        if (snapshot) *snapshot = &s_cueParty;
        return true;
    }

    if (s_mode != RECOMMEND_FOCUS || s_mark < 0
        || s_mark >= Runtime::PARTY_COMBAT_SLOTS) {
        if (engageReason)
            *engageReason = s_mode == RECOMMEND_BIAS ? "decision-bias"
                                                     : "decision-none";
        return false;
    }
    if (!s_party.member[s_mark].body) {
        if (engageReason) *engageReason = "target-body-unavailable";
        return false;
    }
    if (targetSlot) *targetSlot = s_mark;
    if (targetBody) *targetBody = s_party.member[s_mark].body;
    if (response) *response = TACTICAL_RESPONSE_ALARM;
    if (urgency) *urgency = kEmergencyUrgency;
    if (engageReason) *engageReason = "focus-window-synchronized";
    return true;
}

static bool OrdinaryIntentCompletion(const char* reason)
{
    return reason && (!strcmp(reason, "decision-none")
                   || !strcmp(reason, "decision-bias"));
}

static void IncludeReceipt(const Runtime::Tempo::DirectorMobilizationReceipt& r,
                           bool first)
{
    if (first) {
        s_policyL0Lo = s_policyL0Hi = r.stableLoco;
        s_policyA0Lo = s_policyA0Hi = r.stableAnim;
        s_policyL1Lo = s_policyL1Hi = r.rageLoco;
        s_policyA1Lo = s_policyA1Hi = r.rageAnim;
        return;
    }
    if (r.stableLoco < s_policyL0Lo) s_policyL0Lo = r.stableLoco;
    if (r.stableLoco > s_policyL0Hi) s_policyL0Hi = r.stableLoco;
    if (r.stableAnim < s_policyA0Lo) s_policyA0Lo = r.stableAnim;
    if (r.stableAnim > s_policyA0Hi) s_policyA0Hi = r.stableAnim;
    if (r.rageLoco < s_policyL1Lo) s_policyL1Lo = r.rageLoco;
    if (r.rageLoco > s_policyL1Hi) s_policyL1Hi = r.rageLoco;
    if (r.rageAnim < s_policyA1Lo) s_policyA1Lo = r.rageAnim;
    if (r.rageAnim > s_policyA1Hi) s_policyA1Hi = r.rageAnim;
}

static void ApplyPolicies()
{
    if (s_policyHardResetPending) {
        char pending[sizeof(s_policyHardResetReason)];
        lstrcpynA(pending, s_policyHardResetReason, sizeof(pending));
        s_policyHardResetPending = false;
        s_policyHardResetReason[0] = 0;
        ReleasePolicy(pending[0] ? pending : "tactical-unsafe-release", true);
    }
    if (!s_actuatorEnabled) {
        ReleasePolicy("actuator-off", true);
        return;
    }
    if (!s_enabled) {
        ReleasePolicy("director-disabled", true);
        return;
    }

    int targetSlot = -1;
    uintptr_t targetBody = 0;
    uintptr_t excludedBody = 0;
    int situation = TACTICAL_SITUATION_NONE;
    int response = TACTICAL_RESPONSE_NONE;
    float urgency = 0.0f;
    const char* engageReason = 0;
    const Runtime::PartyCombatSnapshot* identitySnapshot = &s_party;
    if (!ResolvePolicyIntent(&targetSlot, &targetBody, &excludedBody,
                             &situation, &response, &urgency, &engageReason,
                             &identitySnapshot)) {
        const char* reason = engageReason ? engageReason : "decision-none";
        ReleasePolicy(reason, !OrdinaryIntentCompletion(reason));
        return;
    }
    if (!(urgency == urgency) || urgency <= 0.0f || urgency > 1.0f) {
        ReleasePolicy("policy-urgency-invalid", true);
        return;
    }
    const uint64_t eventTopology = situation != TACTICAL_SITUATION_NONE
                                 ? s_tactical.topologySignature : 0;

    const char* reason = 0;
    if (!identitySnapshot || !ExactPartyIdentity(*identitySnapshot, &reason)) {
        ReleasePolicy(reason ? reason : "identity-snapshot-unavailable", true);
        return;
    }

    const char* responderKind = PolicyResponderKind(situation);
    const SpeciesCard* card = FindSpeciesCard(responderKind);
    if (!card || !card->aggroWrite) {
        ReleasePolicy("species-aggro-write-denied", true);
        return;
    }

    uintptr_t responders[kMaxPolicyWolves] = {};
    const int nResponder = CollectEligibleResponders(
        responders, kMaxPolicyWolves, excludedBody, responderKind, &reason);
    if (nResponder <= 0) {
        const char* none = !strcmp(responderKind, "uEm0100")
                         ? "goblin-no-free-responder"
                         : "wolf-pack-no-free-responder";
        ReleasePolicy(excludedBody && nResponder == 0 ? none : reason, true);
        return;
    }

    const bool wantTempo = card->tempoRage;
    if (wantTempo && !Runtime::Tempo::DirectorReady(&reason)) {
        ReleasePolicy(reason, true);
        return;
    }

    if (s_policyEngaged) {
        const bool unsafeTopology = s_policyTarget != targetSlot
                                 || s_policyTargetBody != targetBody
                                 || s_policyExcludedBody != excludedBody
                                 || !SamePack(responders, nResponder,
                                              s_responderWolf,
                                              s_nResponderWolf)
                                 || (s_policySituation != TACTICAL_SITUATION_NONE
                                     && situation != TACTICAL_SITUATION_NONE
                                     && s_policyEventTopology != eventTopology);
        const bool commandTransition = s_policySituation != situation
                                    || s_policyResponse != response
                                    || s_policyEventTopology != eventTopology;
        if (unsafeTopology)
            ReleasePolicy("policy-topology-changed", true);
        else if (commandTransition)
            ReleasePolicy("policy-command-transition", false);
    }

    if (!s_policyEngaged) {
        s_policyTarget = targetSlot;
        s_policyTargetBody = targetBody;
        s_policySituation = situation;
        s_policyResponse = response;
        s_policyUrgency = urgency;
        s_policyExcludedBody = excludedBody;
        s_policyEventTopology = eventTopology;
        for (int i = 0; i < nResponder; ++i)
            s_responderWolf[s_nResponderWolf++] = responders[i];

        if (wantTempo) {
            for (int i = 0; i < nResponder; ++i) {
                Runtime::Tempo::DirectorMobilizationReceipt receipt;
                const char* tempoReason = 0;
                if (!Runtime::Tempo::AdmitDirectorMobilization(
                        responders[i], responderKind, urgency, kPolicyTtlMs,
                        &receipt, &tempoReason)) {
                    ReleasePolicy(tempoReason ? tempoReason
                                              : "tempo-mobilization-admit-failed",
                                  true);
                    return;
                }
                IncludeReceipt(receipt, s_nOwnedWolf == 0);
                s_ownedWolf[s_nOwnedWolf++] = responders[i];
                ++s_gameplayWrites;
            }
        }
    } else {
        if (urgency > s_policyUrgency) s_policyUrgency = urgency;
        // Repeated and overlapping orders refresh/maximize the one existing
        // per-body envelope; no factors are multiplied and no endpoint moves.
        if (wantTempo) {
            for (int i = 0; i < s_nOwnedWolf; ++i) {
                Runtime::Tempo::DirectorMobilizationReceipt receipt;
                const char* tempoReason = 0;
                if (!Runtime::Tempo::AdmitDirectorMobilization(
                        s_ownedWolf[i], responderKind, urgency, kPolicyTtlMs,
                        &receipt, &tempoReason)) {
                    ReleasePolicy(tempoReason ? tempoReason
                                              : "tempo-mobilization-refresh-failed",
                                  true);
                    return;
                }
                ++s_gameplayWrites;
            }
        }
    }

    const int aggroResponse = response == TACTICAL_RESPONSE_ALERT
                            ? Runtime::Aggro::DIRECTOR_RESPONSE_ALERT
                            : Runtime::Aggro::DIRECTOR_RESPONSE_ALARM;
    if (!Runtime::Aggro::DirectorFocusSet(targetSlot, targetBody, excludedBody,
                                           aggroResponse, responderKind)) {
        ReleasePolicy("aggro-focus-identity-rejected", true);
        return;
    }
    ++s_gameplayWrites;
    SetPolicyStatus(engageReason, true, "HOLD");
}

static void ResetRuntimeState(const char* reason)
{
    s_nView = 0;
    s_nSeen = 0;
    s_lastTick = 0;
    s_lastDecision = 0;
    s_lastLog = 0;
    s_lastLoggedMode = -1;
    s_havePartySignature = false;
    s_partySignature = 0;
    s_wolfCount = 0;
    memset(&s_party, 0, sizeof(s_party));
    memset(&s_cueParty, 0, sizeof(s_cueParty));
    memset(&s_tactical, 0, sizeof(s_tactical));
    s_tactical.targetSlot = -1;
    s_tactical.pairDistanceM = -1.0f;
    s_policyHardResetPending = false;
    s_policyHardResetReason[0] = 0;
    s_inactiveResetLatched = false;
    s_inactiveSafetyLatched = false;
    s_inactiveSafetyReason[0] = 0;
    s_inactiveSafetySuppressed = 0;
    memset(s_score, 0, sizeof(s_score));
    ClearPriorityOrder();
    ResetDecisionMemory(reason);
}

void Init()
{
    s_enabled = config.getBool("monsterAI", "enabled", false);
    s_actuatorEnabled = config.getBool("monsterAI", "wolfActuator", false);
    s_gameplayWrites = 0;
    s_policyEngaged = false;
    Runtime::Tempo::HardResetAllDirectorMobilization();
    memset(s_ownedWolf, 0, sizeof(s_ownedWolf));
    memset(s_responderWolf, 0, sizeof(s_responderWolf));
    s_nOwnedWolf = 0;
    s_nResponderWolf = 0;
    s_policyTarget = -1;
    s_policyTargetBody = 0;
    s_policySituation = TACTICAL_SITUATION_NONE;
    s_policyResponse = TACTICAL_RESPONSE_NONE;
    s_policyUrgency = 0.0f;
    s_policyL0Lo = s_policyL0Hi = 0.0f;
    s_policyA0Lo = s_policyA0Hi = 0.0f;
    s_policyL1Lo = s_policyL1Hi = 0.0f;
    s_policyA1Lo = s_policyA1Hi = 0.0f;
    s_policyExcludedBody = 0;
    s_policyEventTopology = 0;
    lstrcpynA(s_policyStatus, s_actuatorEnabled ? "waiting-for-intent"
                                                : "actuator-off",
              sizeof(s_policyStatus));
    ResetRuntimeState("waiting");
    Runtime::Aggro::SetObserverDemand(s_enabled);
    lstrcpynA(s_status, s_enabled
        ? "Monster Director: PackMark+tactics armed"
        : "Monster Director: disabled", sizeof(s_status));
    logFile << "Monster Director: " << (s_enabled ? "enabled" : "disabled")
            << " Build012 integrated urgency + mobilization;"
            << " decision=500ms; situationScan=150ms; hold=2500ms;"
            << " grabAlert=GrabStart/750ms/pin-only-Aggro;"
            << " goblinGrab=GrabStart|Hagaijime/4000ms/pin-only-no-tempo+empty-card-wake;"
            << " groundAlarm=Hagaijime4Feet/4000ms/independent;"
            << " liftAlarm=literal-lift/2500ms/separate;"
            << " allAdmittedUrgency=1.0; Tempo=immutable-uEm0200-endpoints/1400ms-decay;"
            << " switchMargin=20%; focusIntent=NONE/BIAS/FOCUS-WINDOW;"
            << " isolation=20%/100%; depth=highestHP/markHP-1;"
            << " tacticsActuator=" << (s_actuatorEnabled ? "ON" : "OFF")
            << " exact4+same-kind+unique-spatial-admission+sticky-exact-continuation;"
            << " observerOnly=" << (s_actuatorEnabled ? 0 : 1)
            << " writes=0" << std::endl;
    PackObserveInit();
}

void Shutdown()
{
    PackObserveShutdown();
    ReleasePolicy("shutdown", true);
    s_actuatorEnabled = false;
    s_enabled = false;
    Runtime::Aggro::SetObserverDemand(false);
    ResetRuntimeState("shutdown");
    lstrcpynA(s_status, "Monster Director: disabled", sizeof(s_status));
}

void Tick()
{
    if (!s_enabled) return;

    const DWORD now = GetTickCount();
    if (s_lastTick && now - s_lastTick < kTickMs) return;
    s_lastTick = now;

    UpdateViews(now);
    UpdateTacticalSituations(now);
    if (!s_lastDecision || now - s_lastDecision >= kDecisionMs) {
        s_lastDecision = now;
        Decide(now);
    }
    ApplyPolicies();
    UpdateStatus(now);
}

bool Enabled() { return s_enabled; }

void SetEnabled(bool on)
{
    if (s_enabled == on) return;
    if (!on) {
        ReleasePolicy("director-disabled", true);
    }
    s_enabled = on;
    Runtime::Aggro::SetObserverDemand(on);
    ResetRuntimeState(on ? "enabled-reset" : "disabled");
    lstrcpynA(s_status, on
        ? "Monster Director: PackMark+tactics armed"
        : "Monster Director: disabled", sizeof(s_status));
    logFile << "Monster Director: " << (on ? "ON" : "OFF")
            << " (Build012 PackMark+restraint+urgency-envelope; tactical memory reset; actuator="
            << (s_actuatorEnabled ? "ON" : "OFF")
            << " observerOnly=" << (s_actuatorEnabled ? 0 : 1)
            << " writes=" << s_gameplayWrites << ")" << std::endl;
}

void SetActuatorEnabled(bool on)
{
    if (s_actuatorEnabled == on) return;
    if (!on) ReleasePolicy("actuator-disabled", true);
    s_actuatorEnabled = on;
    if (on) {
        s_gameplayWrites = 0;
        // A consent/control boundary begins a fresh diagnostic episode. The
        // first operational failure after re-enable must be visible again.
        s_inactiveResetLatched = false;
        s_inactiveSafetyLatched = false;
        s_inactiveSafetyReason[0] = 0;
        s_inactiveSafetySuppressed = 0;
        SetPolicyStatus("waiting-for-intent", false);
    } else {
        SetPolicyStatus("actuator-off", false);
    }
    logFile << "Monster Director: tactics actuator " << (on ? "ON" : "OFF")
            << " (same consent switch; uEm0200 pack + uEm0100 grab pin;"
            << " GrabStart ALERT + ground/lift ALARM + PackMark;"
            << " occupied-exact; every admitted order urgency=1.0;"
            << " immutable rage endpoints + automatic decay)" << std::endl;
}

bool ActuatorEnabled() { return s_actuatorEnabled; }
bool PolicyEngaged() { return s_policyEngaged; }
const char* PolicyStatus() { return s_policyStatus; }

int ViewCount() { return s_nView; }

const MonsterView* ViewAt(int i)
{
    if (i < 0 || i >= s_nView) return 0;
    return &s_view[i];
}

const char* Status() { return s_status; }

bool HuntTelemetryAt(int slot, HuntTelemetry* out)
{
    if (!out || slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS) return false;
    memset(out, 0, sizeof(*out));
    const Runtime::PartyCombatMember& m = s_party.member[slot];
    const TargetScore& q = s_score[slot];

    out->slot = slot;
    out->priorityRank = PriorityRankOf(slot);
    out->recordValid = m.recordValid;
    out->hpValid = m.hpValid;
    out->scoreValid = q.valid;
    out->bodyValid = m.bodyValid;
    out->positionValid = m.positionValid;
    out->coreStatsValid = m.statsValid;
    out->currentHp = m.currentHp;
    out->maxHp = m.maxHp;
    out->coreStrength = m.strength;
    out->coreDefense = m.defense;
    out->coreMagick = m.magick;
    out->coreMagickDefense = m.magickDefense;
    out->lowAbsoluteHp = q.lowAbsoluteHp;
    out->huntScore = q.huntScore;
    out->nearWolfCount = s_wolfCount;
    return m.recordValid;
}

int PackMarkSlot() { return s_mark; }
int RunnerUpSlot() { return s_runner; }
int PrioritySlot(int rank)
{
    if (rank < 0 || rank >= Runtime::PARTY_COMBAT_SLOTS) return -1;
    return s_order[rank];
}
int Recommendation() { return s_mode; }
const char* RecommendationName() { return ModeName(s_mode); }
float TargetIsolationRatio() { return IsolationFor(s_mark, s_runner); }
float TargetDepthRatio() { return DepthFor(s_mark); }
int ScoredWolfCount() { return s_wolfCount; }
int GameplayWriteCount() { return s_gameplayWrites; }

uint32_t HoldRemainingMs()
{
    if (s_mark < 0 || !s_markSince) return 0;
    const DWORD elapsed = GetTickCount() - s_markSince;
    return elapsed >= kMinHoldMs ? 0 : (uint32_t)(kMinHoldMs - elapsed);
}

void DumpSnapshot()
{
    const DWORD now = GetTickCount();
    UpdateViews(now);
    if (!Runtime::ReadPartyCombatSnapshot(&s_party)) {
        memset(&s_party, 0, sizeof(s_party));
        memset(s_score, 0, sizeof(s_score));
        ClearPriorityOrder();
        s_wolfCount = 0;
    } else {
        ScoreParty();
        BuildPriorityOrder();
    }

    logFile << "Monster Director: ===== manual Build012 urgency+mobilization snapshot ====="
            << " enabled=" << (s_enabled ? 1 : 0)
            << " actuator=" << (s_actuatorEnabled ? 1 : 0)
            << " observerOnly=" << (s_actuatorEnabled ? 0 : 1)
            << " writes=" << s_gameplayWrites << " enemies=" << s_nView
            << " wolves=" << s_wolfCount
            << " situation=" << (s_tactical.active
                                  ? TacticalSituationName(s_tactical.situation) : "-")
            << " response=" << (s_tactical.active
                                 ? TacticalResponseName(s_tactical.response) : "NONE")
            << " urgency=" << s_policyUrgency
            << " responders=" << s_nResponderWolf
            << " tempoOwned=" << s_nOwnedWolf
            << " tempoTracks=" << Runtime::Tempo::DirectorMobilizationCount()
            << " endpoints{L0=" << s_policyL0Lo << ".." << s_policyL0Hi
            << ",A0=" << s_policyA0Lo << ".." << s_policyA0Hi
            << ",L1=" << s_policyL1Lo << ".." << s_policyL1Hi
            << ",A1=" << s_policyA1Lo << ".." << s_policyA1Hi << "}"
            << " policy=" << s_policyStatus
            << " strategicBody/position/native=ignored"
            << " tacticalStrictAdmission+stickyExactContinuation"
            << std::endl;
    LogPartyRaw("manual");
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i)
        LogScoreLine(i, "party");
    LogDecision("MANUAL", now, false);

    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        char line[300];
        sprintf_s(line,
            "MD: enemy %s @%p act=%s attacking=%d distArisen=%.2f "
            "loco=%.3f atk=%.3f wolfObserved=%d strategicSpatial=ignored tacticalPairSpatial=observed",
            v.kind, (void*)v.body, v.act[0] ? v.act : "?",
            v.attacking ? 1 : 0, v.distM, v.locoFactor, v.atkFactor,
            IsWolf(v) ? 1 : 0);
        logFile << line << std::endl;
    }
    PackObserveDump();
    logFile << "Monster Director: ===== end snapshot =====" << std::endl;
}

} // namespace MonsterAI

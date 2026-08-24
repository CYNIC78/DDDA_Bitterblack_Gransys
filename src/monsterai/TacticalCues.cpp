// Data-only tactical cue matching. No writes, hooks, configuration, or UI.
#include "stdafx.h"
#include "TacticalCues.h"
#include <math.h>
#include <string.h>

namespace MonsterAI {
namespace {

// Literal lift/carry remains distinct from body-weight ground restraint.
static const char* const kPackLiftRescueHolderActs[] = {
    "cPlActLiftBeginSmall",
    "cPlActLiftGeneric",
    "cPlActLiftRun",
    "cPlActLiftWalk",
    "cPlActLiftJump"
};
static const char* const kPackLiftRescueVictimActs[] = {
    "cEm0200Lifted"
};

// Live Build 010 evidence established the two ground-restraint phases:
// GrabStart is an attempt/precursor; Hagaijime4Feet is the acting pawn's
// sustained successful body-weight pin. They are independent action signals
// with different response strengths, not a hard-coded temporal state machine.
static const char* const kPackGrabAlertActs[] = {
    "cPlActGrabStart"
};
static const char* const kPackGroundPinAlarmActs[] = {
    "cPlActHagaijime4Feet"
};
// Night-shore log 23: the pawn leaves GrabStart for cPlActHagaijime
// (not the wolf 4-feet pin). Same opportunist ALERT, longer hold.
static const char* const kGoblinGrabAlertActs[] = {
    "cPlActGrabStart",
    "cPlActHagaijime"
};

struct TacticalRule {
    int       situation;
    const char* name;
    const char* policyReason;
    int       priority;
    int       response;
    float     urgency;
    const char* monsterKind;
    const char* const* targetActs;
    int       targetActCount;
    // Empty evidence action set means any current action of the exact kind.
    // This is used when the party action itself proves the interaction while
    // spatial uniqueness resolves the exact restrained body for exclusion.
    const char* const* evidenceActs;
    int       evidenceActCount;
    bool      requireGloballyUniqueEvidence;
    float     maxPairDistanceM;
    uint32_t  maxLeaseMs;
    bool      excludeEvidenceBody;
};

// Adding another proved species/restraint is one row plus its action array.
// Aggro write admission remains independently species-specific downstream.
static const TacticalRule kRules[] = {
    {
        TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM,
        "PACK-GROUND-PIN-ALARM",
        "tactical-ground-pin-alarm",
        200,
        TACTICAL_RESPONSE_ALARM,
        1.0f,
        "uEm0200",
        kPackGroundPinAlarmActs,
        (int)(sizeof(kPackGroundPinAlarmActs) /
              sizeof(kPackGroundPinAlarmActs[0])),
        0, 0,
        false,
        2.00f,
        4000,
        true
    },
    {
        TACTICAL_SITUATION_PACK_LIFT_RESCUE,
        "PACK-LIFT-RESCUE",
        "tactical-pack-lift-rescue",
        150,
        TACTICAL_RESPONSE_ALARM,
        1.0f,
        "uEm0200",
        kPackLiftRescueHolderActs,
        (int)(sizeof(kPackLiftRescueHolderActs) /
              sizeof(kPackLiftRescueHolderActs[0])),
        kPackLiftRescueVictimActs,
        (int)(sizeof(kPackLiftRescueVictimActs) /
              sizeof(kPackLiftRescueVictimActs[0])),
        true,
        2.50f,
        2500,
        true
    },
    {
        TACTICAL_SITUATION_PACK_GRAB_ALERT,
        "PACK-GRAB-ALERT",
        "tactical-grab-alert",
        100,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0200",
        kPackGrabAlertActs,
        (int)(sizeof(kPackGrabAlertActs) / sizeof(kPackGrabAlertActs[0])),
        0, 0,
        false,
        2.00f,
        750,
        true
    },
    {
        TACTICAL_SITUATION_GOBLIN_GRAB_ALERT,
        "GOBLIN-GRAB-ALERT",
        "tactical-goblin-grab-alert",
        90,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0100",
        kGoblinGrabAlertActs,
        (int)(sizeof(kGoblinGrabAlertActs) / sizeof(kGoblinGrabAlertActs[0])),
        0, 0,
        false,
        2.00f,
        4000,
        true
    }
};

static bool ExactAction(const char* act, const char* const* accepted, int count)
{
    if (!act || !act[0] || !accepted || count <= 0) return false;
    for (int i = 0; i < count; ++i)
        if (!strcmp(act, accepted[i])) return true;
    return false;
}

static bool EvidenceAction(const char* act, const TacticalRule& rule)
{
    if (!act || !act[0]) return false;
    if (rule.evidenceActCount <= 0) return true;
    return ExactAction(act, rule.evidenceActs, rule.evidenceActCount);
}

static bool ExactKind(const char* kind, const char* expected)
{
    return kind && expected && !strcmp(kind, expected);
}

static bool DistanceM(const TacticalPartyActor& p, const TacticalMonsterActor& m,
                      float* out)
{
    if (out) *out = -1.0f;
    if (!p.positionValid || !m.positionValid) return false;
    const float dx = p.x - m.x;
    const float dy = p.y - m.y;
    const float dz = p.z - m.z;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
    if (!(d == d) || d < 0.0f || d > 100000.0f) return false;
    if (out) *out = d;
    return true;
}

static void InitScan(TacticalScan* out)
{
    memset(out, 0, sizeof(*out));
    out->situation = TACTICAL_SITUATION_NONE;
    out->name = "NONE";
    out->response = TACTICAL_RESPONSE_NONE;
    out->firstTargetSlot = -1;
    out->nearestDistanceM = -1.0f;
    out->match.situation = TACTICAL_SITUATION_NONE;
    out->match.response = TACTICAL_RESPONSE_NONE;
    out->match.targetSlot = -1;
    out->match.pairDistanceM = -1.0f;
}

static const TacticalRule* FindRule(int situation)
{
    for (int i = 0; i < (int)(sizeof(kRules) / sizeof(kRules[0])); ++i)
        if (kRules[i].situation == situation) return &kRules[i];
    return 0;
}

} // namespace

const char* TacticalSituationName(int situation)
{
    const TacticalRule* rule = FindRule(situation);
    return rule ? rule->name : "NONE";
}

const char* TacticalSituationPolicyReason(int situation)
{
    const TacticalRule* rule = FindRule(situation);
    return rule ? rule->policyReason : "tactical-none";
}

const char* TacticalResponseName(int response)
{
    if (response == TACTICAL_RESPONSE_ALERT) return "ALERT";
    if (response == TACTICAL_RESPONSE_ALARM) return "ALARM";
    return "NONE";
}

void InspectTacticalContinuation(int situation, uintptr_t targetBody,
                                 uintptr_t evidenceBody,
                                 const TacticalPartyActor* party, int partyCount,
                                 const TacticalMonsterActor* monsters, int monsterCount,
                                 TacticalContinuation* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->distanceM = -1.0f;
    const TacticalRule* rule = FindRule(situation);
    if (!rule || !targetBody || !evidenceBody || !party || partyCount <= 0
        || !monsters || monsterCount <= 0) return;

    const TacticalPartyActor* target = 0;
    const TacticalMonsterActor* evidence = 0;
    for (int i = 0; i < partyCount; ++i) {
        if (party[i].body != targetBody) continue;
        out->targetBodyPresent = true;
        if (ExactAction(party[i].act, rule->targetActs, rule->targetActCount)) {
            out->targetActionMatched = true;
            target = &party[i];
        }
        break;
    }
    for (int i = 0; i < monsterCount; ++i) {
        if (monsters[i].body != evidenceBody) continue;
        out->evidenceBodyPresent = true;
        out->evidenceKindMatched = ExactKind(monsters[i].kind, rule->monsterKind);
        out->evidenceActionMatched = out->evidenceKindMatched
                                  && EvidenceAction(monsters[i].act, *rule);
        evidence = &monsters[i];
        break;
    }
    if (target && evidence)
        out->distanceValid = DistanceM(*target, *evidence, &out->distanceM);
}

void ScanTacticalSituations(const TacticalPartyActor* party, int partyCount,
                            const TacticalMonsterActor* monsters, int monsterCount,
                            TacticalScan* out)
{
    if (!out) return;
    InitScan(out);
    if (!party || partyCount <= 0 || !monsters || monsterCount <= 0) return;

    TacticalScan bestDiagnostic;
    TacticalScan bestMatchScan;
    InitScan(&bestDiagnostic);
    InitScan(&bestMatchScan);
    int bestDiagnosticPriority = -1;
    int bestMatchPriority = -1;

    for (int r = 0; r < (int)(sizeof(kRules) / sizeof(kRules[0])); ++r) {
        const TacticalRule& rule = kRules[r];
        TacticalScan diag;
        InitScan(&diag);
        diag.situation = rule.situation;
        diag.name = rule.name;
        diag.response = rule.response;

        for (int p = 0; p < partyCount; ++p) {
            const TacticalPartyActor& target = party[p];
            if (!target.body
                || !ExactAction(target.act, rule.targetActs, rule.targetActCount))
                continue;
            ++diag.targetCandidates;
            if (!diag.firstTargetBody) {
                diag.firstTargetSlot = target.slot;
                diag.firstTargetBody = target.body;
                diag.firstTargetAct = target.act;
            }
        }

        for (int m = 0; m < monsterCount; ++m) {
            const TacticalMonsterActor& evidence = monsters[m];
            if (!evidence.body || !ExactKind(evidence.kind, rule.monsterKind)
                || !EvidenceAction(evidence.act, rule))
                continue;
            ++diag.evidenceCandidates;
            if (!diag.firstEvidenceBody) {
                diag.firstEvidenceBody = evidence.body;
                diag.firstEvidenceAct = evidence.act;
            }
        }

        // The party actor must always be unique. For action-proved restraint,
        // many same-kind monsters may exist in the fight; exactly one may be
        // spatially correlated with the acting pawn. Literal lift additionally
        // keeps its stricter globally unique victim-action requirement.
        for (int p = 0; p < partyCount; ++p) {
            const TacticalPartyActor& target = party[p];
            if (!target.body
                || !ExactAction(target.act, rule.targetActs, rule.targetActCount))
                continue;

            for (int m = 0; m < monsterCount; ++m) {
                const TacticalMonsterActor& evidence = monsters[m];
                if (!evidence.body || !ExactKind(evidence.kind, rule.monsterKind)
                    || !EvidenceAction(evidence.act, rule))
                    continue;

                float distance = -1.0f;
                if (!DistanceM(target, evidence, &distance)) {
                    ++diag.positionRejected;
                    continue;
                }
                if (diag.nearestDistanceM < 0.0f || distance < diag.nearestDistanceM)
                    diag.nearestDistanceM = distance;
                if (distance > rule.maxPairDistanceM) continue;
                ++diag.pairCandidates;

                const bool evidenceIdentityAllowed =
                    !rule.requireGloballyUniqueEvidence
                    || diag.evidenceCandidates == 1;
                if (diag.targetCandidates == 1 && evidenceIdentityAllowed) {
                    diag.match.situation = rule.situation;
                    diag.match.name = rule.name;
                    diag.match.policyReason = rule.policyReason;
                    diag.match.priority = rule.priority;
                    diag.match.response = rule.response;
                    diag.match.urgency = rule.urgency;
                    diag.match.targetSlot = target.slot;
                    diag.match.targetBody = target.body;
                    diag.match.evidenceBody = evidence.body;
                    diag.match.targetAct = target.act;
                    diag.match.evidenceAct = evidence.act;
                    diag.match.pairDistanceM = distance;
                    diag.match.maxLeaseMs = rule.maxLeaseMs;
                    diag.match.excludeEvidenceBody = rule.excludeEvidenceBody;
                }
            }
        }

        // A holder action alone is not a diagnostic. Wolf GrabStart must not
        // hide the goblin row when no uEm0200 is present (log 23 PARTIAL).
        if (diag.targetCandidates > 0 && diag.evidenceCandidates > 0
            && rule.priority > bestDiagnosticPriority) {
            bestDiagnostic = diag;
            bestDiagnosticPriority = rule.priority;
        }

        const bool evidenceIdentityAllowed =
            !rule.requireGloballyUniqueEvidence || diag.evidenceCandidates == 1;
        const bool uniqueCorrelatedPair = diag.targetCandidates == 1
                                       && evidenceIdentityAllowed
                                       && diag.pairCandidates == 1
                                       && diag.match.situation
                                          != TACTICAL_SITUATION_NONE;
        if (uniqueCorrelatedPair && rule.priority > bestMatchPriority) {
            bestMatchScan = diag;
            bestMatchPriority = rule.priority;
        }
    }

    if (bestMatchPriority >= 0) {
        *out = bestMatchScan;
        out->matched = true;
    } else if (bestDiagnosticPriority >= 0) {
        *out = bestDiagnostic;
        out->matched = false;
    }
}

} // namespace MonsterAI

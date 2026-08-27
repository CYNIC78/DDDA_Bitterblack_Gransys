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

// Proactive caller acts: callers are vulnerable while blowing horn or howling.
static const char* const kGoblinHornCallerActs[] = {
    "cEm0100ActHornReinforce",
    "cEm0100ActHornTensionUp"
};
static const char* const kWolfHowlCallerActs[] = {
    "cEm0200Howling"
};
static const char* const kSaurianHowlCallerActs[] = {
    "cEm0400ActFriendHowl"
};

// Player chanting / casting acts: long incantation stances vulnerable to harass.
static const char* const kPlayerCasterActs[] = {
    "cPlActWpnWandBase",
    "cPlActWpnMagicBow",
    "cPlActWpnMagicShieldBase",
    "cPlActWpnSwordCstmMadouBase",
    "cPlActWpnDaggerCstmMadouBase"
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
    bool      arisenOnly;
    bool      casterVocationOnly;
};

static bool IsCasterVocation(int v)
{
    // 3 = Mage, 9 = Sorcerer (pure blue)
    // 4 = Mystic Knight, 6 = Magick Archer (hybrid blue)
    return v == 3 || v == 9 || v == 4 || v == 6;
}

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
        true,
        false, false
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
        true,
        false, false
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
        true,
        false, false
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
        true,
        false, false
    },
    {
        TACTICAL_SITUATION_HOB_GRAB_ALERT,
        "HOB-GRAB-ALERT",
        "tactical-hob-grab-alert",
        88,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0101",
        kGoblinGrabAlertActs,
        (int)(sizeof(kGoblinGrabAlertActs) / sizeof(kGoblinGrabAlertActs[0])),
        0, 0,
        false,
        2.00f,
        4000,
        true,
        false, false
    },
    {
        TACTICAL_SITUATION_GOB_HORN_ALERT,
        "GOB-HORN-ALERT",
        "tactical-gob-horn-alert",
        85,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0100",
        0, 0,
        kGoblinHornCallerActs,
        (int)(sizeof(kGoblinHornCallerActs) / sizeof(kGoblinHornCallerActs[0])),
        false,
        12.00f,
        4500,
        true,
        false, false
    },
    {
        TACTICAL_SITUATION_HOB_HORN_ALERT,
        "HOB-HORN-ALERT",
        "tactical-hob-horn-alert",
        84,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0101",
        0, 0,
        kGoblinHornCallerActs,
        (int)(sizeof(kGoblinHornCallerActs) / sizeof(kGoblinHornCallerActs[0])),
        false,
        12.00f,
        4500,
        true,
        false, false
    },
    {
        TACTICAL_SITUATION_WOLF_HOWL_ALERT,
        "WOLF-HOWL-ALERT",
        "tactical-wolf-howl-alert",
        80,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0200",
        0, 0,
        kWolfHowlCallerActs,
        (int)(sizeof(kWolfHowlCallerActs) / sizeof(kWolfHowlCallerActs[0])),
        false,
        14.00f,
        3500,
        true,
        false, false
    },
    {
        TACTICAL_SITUATION_SAURIAN_HOWL_ALERT,
        "SAURIAN-HOWL-ALERT",
        "tactical-saurian-howl-alert",
        78,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0400",
        0, 0,
        kSaurianHowlCallerActs,
        (int)(sizeof(kSaurianHowlCallerActs) / sizeof(kSaurianHowlCallerActs[0])),
        false,
        10.00f,
        4000,
        true,
        false, false
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        75,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0100",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        12.00f,
        3500,
        false,
        true, true
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        75,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0200",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        14.00f,
        3500,
        false,
        true, true
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        74,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0101",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        12.00f,
        3500,
        false,
        true, true
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        73,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0400",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        10.00f,
        3500,
        false,
        true, true
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
        if (rule->arisenOnly && party[i].slot != 0) continue;
        if (rule->casterVocationOnly && !IsCasterVocation(party[i].vocation)) continue;
        if (rule->targetActCount <= 0
            || ExactAction(party[i].act, rule->targetActs, rule->targetActCount)) {
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

        const bool proactiveCaller = (rule.targetActCount <= 0);

        if (proactiveCaller) {
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

            if (diag.evidenceCandidates > 0) {
                float bestDist = -1.0f;
                int bestTargetIdx = -1;
                uintptr_t bestCallerBody = 0;
                const char* bestCallerAct = 0;

                for (int m = 0; m < monsterCount; ++m) {
                    const TacticalMonsterActor& evidence = monsters[m];
                    if (!evidence.body || !ExactKind(evidence.kind, rule.monsterKind)
                        || !EvidenceAction(evidence.act, rule))
                        continue;

                    for (int p = 0; p < partyCount; ++p) {
                        const TacticalPartyActor& target = party[p];
                        if (!target.body) continue;
                        float distance = -1.0f;
                        if (!DistanceM(target, evidence, &distance)) {
                            ++diag.positionRejected;
                            continue;
                        }
                        if (diag.nearestDistanceM < 0.0f || distance < diag.nearestDistanceM)
                            diag.nearestDistanceM = distance;
                        if (distance > rule.maxPairDistanceM) continue;

                        if (bestDist < 0.0f || distance < bestDist) {
                            bestDist = distance;
                            bestTargetIdx = p;
                            bestCallerBody = evidence.body;
                            bestCallerAct = evidence.act;
                        }
                    }
                }

                if (bestTargetIdx >= 0) {
                    const TacticalPartyActor& target = party[bestTargetIdx];
                    diag.targetCandidates = 1;
                    diag.pairCandidates = 1;
                    diag.firstTargetSlot = target.slot;
                    diag.firstTargetBody = target.body;
                    diag.firstTargetAct = target.act;

                    diag.match.situation = rule.situation;
                    diag.match.name = rule.name;
                    diag.match.policyReason = rule.policyReason;
                    diag.match.priority = rule.priority;
                    diag.match.response = rule.response;
                    diag.match.urgency = rule.urgency;
                    diag.match.targetSlot = target.slot;
                    diag.match.targetBody = target.body;
                    diag.match.evidenceBody = bestCallerBody;
                    diag.match.targetAct = target.act;
                    diag.match.evidenceAct = bestCallerAct;
                    diag.match.pairDistanceM = bestDist;
                    diag.match.maxLeaseMs = rule.maxLeaseMs;
                    diag.match.excludeEvidenceBody = rule.excludeEvidenceBody;
                    diag.match.responderKind = rule.monsterKind;
                }
            }
        } else {
            for (int p = 0; p < partyCount; ++p) {
                const TacticalPartyActor& target = party[p];
                if (!target.body) continue;
                if (rule.arisenOnly && target.slot != 0) continue;
                if (rule.casterVocationOnly && !IsCasterVocation(target.vocation)) continue;
                if (!ExactAction(target.act, rule.targetActs, rule.targetActCount))
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
                if (!target.body) continue;
                if (rule.arisenOnly && target.slot != 0) continue;
                if (rule.casterVocationOnly && !IsCasterVocation(target.vocation)) continue;
                if (!ExactAction(target.act, rule.targetActs, rule.targetActCount))
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
                        diag.match.responderKind = rule.monsterKind;
                    }
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

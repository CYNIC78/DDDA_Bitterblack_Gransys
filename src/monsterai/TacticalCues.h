#pragma once
// Small, data-driven tactical cue matcher used by Monster Director.
//
// A rule maps exact live actions to a tactical response. The matcher owns no
// Aggro/Tempo state and performs no writes. It only resolves roles: which exact
// party body caused the cue, which exact same-kind monster is involved, which
// response tier is requested, and for how long the evidence may be leased.
// New proved restraint families are added as table rows, not Director branches.

#include <stdint.h>

namespace MonsterAI {

enum TacticalSituationId {
    TACTICAL_SITUATION_NONE = 0,
    TACTICAL_SITUATION_PACK_LIFT_RESCUE = 1,
    TACTICAL_SITUATION_PACK_GRAB_ALERT = 2,
    TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM = 3,
    TACTICAL_SITUATION_GOBLIN_GRAB_ALERT = 4,
    TACTICAL_SITUATION_HOB_GRAB_ALERT = 5,
    TACTICAL_SITUATION_GOB_HORN_ALERT = 6,
    TACTICAL_SITUATION_HOB_HORN_ALERT = 7,
    TACTICAL_SITUATION_WOLF_HOWL_ALERT = 8,
    TACTICAL_SITUATION_SAURIAN_HOWL_ALERT = 9,
    TACTICAL_SITUATION_PLAYER_CHANT_HARASS = 10
};

// ALERT and ALARM keep distinct Aggro bundles and evidence leases. Both are
// emergency Director orders and therefore request normalized urgency 1.0.
enum TacticalResponseLevel {
    TACTICAL_RESPONSE_NONE = 0,
    TACTICAL_RESPONSE_ALERT = 1,
    TACTICAL_RESPONSE_ALARM = 2
};

struct TacticalPartyActor {
    int       slot;
    uintptr_t body;
    const char* act;
    bool      positionValid;
    float     x, y, z;
    int       vocation;
};

struct TacticalMonsterActor {
    uintptr_t body;
    const char* kind;
    const char* act;
    bool      positionValid;
    float     x, y, z;
};

struct TacticalMatch {
    int       situation;
    const char* name;
    const char* policyReason;
    int       priority;
    int       response;
    float     urgency;
    int       targetSlot;
    uintptr_t targetBody;
    uintptr_t evidenceBody;
    const char* targetAct;
    const char* evidenceAct;
    float     pairDistanceM;
    uint32_t  maxLeaseMs;
    bool      excludeEvidenceBody;
    const char* responderKind;
};

// Diagnostics are transition-logged by Monster Director. Counts expose why a
// rule did not admit without producing frame-by-frame telemetry.
struct TacticalScan {
    bool      matched;
    int       situation;
    const char* name;
    int       response;
    int       targetCandidates;
    int       evidenceCandidates;
    int       pairCandidates;
    int       positionRejected;
    int       firstTargetSlot;
    uintptr_t firstTargetBody;
    uintptr_t firstEvidenceBody;
    const char* firstTargetAct;
    const char* firstEvidenceAct;
    float     nearestDistanceM;
    TacticalMatch match;
};

// Continuation never admits a new pair. It only proves that the two exact
// bodies admitted by a prior strict unique-spatial match still satisfy their
// recipe. Distance and unrelated candidates become diagnostic after admission.
struct TacticalContinuation {
    bool  targetBodyPresent;
    bool  targetActionMatched;
    bool  evidenceBodyPresent;
    bool  evidenceKindMatched;
    bool  evidenceActionMatched;
    bool  distanceValid;
    float distanceM;
};

void InspectTacticalContinuation(int situation, uintptr_t targetBody,
                                 uintptr_t evidenceBody,
                                 const TacticalPartyActor* party, int partyCount,
                                 const TacticalMonsterActor* monsters, int monsterCount,
                                 TacticalContinuation* out);

void ScanTacticalSituations(const TacticalPartyActor* party, int partyCount,
                            const TacticalMonsterActor* monsters, int monsterCount,
                            TacticalScan* out);
const char* TacticalSituationName(int situation);
const char* TacticalResponseName(int response);
const char* TacticalSituationPolicyReason(int situation);

} // namespace MonsterAI

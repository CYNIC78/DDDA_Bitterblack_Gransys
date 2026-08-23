#pragma once
// Monster Director — Build 012 (targeted urgency + responder mobilization).
//
// The Build 004 HP-only PackMark and Build 011 universal restraint recipes stay
// intact. Every admitted command now retains an exact target plus normalized
// urgency. Aggro consumes the target with distinct ALERT/ALARM strength, while
// Tempo owns one bounded stable→personal-rage envelope for every exact free
// responder. Tactical intent outranks but never overwrites PackMark memory;
// confirmed absolute current HP remains the only strategic ranking input.

#include <stdint.h>
#include <stddef.h>

namespace MonsterAI {

struct MonsterView {
    uintptr_t body;
    char      kind[24];
    char      act[64];
    float     x, y, z;
    float     distM;
    float     locoFactor;
    float     atkFactor;
    bool      positionValid;
    bool      attacking;
    bool      dead;
    uint32_t  seenMs;
};

enum RecommendationLevel {
    RECOMMEND_NONE  = 0,
    RECOMMEND_BIAS  = 1,
    // Actuated only by the explicit Director pilot after all safety gates.
    RECOMMEND_FOCUS = 2
};

struct HuntTelemetry {
    int   slot;
    int   priorityRank;          // 1..4 among eligible records; 0 = ineligible
    bool  recordValid;
    bool  hpValid;
    bool  scoreValid;

    // Diagnostics only. Build 004 neither requires nor scores these channels.
    bool  bodyValid;
    bool  positionValid;
    bool  coreStatsValid;
    float currentHp;
    float maxHp;                 // diagnostic only; no HP percentage
    float coreStrength;          // CORE/UNVALIDATED, ignored
    float coreDefense;           // CORE/UNVALIDATED, ignored
    float coreMagick;            // CORE/UNVALIDATED, ignored
    float coreMagickDefense;     // CORE/UNVALIDATED, ignored

    // The sole score is relative vulnerability = highest current HP / current
    // HP. It is monotonic in absolute current HP and never uses max HP.
    float lowAbsoluteHp;         // normalized diagnostic, absolute-HP based
    float huntScore;
    int   nearWolfCount;
};

void Init();
void Shutdown();
void Tick();

bool Enabled();
void SetEnabled(bool on);

// Live actuator is a separate consent switch and defaults OFF even when the
// observer is enabled. Disabling either switch synchronously releases all
// Director-owned Aggro and per-body Tempo state.
void SetActuatorEnabled(bool on);
bool ActuatorEnabled();
bool PolicyEngaged();
const char* PolicyStatus();

int ViewCount();
const MonsterView* ViewAt(int i);
const char* Status();
void DumpSnapshot();

bool HuntTelemetryAt(int slot, HuntTelemetry* out);
int PackMarkSlot();
int RunnerUpSlot();
// rank is zero-based: 0 = raw lowest absolute HP, independently of hold.
int PrioritySlot(int rank);
int Recommendation();
const char* RecommendationName();
// Ratios, not percentages. Isolation = runnerHP / markHP - 1 and may be
// negative while hysteresis holds an older mark. Depth = highestHP / markHP - 1
// and is always non-negative for a valid mark. Both use current absolute HP.
float TargetIsolationRatio();
float TargetDepthRatio();
int ScoredWolfCount();
uint32_t HoldRemainingMs();

// Mutation API calls made by the opt-in pilot. It remains exactly zero for
// the default-off observer regression.
int GameplayWriteCount();

} // namespace MonsterAI

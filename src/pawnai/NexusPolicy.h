#pragma once
#include <stdint.h>

// Pure decisions: no heap dereference, no game writes. Portable regression tests.
namespace PawnAI { namespace NexusPolicy {
inline bool BetterThreat(bool critical, float distance, uintptr_t body,
                         bool bestCritical, float bestDistance, uintptr_t bestBody)
{
    if (!body || !(distance >= 0.0f) || distance > 12.0f) return false;
    if (!bestBody) return true;
    if (critical != bestCritical) return critical;
    return distance < bestDistance || (distance == bestDistance && body < bestBody);
}
} }

namespace PawnAI { namespace NexusPolicy {
struct Candidate {
    uintptr_t body, record;
    int score;
    bool valid, emergency;
};
struct Anchor {
    int slot;
    uintptr_t body, record;
    Anchor() : slot(-1), body(0), record(0) {}
    bool Matches(const Candidate* c) const {
        return slot >= 1 && slot <= 3 && c[slot].valid &&
               c[slot].body == body && c[slot].record == record;
    }
    void Set(int s, const Candidate* c) {
        slot = s; body = c[s].body; record = c[s].record;
    }
};
struct Assignment {
    Anchor primary, temporary;
    uint32_t quietSince;
    bool quiet;
    Assignment() : quietSince(0), quiet(false) {}
    void Reset() { *this = Assignment(); }
    int Select(const Candidate* c, uint32_t now) {
        if (!primary.Matches(c)) {
            primary = Anchor();
            int best = -1;
            for (int s = 1; s <= 3; ++s)
                if (c[s].valid && c[s].score > 0 &&
                    (best < 0 || c[s].score > c[best].score)) best = s;
            if (best >= 0) primary.Set(best, c);
        }
        if (!temporary.Matches(c)) { temporary = Anchor(); quiet = false; }
        if (temporary.slot >= 1) {
            if (c[temporary.slot].emergency) quiet = false;
            else if (!quiet) { quiet = true; quietSince = now; }
            else if (uint32_t(now - quietSince) >= 3000) {
                temporary = Anchor(); quiet = false;
            }
        }
        // Do not abandon a primary already in crisis for another wounded pawn.
        if (temporary.slot < 0 && !(primary.slot >= 1 && c[primary.slot].emergency)) {
            for (int s = 1; s <= 3; ++s)
                if (c[s].valid && c[s].emergency && s != primary.slot) {
                    temporary.Set(s, c); break;
                }
        }
        return temporary.slot >= 1 ? temporary.slot : primary.slot;
    }
};
} }

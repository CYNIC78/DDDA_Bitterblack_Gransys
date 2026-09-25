#include "../src/pawnai/NexusPolicy.h"
#include <assert.h>
#include <limits>
int main() {
    using PawnAI::NexusPolicy::BetterThreat;
    assert(!BetterThreat(true, 5.5f, 2, true, 2.f, 1));
    assert(BetterThreat(true, 2.f, 1, true, 5.5f, 2));
    assert(BetterThreat(true, 6.f, 2, false, 7.f, 1));
    assert(!BetterThreat(false, 7.f, 1, true, 6.f, 2));
    assert(BetterThreat(true, 2.f, 1, true, 2.f, 2));
    assert(!BetterThreat(true, 2.f, 2, true, 2.f, 1));
    assert(!BetterThreat(false, std::numeric_limits<float>::quiet_NaN(), 1, false, 0.f, 0));
    assert(!BetterThreat(false, 13.f, 1, false, 0.f, 0));

    using namespace PawnAI::NexusPolicy;
    Candidate c[4] = {};
    c[2] = Candidate{20, 200, 30, true, false};
    c[3] = Candidate{30, 300, 10, true, false};
    Assignment a;
    assert(a.Select(c, 100) == 2);
    c[3].score = 100;
    assert(a.Select(c, 200) == 2); // sticky, not highest score every tick
    c[3].emergency = true;
    assert(a.Select(c, 300) == 3);
    assert(a.primary.slot == 2);
    c[3].emergency = false;
    assert(a.Select(c, 400) == 3);
    assert(a.Select(c, 3399) == 3);
    assert(a.Select(c, 3400) == 2); // returns, does not re-elect
    c[2].emergency = c[3].emergency = true;
    assert(a.Select(c, 3500) == 2); // do not abandon primary in crisis
    c[2].emergency = false;
    assert(a.Select(c, 3600) == 3);
    c[3].valid = false;
    assert(a.Select(c, 3700) == 2);
    c[2].body = 21; // replacement body is a new identity
    assert(!a.primary.Matches(c));
    assert(a.Select(c, 3800) == 2);
    assert(a.primary.body == 21);
    a.Reset();
    c[2].score = 0; c[2].emergency = false;
    assert(a.Select(c, 3900) == -1); // Guardian excluded normally
    c[2].emergency = true;
    assert(a.Select(c, 4000) == 2); // but eligible for emergency
    c[2].emergency = false;
    a.Select(c, 0xfffffff0u);
    assert(a.Select(c, 0x00000ba8u) == -1); // DWORD wrap, 3000 ms
}

#include "../../src/runtime/MonsterTempo.h"
#include "../../src/runtime/RuntimeInternal.h"
#include <assert.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

std::ofstream logFile("/tmp/monster_tempo_mobilization_test.log");
BYTE** pBase = 0;
BYTE* codeBase = 0;
BYTE* codeEnd = 0;
DWORD g_tempoTestNow = 0;
IniConfigStub config;

namespace Runtime {
ActorDump g_act[32] = {};
int g_nAct = 0;
bool KindIsEnemy(const char* kind) { return kind && kind[0] == 'u'; }
namespace Mem {
bool Rd(const void*, void*, size_t) { return false; }
bool WrSafe(void*, const void*, size_t) { return false; }
bool NameOfLiveObject(uintptr_t, char* out, int cap)
{ if (out && cap > 0) out[0] = 0; return false; }
} // namespace Mem
} // namespace Runtime

static bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.00001f;
}

int main()
{
    using namespace Runtime::Tempo;
    const char* reason = 0;
    DirectorMobilizationReceipt r = {};

    HardResetAllDirectorMobilization();
    ClearAllOverrides();
    SetRange(1.05f, 1.20f);
    SetAnimRange(1.05f, 1.15f);
    SetAnimCoupling(0.0f);

    assert(!AdmitDirectorMobilization(0, "uEm0200", 1.0f, 600, &r, &reason));
    assert(std::string(reason) == "director-mobilization-body-invalid");
    assert(!AdmitDirectorMobilization(0x9000, "uEm0200Variant", 1.0f, 600, &r, &reason));
    assert(std::string(reason) == "director-mobilization-species-unvalidated");
    assert(!AdmitDirectorMobilization(0x9000, "uEm0200", 1.01f, 600, &r, &reason));
    assert(std::string(reason) == "director-mobilization-urgency-invalid");
    assert(!AdmitDirectorMobilization(0x9000, "uEm0200", 1.0f, 0, &r, &reason));
    assert(std::string(reason) == "director-mobilization-ttl-required");
    assert(DirectorMobilizationCount() == 0);

    const uintptr_t body = 0x10c40060u;
    g_tempoTestNow = 100;
    assert(AdmitDirectorMobilization(body, "uEm0200", 0.4f, 600, &r, &reason));
    assert(std::string(reason) == "director-mobilization-ready");
    assert(r.body == body && r.holding && !r.decaying);
    assert(Near(r.urgency, 0.4f) && Near(r.level, 0.4f));
    assert(r.rageLoco > r.stableLoco && r.rageAnim > r.stableAnim);
    assert(r.rageLoco >= 1.20f && r.rageLoco <= 1.25f);
    assert(r.rageAnim >= 1.20f && r.rageAnim <= 1.26f);
    assert(Near(r.effectiveLoco,
                r.stableLoco + (r.rageLoco - r.stableLoco) * 0.4f));
    assert(Near(r.effectiveAnim,
                r.stableAnim + (r.rageAnim - r.stableAnim) * 0.4f));
    const DirectorMobilizationReceipt first = r;

    // A repeated weaker request refreshes the lease but cannot lower the level
    // or recompute either immutable endpoint.
    g_tempoTestNow = 200;
    assert(AdmitDirectorMobilization(body, "uEm0200", 0.2f, 600, &r, &reason));
    assert(Near(r.level, 0.4f) && Near(r.urgency, 0.4f));
    assert(Near(r.stableLoco, first.stableLoco));
    assert(Near(r.stableAnim, first.stableAnim));
    assert(Near(r.rageLoco, first.rageLoco));
    assert(Near(r.rageAnim, first.rageAnim));
    assert(DirectorMobilizationCount() == 1);

    // Full urgency reaches this body's personal endpoint. Generic overrides are
    // composed afterward and only then clamped.
    assert(AdmitDirectorMobilization(body, "uEm0200", 1.0f, 600, &r, &reason));
    assert(Near(r.level, 1.0f));
    assert(SetOverride(body, 1.02f, 0.98f, 0));
    float loco = 0.0f, anim = 0.0f;
    assert(GetFactors(body, &loco, &anim));
    assert(Near(loco, r.rageLoco * 1.02f));
    assert(Near(anim, r.rageAnim * 0.98f));

    // Ordinary release decays linearly for 1400 ms instead of deleting state.
    g_tempoTestNow = 250;
    ReleaseDirectorMobilization(body);
    assert(DirectorMobilizationCount() == 1);
    g_tempoTestNow = 950; // half of the 1400 ms decay
    ReleaseDirectorMobilization(body); // advances the product envelope
    assert(DirectorMobilizationCount() == 1);
    assert(GetFactors(body, &loco, &anim));
    const float halfLoco = first.stableLoco
                         + (first.rageLoco - first.stableLoco) * 0.5f;
    const float halfAnim = first.stableAnim
                         + (first.rageAnim - first.stableAnim) * 0.5f;
    assert(Near(loco, halfLoco * 1.02f));
    assert(Near(anim, halfAnim * 0.98f));
    g_tempoTestNow = 1650;
    ReleaseDirectorMobilization(body);
    assert(DirectorMobilizationCount() == 0);

    // A hard reset is immediate. Re-admission derives a stable mutation from
    // the deterministic assignment, never from the former rage/live factor.
    ClearOverride(body);
    SetRange(1.05f, 1.20f);
    SetAnimRange(1.05f, 1.15f);
    g_tempoTestNow = 2000;
    assert(AdmitDirectorMobilization(body, "uEm0200", 1.0f, 600, &r, &reason));
    const DirectorMobilizationReceipt beforeReset = r;
    SetRange(0.80f, 0.90f);
    SetAnimRange(0.80f, 0.90f);
    assert(AdmitDirectorMobilization(body, "uEm0200", 1.0f, 600, &r, &reason));
    assert(Near(r.stableLoco, beforeReset.stableLoco));
    assert(Near(r.stableAnim, beforeReset.stableAnim));
    assert(Near(r.rageLoco, beforeReset.rageLoco));
    assert(Near(r.rageAnim, beforeReset.rageAnim));
    HardResetDirectorMobilization(body);
    assert(DirectorMobilizationCount() == 0);
    assert(AdmitDirectorMobilization(body, "uEm0200", 1.0f, 600, &r, &reason));
    assert(r.stableLoco >= 0.80f && r.stableLoco <= 0.90f);
    assert(r.stableAnim >= 0.80f && r.stableAnim <= 0.90f);
    assert(!Near(r.stableLoco, beforeReset.rageLoco));
    assert(!Near(r.stableAnim, beforeReset.rageAnim));
    HardResetAllDirectorMobilization();

    // Capacity is bounded and partial admission has a deterministic failure.
    SetRange(1.05f, 1.20f);
    SetAnimRange(1.05f, 1.15f);
    g_tempoTestNow = 3000;
    for (int i = 0; i < 16; ++i)
        assert(AdmitDirectorMobilization(0x20000u + (uintptr_t)i * 0x100u,
                                            "uEm0200", 1.0f, 600, &r, &reason));
    assert(DirectorMobilizationCount() == 16);
    assert(!AdmitDirectorMobilization(0x30000u, "uEm0200", 1.0f, 600,
                                         &r, &reason));
    assert(std::string(reason) == "director-mobilization-table-full");
    HardResetAllDirectorMobilization();
    assert(DirectorMobilizationCount() == 0);

    // A missing controller refresh expires as an unsafe hard reset, not decay.
    g_tempoTestNow = 4000;
    assert(AdmitDirectorMobilization(0x40000u, "uEm0200", 1.0f, 600,
                                     &r, &reason));
    g_tempoTestNow = 4599;
    RefreshTable();
    assert(DirectorMobilizationCount() == 1);
    g_tempoTestNow = 4600;
    RefreshTable();
    assert(DirectorMobilizationCount() == 0);

    std::cout << "MonsterTempo Build012 mobilization: PASS\n";
    return 0;
}

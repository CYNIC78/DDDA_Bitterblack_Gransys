// PackObserve + SpeciesCard: syntax check and portable fixture.
#ifndef DDDA_PACKOBSERVE_PORTABLE
#define DDDA_PACKOBSERVE_PORTABLE
#endif
#define DDDA_PACKOBSERVE_LINKED
#include "director_stdafx.h"
#include "../../src/monsterai/PackObserve.cpp"

#include <assert.h>
#include <iostream>
#include <iterator>
#include <string>

std::ofstream logFile("/tmp/packobserve_t.log", std::ios::trunc);
IniConfigStub config;
BYTE** pBase = 0;

using namespace MonsterAI;

static std::string Slurp()
{
    logFile.flush();
    std::ifstream in("/tmp/packobserve_t.log");
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

static void Need(const std::string& hay, const char* needle)
{
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "missing log: " << needle << std::endl;
        std::cerr << hay << std::endl;
        assert(false);
    }
}

static void NeedNot(const std::string& hay, const char* needle)
{
    if (hay.find(needle) != std::string::npos) {
        std::cerr << "forbidden log: " << needle << std::endl;
        assert(false);
    }
}

static WorldReport Make(uint32_t now, int n, const uintptr_t* bodies,
                        const char* const* kinds, const char* const* acts)
{
    WorldReport w;
    memset(&w, 0, sizeof(w));
    w.timestampMs = now;
    w.count = n;
    for (int i = 0; i < n; ++i) {
        w.units[i].ptr = bodies[i];
        w.units[i].kind = kinds[i];
        lstrcpynA(w.units[i].actName, acts[i], sizeof(w.units[i].actName));
    }
    return w;
}

static void TestSpeciesCards()
{
    const SpeciesCard* wolf = FindSpeciesCard("uEm0200");
    const SpeciesCard* gob = FindSpeciesCard("uEm0100");
    assert(wolf && wolf->observe && wolf->tempoRage && wolf->aggroWrite);
    assert(wolf->bodySize == 29888u);
    assert(gob && gob->observe && !gob->tempoRage && gob->aggroWrite);
    assert(gob->bodySize == 29632u);
    assert(!SpeciesIsObserveOnly(gob));
    assert(!SpeciesIsObserveOnly(wolf));
    assert(!FindSpeciesCard("uEm0101"));
    assert(!FindSpeciesCard("uEm0100_0"));
    assert(!FindSpeciesCard("uEm0100_2"));
    assert(SpeciesExactKind("uEm0100", "uEm0100"));
    assert(!SpeciesExactKind("uEm0100_0", "uEm0100"));
}

static void TestClassify()
{
    assert(ClassifyGoblinAct("cEm0100ActCSldBGrd") == PACK_ROLE_SHIELD);
    assert(ClassifyGoblinAct("cEm0100ActHornReinforce") == PACK_ROLE_CALLER);
    assert(ClassifyGoblinAct("cEm0100ActHornTensionUp") == PACK_ROLE_CALLER);
    assert(ClassifyGoblinAct("cEm0100ActChargeCommand") == PACK_ROLE_COMMAND);
    assert(ClassifyGoblinAct("cEm0100ActIgnoreLeader") == PACK_ROLE_INSUB);
    assert(ClassifyGoblinAct("cEm0100ActEscapeStart") == PACK_ROLE_FLEE);
    assert(ClassifyGoblinAct("cEm0100ActScared") == PACK_ROLE_FLEE);
    assert(ClassifyGoblinAct("cEm0100ActHagaijime") == PACK_ROLE_RESTRAINT);
    assert(ClassifyGoblinAct("cEm0100ActJumpAttack") == PACK_ROLE_MELEE);
    assert(ClassifyGoblinAct("cEm0100ActJump") == PACK_ROLE_MOVE);
    assert(ClassifyGoblinAct("cEm0100ActGuard") == PACK_ROLE_GUARD);
    assert(ClassifyGoblinAct("cEm0100ActThrowRock") == PACK_ROLE_THROW);
    assert(ClassifyGoblinAct("cEm0100ActThreatHowl") == PACK_ROLE_IDLE);
    assert(ClassifyGoblinAct("cEm0100ActWait") == PACK_ROLE_IDLE);
    assert(ClassifyGoblinAct("") == PACK_ROLE_UNKNOWN);
}

static void TestRabbleTrio()
{
    PackObserveShutdown();
    PackObserveInit();
    uintptr_t b[3] = { 0x1001, 0x1002, 0x1003 };
    const char* k[3] = { "uEm0100", "uEm0100", "uEm0100" };
    const char* a[3] = { "cEm0100ActWait", "cEm0100ActWait", "cEm0100ActWalk" };
    PackObserveIngest(Make(1000, 3, b, k, a), 1000);
    assert(PackObserveCount() == 3);
    assert(PackObserveComposition() == PACK_RABBLE);
    const std::string log = Slurp();
    Need(log, "ADMIT species=uEm0100 size=29632 observe=1 tempoRage=0 aggroWrite=1");
    Need(log, "PACK n=3 composition=rabble");
    Need(log, "JOIN @0x1001");
}

static void TestSizeLedAndSkipMixed()
{
    PackObserveShutdown();
    PackObserveInit();
    uintptr_t b[7] = { 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x20C0, 0x20D0 };
    const char* k[7] = {
        "uEm0100", "uEm0100", "uEm0100", "uEm0100", "uEm0100",
        "uEm0100_0", "uEm0101"
    };
    const char* a[7] = {
        "cEm0100ActWait", "cEm0100ActWait", "cEm0100ActWait",
        "cEm0100ActWait", "cEm0100ActWait",
        "cEm0100ActWait", "cEm0101ActWait"
    };
    PackObserveIngest(Make(2000, 7, b, k, a), 2000);
    assert(PackObserveCount() == 5);
    assert(PackObserveComposition() == PACK_LED);
    const std::string log = Slurp();
    Need(log, "PACK n=5 composition=led");
    Need(log, "SKIP uEm0100_0 (component, not full-body uEm0100)");
    Need(log, "MIXED uEm0100=5 uEm0101=1");
    NeedNot(log, "JOIN @0x20c0");
    NeedNot(log, "JOIN @0x20d0");
}

static void TestHornChargeIgnoreAndFall()
{
    PackObserveShutdown();
    PackObserveInit();
    uintptr_t b[4] = { 0x3001, 0x3002, 0x3003, 0x3004 };
    const char* k[4] = { "uEm0100", "uEm0100", "uEm0100", "uEm0100" };
    const char* wait[4] = {
        "cEm0100ActWait", "cEm0100ActWait", "cEm0100ActWait", "cEm0100ActWait"
    };
    PackObserveIngest(Make(3000, 4, b, k, wait), 3000);
    assert(PackObserveComposition() == PACK_RABBLE);

    const char* horn[4] = {
        "cEm0100ActHornReinforce", "cEm0100ActWait",
        "cEm0100ActWait", "cEm0100ActWait"
    };
    PackObserveIngest(Make(3150, 4, b, k, horn), 3150);
    assert(PackObserveComposition() == PACK_LED);

    const char* both[4] = {
        "cEm0100ActHornReinforce", "cEm0100ActChargeCommand",
        "cEm0100ActIgnoreLeader", "cEm0100ActWait"
    };
    PackObserveIngest(Make(3300, 4, b, k, both), 3300);
    assert(PackObserveComposition() == PACK_LED);

    // King (charge body 0x3002) leaves; remaining stay waiting.
    uintptr_t left[3] = { 0x3001, 0x3003, 0x3004 };
    const char* k3[3] = { "uEm0100", "uEm0100", "uEm0100" };
    const char* still[3] = {
        "cEm0100ActWait", "cEm0100ActWait", "cEm0100ActWait"
    };
    PackObserveIngest(Make(3450, 3, left, k3, still), 3450);

    const char* flee[3] = {
        "cEm0100ActEscapeStart", "cEm0100ActScared", "cEm0100ActWait"
    };
    PackObserveIngest(Make(3600, 3, left, k3, flee), 3600);
    assert(PackObserveComposition() == PACK_LED);

    PackObserveDump();
    const std::string log = Slurp();
    Need(log, "HORN @0x3001");
    Need(log, "CHARGE @0x3002");
    Need(log, "IGNORE-LEADER @0x3003");
    Need(log, "LEADER-CAND @0x3002 reason=charge");
    Need(log, "LEAVE @0x3002");
    Need(log, "LEADER-LOST @0x3002");
    Need(log, "FLEE @0x3001 after-leader-lost");
    Need(log, "LEADER-FALL confirmed @0x3002");
    Need(log, "PackObserve dump:");
    Need(log, "write=off");
    assert(strstr(PackObserveStatus(), "led"));
}

static void TestGoneDebounce()
{
    PackObserveShutdown();
    PackObserveInit();
    uintptr_t b[2] = { 0x4001, 0x4002 };
    const char* k[2] = { "uEm0100", "uEm0100" };
    const char* a[2] = { "cEm0100ActWait", "cEm0100ActWait" };
    PackObserveIngest(Make(4000, 2, b, k, a), 4000);
    assert(PackObserveCount() == 2);

    WorldReport empty;
    memset(&empty, 0, sizeof(empty));
    empty.timestampMs = 4100;
    PackObserveIngest(empty, 4100);
    PackObserveIngest(empty, 4200);
    PackObserveIngest(empty, 4300);
    assert(PackObserveCount() == 2);
    empty.timestampMs = 4400;
    PackObserveIngest(empty, 4400);
    assert(PackObserveCount() == 0);
    assert(PackObserveComposition() == PACK_NONE);
    const std::string log = Slurp();
    Need(log, "PACK-GONE");
}

int main()
{
    TestSpeciesCards();
    TestClassify();
    TestRabbleTrio();
    TestSizeLedAndSkipMixed();
    TestHornChargeIgnoreAndFall();
    TestGoneDebounce();
    PackObserveShutdown();
    std::cout << "packobserve: PASS (cards, roles, rabble/led, skip, fall, gone)\n";
    return 0;
}

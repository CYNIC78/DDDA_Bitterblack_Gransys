// Execute the production subscriber and GetDelta with a deterministic clock.
#include "tcomp/director_stdafx.h"
static DWORD testNow = 100;
static DWORD AcquisitorTestClock() { return testNow; }
#define MsNow AcquisitorTestClock
#include "../src/pawnai/AcquisitorManager.cpp"
#undef MsNow
#include <cassert>
#include <cstdio>
using namespace PawnAI;
static void Publish(bool combat, DWORD at) {
    testNow=at; CombatReport r={};r.inCombat=combat;
    CombatBus::Instance().Publish(r);
}
static float Target(AcquisitorManager& m, float baseValue, DWORD at,
                    AcquisitorManager::State expected) {
    testNow=at;float base[I_COUNT]={};float delta[I_COUNT]={};
    base[I_ACQUISITOR]=baseValue;
    m.GetDelta(base,delta);
    assert(m.lastState==expected);
    for(int i=0;i<I_COUNT;++i) if(i!=I_ACQUISITOR) assert(delta[i]==0);
    return baseValue+delta[I_ACQUISITOR];
}
int main() {
    AcquisitorManager m;m.Init();
    assert(m.boostAmount==650 && m.boostWindowMs==20000);
    assert(m.suppressFloor==100 && m.returnMs==4000);
    assert(Target(m,250,100,AcquisitorManager::ST_IDLE)==250);
    Publish(true,1000);
    assert(Target(m,250,1000,AcquisitorManager::ST_SUPPRESS)==100);
    assert(Target(m,50,1100,AcquisitorManager::ST_SUPPRESS)==50);
    Publish(false,1150);
    assert(Target(m,250,2499,AcquisitorManager::ST_SUPPRESS)==100);
    assert(Target(m,250,2500,AcquisitorManager::ST_BOOST)==900);
    assert(Target(m,250,21149,AcquisitorManager::ST_BOOST)==900);
    assert(Target(m,250,21150,AcquisitorManager::ST_RETURN)==900);
    assert(Target(m,250,23150,AcquisitorManager::ST_RETURN)==575);
    assert(Target(m,250,25150,AcquisitorManager::ST_IDLE)==250);
    Publish(true,26000);Publish(false,26150);
    assert(Target(m,800,28000,AcquisitorManager::ST_BOOST)==1000);
    Publish(true,28150);
    assert(Target(m,800,28150,AcquisitorManager::ST_SUPPRESS)==100);
    // Existing user settings remain usable; no automatic migration.
    m.boostAmount=180;m.boostWindowMs=8000;
    Publish(false,28300);
    assert(Target(m,250,30000,AcquisitorManager::ST_BOOST)==430);
    assert(Target(m,250,36300,AcquisitorManager::ST_RETURN)==430);
    m.enabled=false;
    float base[I_COUNT]={},delta[I_COUNT]={};base[I_ACQUISITOR]=250;
    m.GetDelta(base,delta);assert(delta[I_ACQUISITOR]==0);
    m.Shutdown();
    puts("Acquisitor: PASS (defaults, timing, tail, boost, return, resumed combat, old config, isolation)");
}

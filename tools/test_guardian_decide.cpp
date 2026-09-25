// Execute production Decide through the existing Linux shim, not a reimplementation.
#include "tcomp/guard_t.cpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace PawnAI;
static int readsBeforeFail = -1;
namespace Runtime { namespace Mem {
bool Rd(const void* p, void* out, size_t n) {
    if (readsBeforeFail == 0) return false;
    if (readsBeforeFail > 0) --readsBeforeFail;
    memcpy(out, p, n); return true;
}
} }

static GuardianSitRep Scene() {
    GuardianSitRep s = {};
    s.anchorValid = s.pawnValid = true;
    s.guardian = 600; s.nexus = 350;
    s.pawnVocation = VOC_FIGHTER; s.anchorVocation = VOC_SORCERER;
    s.timestampMs = 100;
    return s;
}
static GuardianThreat Threat(uintptr_t id, float meters, bool target=true) {
    GuardianThreat t = {}; t.body=id; t.x=meters*100;
    t.kind="uEm0100"; t.targetingArisen=target; return t;
}
static void Pair(float a, float b, uintptr_t expected, float distance) {
    for (int reverse=0; reverse<2; ++reverse) {
        GuardianDoctrine d; GuardianReport r; auto s=Scene(); s.threatCount=2;
        s.threats[reverse]=Threat(10,a);s.threats[1-reverse]=Threat(20,b);
        d.Decide(s,r);assert(r.targetThreatBody==expected);
        assert(r.nearestThreatDist==distance);
    }
}
int main() {
    Pair(2,5,10,2); Pair(11,8,20,8); Pair(6,7,10,6); Pair(5,5,10,5);
    GuardianReport r;
    { GuardianDoctrine d;auto s=Scene();s.threatCount=2;
      s.threats[0]=Threat(10,7,false);s.threats[1]=Threat(20,10);
      d.Decide(s,r);assert(r.targetThreatBody==20 && r.nearestThreatDist==10); }
    { GuardianDoctrine d;auto s=Scene();s.threatCount=3;
      s.threats[0]=Threat(0,1);s.threats[1]=Threat(10,std::numeric_limits<float>::quiet_NaN());
      s.threats[2]=Threat(20,std::numeric_limits<float>::infinity());
      d.Decide(s,r);assert(!r.targetThreatBody && !r.threatsInZone); }
    // Preserve existing behavior: no undocumented perimeter/hysteresis change.
    { GuardianDoctrine d;auto s=Scene();s.threatCount=1;s.threats[0]=Threat(10,13);
      d.Decide(s,r);assert(r.targetThreatBody==10);
      s.timestampMs=250;d.Decide(s,r);assert(r.zoneEngaged && !r.targetThreatBody); }
    { GuardianDoctrine d;auto s=Scene();s.threatCount=1;s.threats[0]=Threat(10,5,false);
      d.Decide(s,r);assert(r.targetThreatBody==10);
      s.pawnX=2100;d.Decide(s,r);assert(r.targetThreatBody==10);
      s.pawnX=2101;d.Decide(s,r);assert(!r.targetThreatBody); }
    { // Production binder, not a model: selected hired inclinations replace main.
      float body[32] = {}; body[16]=100;body[17]=200;body[18]=300;
      uintptr_t addr=reinterpret_cast<uintptr_t>(body);
      auto s=Scene();s.guardian=350;s.nexus=700;
      assert(BindGuardianActor(s,addr,VOC_FIGHTER,700,350));
      assert(s.guardian==700 && s.nexus==350 && s.pawnX==100 && s.pawnZ==300);
      GuardianDoctrine d;s.threatCount=1;s.threats[0]=Threat(10,2);
      d.Decide(s,r);assert(r.targetThreatBody==10);
      for(int fail=0;fail<3;++fail) {
          s=Scene();s.pawnX=999;readsBeforeFail=fail;
          assert(!BindGuardianActor(s,addr,VOC_FIGHTER,700,350));
          assert(!s.pawnValid && s.pawnX==0 && s.pawnY==0 && s.pawnZ==0);
      }
      readsBeforeFail=-1;
      assert(!BindGuardianActor(s,0,VOC_FIGHTER,700,350));
      // A failed first candidate must not poison the next one.
      assert(BindGuardianActor(s,addr,VOC_WARRIOR,600,350));
      assert(s.pawnVocation==VOC_WARRIOR);
      body[16]=std::numeric_limits<float>::quiet_NaN();
      assert(!BindGuardianActor(s,addr,VOC_FIGHTER,700,350));
      body[16]=std::numeric_limits<float>::infinity();
      assert(!BindGuardianActor(s,addr,VOC_FIGHTER,700,350));
      body[16]=100;
      assert(!BindGuardianActor(s,addr,VOC_FIGHTER,600,700));
      assert(!BindGuardianActor(s,addr,VOC_FIGHTER,349,0));
      assert(BindGuardianActor(s,addr,VOC_FIGHTER,350,350));
      assert(!BindGuardianActor(s,addr,VOC_FIGHTER,700,std::numeric_limits<float>::quiet_NaN()));
      puts("Guardian actor binding: PASS (own inclinations, failed reads, fallback, finite checks)");
    }
    puts("Guardian Decide: PASS (ranking, distance, invalid targets, preserved boundaries)");
}

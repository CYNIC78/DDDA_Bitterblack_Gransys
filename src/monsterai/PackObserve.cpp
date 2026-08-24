// PackObserve — read-only night instrument for exact uEm0100.
// Wolf 012 write path is not referenced. No F12 controls. Transition log only.

#include "stdafx.h"
#include "PackObserve.h"
#include "SpeciesCard.h"
#include "../CombatBus.h"
#ifndef DDDA_PACKOBSERVE_PORTABLE
#include "../runtime/Runtime.h"
#endif
#include <math.h>
#include <string.h>

namespace MonsterAI {

static const int    kMaxMembers     = 16;
static const DWORD  kTickMs         = 150;
static const DWORD  kWorldFreshMs   = 450;
static const DWORD  kEscapeWindowMs = 2000;
static const DWORD  kHeartbeatMs    = 15000;
static const int    kEmptyDebounce  = 4;
static const int    kRabbleMax      = 4;
static const int    kFallEscapes    = 2;
static const float  kScaleHintHi    = 1.12f;
static const float  kScaleHintLo    = 1.08f;
static const uint32_t kScaleOffH    = 0x64;

struct Member {
    uintptr_t body;
    char      kind[16];
    char      act[64];
    int       role;
    int       loggedRole;
    float     x, y, z;
    float     distM;
    float     scaleH;
    bool      scaleValid;
    uint32_t  joinMs;
    uint32_t  lastSeenMs;
    uint32_t  hornCount;
    uint32_t  chargeCount;
    uint32_t  escapeCount;
    uint32_t  ignoreCount;
    bool      present;
};

static bool     s_armed = false;
static bool     s_admitted = false;
static Member   s_mem[kMaxMembers];
static int      s_nMem = 0;
static int      s_live = 0;
static int      s_composition = PACK_NONE;
static bool     s_ledLatched = false;
static bool     s_hornSeen = false;
static bool     s_chargeSeen = false;
static bool     s_ignoreSeen = false;
static uintptr_t s_lastHornBody = 0;
static uint32_t s_lastHornMs = 0;
static uintptr_t s_lastChargeBody = 0;
static uint32_t s_lastChargeMs = 0;
static uintptr_t s_leaderCand = 0;
static char     s_leaderReason[16] = "none";
static uintptr_t s_leaderLostBody = 0;
static uint32_t s_leaderLostMs = 0;
static int      s_escapesAfterLost = 0;
static bool     s_fallConfirmed = false;
static bool     s_scaleHintLogged = false;
static int      s_emptyTicks = 0;
static DWORD    s_lastTick = 0;
static DWORD    s_lastHeartbeat = 0;
static char     s_status[240] = "PackObserve: idle (exact uEm0100, read-only)";
static bool     s_skipLogged[8] = {};
static char     s_skipKind[8][16] = {};
static int      s_nSkip = 0;
static bool     s_mixedLogged = false;

int ClassifyGoblinAct(const char* act)
{
    if (!act || !act[0]) return PACK_ROLE_UNKNOWN;
    if (strstr(act, "CSld")) return PACK_ROLE_SHIELD;
    if (strstr(act, "HornReinforce") || strstr(act, "HornTensionUp"))
        return PACK_ROLE_CALLER;
    if (strstr(act, "ChargeCommand")) return PACK_ROLE_COMMAND;
    if (strstr(act, "IgnoreLeader")) return PACK_ROLE_INSUB;
    if (strstr(act, "EscapeStart") || strstr(act, "Scared"))
        return PACK_ROLE_FLEE;
    if (strstr(act, "Hagaijime")) return PACK_ROLE_RESTRAINT;
    if (strstr(act, "JumpAttack") || strstr(act, "NAttack")
        || strstr(act, "SwingAttack") || strstr(act, "ThrustAttack")
        || strstr(act, "LAttack") || strstr(act, "Assassin")
        || strstr(act, "SwMoveAttack") || strstr(act, "DwnAt")
        || strstr(act, "TorchAttack") || strstr(act, "WaitAtck"))
        return PACK_ROLE_MELEE;
    if (strstr(act, "Throw")) return PACK_ROLE_THROW;
    if (strstr(act, "Guard")) return PACK_ROLE_GUARD;
    if (strstr(act, "Dash") || strstr(act, "Run") || strstr(act, "Walk")
        || strstr(act, "Turn") || strstr(act, "Leap") || strstr(act, "Tumble")
        || strstr(act, "Jump"))
        return PACK_ROLE_MOVE;
    if (strstr(act, "Wait") || strstr(act, "Sit") || strstr(act, "Lie")
        || strstr(act, "Laugh") || strstr(act, "Dance")
        || strstr(act, "FindFinger") || strstr(act, "ThreatHowl")
        || strstr(act, "ArmUpHowl") || strstr(act, "TorchThreat")
        || strstr(act, "Proboke") || strstr(act, "TorchWait"))
        return PACK_ROLE_IDLE;
    return PACK_ROLE_UNKNOWN;
}

const char* PackRoleName(int role)
{
    switch (role) {
    case PACK_ROLE_IDLE:      return "idle";
    case PACK_ROLE_MOVE:      return "move";
    case PACK_ROLE_MELEE:     return "melee";
    case PACK_ROLE_THROW:     return "throw";
    case PACK_ROLE_SHIELD:    return "shield";
    case PACK_ROLE_GUARD:     return "guard";
    case PACK_ROLE_CALLER:    return "caller";
    case PACK_ROLE_COMMAND:   return "command";
    case PACK_ROLE_FLEE:      return "flee";
    case PACK_ROLE_RESTRAINT: return "restraint";
    case PACK_ROLE_INSUB:     return "insub";
    default:                  return "unknown";
    }
}

const char* PackCompositionName(int composition)
{
    if (composition == PACK_RABBLE) return "rabble";
    if (composition == PACK_LED) return "led";
    return "none";
}

static bool RoleIsSignal(int role)
{
    return role == PACK_ROLE_SHIELD || role == PACK_ROLE_CALLER
        || role == PACK_ROLE_COMMAND || role == PACK_ROLE_FLEE
        || role == PACK_ROLE_RESTRAINT || role == PACK_ROLE_INSUB;
}

static bool ExactGoblin(const char* kind)
{
    return SpeciesExactKind(kind, "uEm0100");
}

static bool GoblinComponent(const char* kind)
{
    return kind && !strncmp(kind, "uEm0100", 7) && kind[7] != 0;
}

static Member* FindMember(uintptr_t body)
{
    for (int i = 0; i < s_nMem; ++i)
        if (s_mem[i].body == body) return &s_mem[i];
    return 0;
}

static bool AlreadySkipped(const char* kind)
{
    if (!kind) return true;
    for (int i = 0; i < s_nSkip; ++i)
        if (!strcmp(s_skipKind[i], kind)) return true;
    if (s_nSkip < 8) {
        lstrcpynA(s_skipKind[s_nSkip], kind, sizeof(s_skipKind[s_nSkip]));
        s_skipLogged[s_nSkip] = true;
        ++s_nSkip;
    }
    return false;
}

static bool ReadScaleH(uintptr_t body, float* out)
{
#ifdef DDDA_PACKOBSERVE_PORTABLE
    (void)body;
    (void)out;
    return false;
#else
    float h = 0.0f;
    if (!body || !out) return false;
    if (!Runtime::ReadSafe(body + kScaleOffH, &h, sizeof(h))) return false;
    if (!(h == h) || h < 0.45f || h > 3.50f) return false;
    *out = h;
    return true;
#endif
}

static float DistToArisen(float x, float y, float z)
{
#ifdef DDDA_PACKOBSERVE_PORTABLE
    (void)x; (void)y; (void)z;
    return -1.0f;
#else
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    if (!Runtime::GetArisenWorldPos(&ax, &ay, &az)) return -1.0f;
    const float dx = x - ax;
    const float dy = y - ay;
    const float dz = z - az;
    return sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
#endif
}

static void ResetEncounter(const char* why)
{
    const int was = s_live;
    const int wasComp = s_composition;
    memset(s_mem, 0, sizeof(s_mem));
    s_nMem = 0;
    s_live = 0;
    s_composition = PACK_NONE;
    s_ledLatched = false;
    s_hornSeen = false;
    s_chargeSeen = false;
    s_ignoreSeen = false;
    s_lastHornBody = 0;
    s_lastHornMs = 0;
    s_lastChargeBody = 0;
    s_lastChargeMs = 0;
    s_leaderCand = 0;
    lstrcpynA(s_leaderReason, "none", sizeof(s_leaderReason));
    s_leaderLostBody = 0;
    s_leaderLostMs = 0;
    s_escapesAfterLost = 0;
    s_fallConfirmed = false;
    s_scaleHintLogged = false;
    s_emptyTicks = 0;
    s_mixedLogged = false;
    if (was > 0) {
        logFile << "PackObserve: PACK-GONE nWas=" << was
                << " composition=" << PackCompositionName(wasComp)
                << " reason=" << (why ? why : "clear") << std::endl;
    }
}

static void UpdateStatus()
{
    if (s_live <= 0) {
        lstrcpynA(s_status,
                  "PackObserve: idle (exact uEm0100, read-only)",
                  sizeof(s_status));
        return;
    }
    int shield = 0, caller = 0, flee = 0;
    for (int i = 0; i < s_nMem; ++i) {
        if (!s_mem[i].present) continue;
        if (s_mem[i].role == PACK_ROLE_SHIELD) ++shield;
        if (s_mem[i].role == PACK_ROLE_CALLER) ++caller;
        if (s_mem[i].role == PACK_ROLE_FLEE) ++flee;
    }
    sprintf_s(s_status,
              "PackObserve: %d uEm0100 %s leader=%s shield=%d caller=%d flee=%d",
              s_live, PackCompositionName(s_composition),
              s_leaderReason, shield, caller, flee);
}

static void LogPackChange(const char* event)
{
    logFile << "PackObserve: " << (event ? event : "PACK")
            << " n=" << s_live
            << " composition=" << PackCompositionName(s_composition)
            << " leader=" << s_leaderReason
            << " cand=0x" << std::hex << s_leaderCand << std::dec
            << " horn=" << (s_hornSeen ? 1 : 0)
            << " charge=" << (s_chargeSeen ? 1 : 0)
            << " ignore=" << (s_ignoreSeen ? 1 : 0)
            << std::endl;
}

static void MaybeScaleHint()
{
    if (s_scaleHintLogged || s_live < 2) return;
    int nValid = 0;
    int hiIdx = -1;
    float hi = 0.0f;
    for (int i = 0; i < s_nMem; ++i) {
        if (!s_mem[i].present || !s_mem[i].scaleValid) continue;
        ++nValid;
        if (hiIdx < 0 || s_mem[i].scaleH > hi) {
            hi = s_mem[i].scaleH;
            hiIdx = i;
        }
    }
    if (nValid < 2 || hiIdx < 0 || hi < kScaleHintHi) return;
    for (int i = 0; i < s_nMem; ++i) {
        if (!s_mem[i].present || !s_mem[i].scaleValid || i == hiIdx) continue;
        if (s_mem[i].scaleH >= kScaleHintLo) return;
    }
    s_scaleHintLogged = true;
    logFile << "PackObserve: SCALE-HINT @0x" << std::hex
            << s_mem[hiIdx].body << std::dec
            << " scaleH=" << hi
            << " others<" << kScaleHintLo
            << " (not a leader proof)" << std::endl;
}

static bool BodyPresent(uintptr_t body)
{
    Member* m = FindMember(body);
    return m && m->present;
}

static void ResolveLeader(uint32_t now)
{
    // A signal older than LEADER-LOST is not a new king. Otherwise the
    // horn-blower would inherit the crown the tick the charger died and
    // the flee window would never open.
    const bool chargeOk = s_lastChargeBody && BodyPresent(s_lastChargeBody)
                       && (!s_leaderLostMs || s_lastChargeMs > s_leaderLostMs);
    const bool hornOk = s_lastHornBody && BodyPresent(s_lastHornBody)
                     && (!s_leaderLostMs || s_lastHornMs > s_leaderLostMs);
    uintptr_t cand = 0;
    const char* reason = "none";
    if (chargeOk) { cand = s_lastChargeBody; reason = "charge"; }
    else if (hornOk) { cand = s_lastHornBody; reason = "horn"; }

    const bool oldGone = s_leaderCand && !BodyPresent(s_leaderCand);
    if (oldGone) {
        s_leaderLostBody = s_leaderCand;
        s_leaderLostMs = now;
        s_escapesAfterLost = 0;
        s_fallConfirmed = false;
        cand = 0;
        reason = "none";
        logFile << "PackObserve: LEADER-LOST @0x" << std::hex
                << s_leaderLostBody << std::dec
                << " was=" << s_leaderReason << std::endl;
    } else if (cand && cand != s_leaderCand) {
        s_leaderLostBody = 0;
        s_leaderLostMs = 0;
        s_escapesAfterLost = 0;
        s_fallConfirmed = false;
        logFile << "PackObserve: LEADER-CAND @0x" << std::hex
                << cand << std::dec
                << " reason=" << reason << std::endl;
    }

    s_leaderCand = cand;
    lstrcpynA(s_leaderReason, reason, sizeof(s_leaderReason));
}

static void ResolveComposition()
{
    const bool signal = s_hornSeen || s_chargeSeen || s_ignoreSeen;
    int next = PACK_NONE;
    if (s_live <= 0) next = PACK_NONE;
    else if (s_ledLatched || signal || s_live > kRabbleMax) next = PACK_LED;
    else next = PACK_RABBLE;
    if (next == PACK_LED) s_ledLatched = true;
    if (next != s_composition) {
        s_composition = next;
        LogPackChange("PACK");
    }
}

static void CountRoles(int* shield, int* caller, int* flee)
{
    if (shield) *shield = 0;
    if (caller) *caller = 0;
    if (flee) *flee = 0;
    for (int i = 0; i < s_nMem; ++i) {
        if (!s_mem[i].present) continue;
        if (s_mem[i].role == PACK_ROLE_SHIELD && shield) ++*shield;
        if (s_mem[i].role == PACK_ROLE_CALLER && caller) ++*caller;
        if (s_mem[i].role == PACK_ROLE_FLEE && flee) ++*flee;
    }
}

static void NoteNeighbors(const WorldReport& w)
{
    if (s_mixedLogged) return;
    int n0101 = 0, n0102 = 0, n0200 = 0, nComp = 0;
    for (int i = 0; i < w.count; ++i) {
        const char* k = w.units[i].kind;
        if (!k) continue;
        if (!strcmp(k, "uEm0101")) ++n0101;
        else if (!strcmp(k, "uEm0102")) ++n0102;
        else if (!strcmp(k, "uEm0200")) ++n0200;
        else if (GoblinComponent(k)) ++nComp;
    }
    if (n0101 + n0102 + n0200 + nComp == 0) return;
    s_mixedLogged = true;
    logFile << "PackObserve: MIXED uEm0100=" << s_live
            << " uEm0101=" << n0101
            << " uEm0102=" << n0102
            << " uEm0200=" << n0200
            << " component=" << nComp
            << " (observe-only, no write)" << std::endl;
}

static Member* AddMember(uintptr_t body, const char* kind,
                         float x, float y, float z, uint32_t now)
{
    if (s_nMem >= kMaxMembers) return 0;
    Member& m = s_mem[s_nMem++];
    memset(&m, 0, sizeof(m));
    m.body = body;
    lstrcpynA(m.kind, kind ? kind : "?", sizeof(m.kind));
    m.role = PACK_ROLE_UNKNOWN;
    m.loggedRole = PACK_ROLE_UNKNOWN;
    m.x = x; m.y = y; m.z = z;
    m.distM = DistToArisen(x, y, z);
    m.scaleValid = ReadScaleH(body, &m.scaleH);
    m.joinMs = now;
    m.lastSeenMs = now;
    m.present = true;
    return &m;
}

static void LogJoin(const Member& m)
{
    logFile << "PackObserve: JOIN @0x" << std::hex << m.body << std::dec
            << " kind=" << m.kind
            << " act=" << (m.act[0] ? m.act : "?")
            << " role=" << PackRoleName(m.role)
            << " scaleH=" << (m.scaleValid ? m.scaleH : -1.0f)
            << " dist=" << m.distM << "m"
            << std::endl;
}

static void LeaveMember(Member& m, uint32_t now)
{
    logFile << "PackObserve: LEAVE @0x" << std::hex << m.body << std::dec
            << " lastAct=" << (m.act[0] ? m.act : "?")
            << " role=" << PackRoleName(m.role)
            << " lived=" << (now - m.joinMs) << "ms"
            << std::endl;
    m.present = false;
}

static void OnAct(Member& m, const char* act, uint32_t now)
{
    const char* nextAct = act ? act : "";
    const bool actChanged = strcmp(m.act, nextAct) != 0;
    if (actChanged) lstrcpynA(m.act, nextAct, sizeof(m.act));
    const int role = ClassifyGoblinAct(m.act);
    m.role = role;

    if (role == PACK_ROLE_CALLER && actChanged) {
        ++m.hornCount;
        s_hornSeen = true;
        s_lastHornBody = m.body;
        s_lastHornMs = now;
        logFile << "PackObserve: HORN @0x" << std::hex << m.body << std::dec
                << " act=" << m.act
                << " n=" << s_live << std::endl;
    }
    if (role == PACK_ROLE_COMMAND && actChanged) {
        ++m.chargeCount;
        s_chargeSeen = true;
        s_lastChargeBody = m.body;
        s_lastChargeMs = now;
        logFile << "PackObserve: CHARGE @0x" << std::hex << m.body << std::dec
                << " act=" << m.act
                << " n=" << s_live << std::endl;
    }
    if (role == PACK_ROLE_INSUB && actChanged) {
        ++m.ignoreCount;
        s_ignoreSeen = true;
        logFile << "PackObserve: IGNORE-LEADER @0x" << std::hex << m.body
                << std::dec << " act=" << m.act << std::endl;
    }
    if (role == PACK_ROLE_FLEE && actChanged) {
        ++m.escapeCount;
        if (s_leaderLostMs
            && now >= s_leaderLostMs
            && now - s_leaderLostMs <= kEscapeWindowMs) {
            ++s_escapesAfterLost;
            logFile << "PackObserve: FLEE @0x" << std::hex << m.body
                    << std::dec
                    << " after-leader-lost " << (now - s_leaderLostMs)
                    << "ms escapes=" << s_escapesAfterLost << std::endl;
            if (!s_fallConfirmed && s_escapesAfterLost >= kFallEscapes) {
                s_fallConfirmed = true;
                logFile << "PackObserve: LEADER-FALL confirmed @0x"
                        << std::hex << s_leaderLostBody << std::dec
                        << " escapes=" << s_escapesAfterLost
                        << " window=" << (now - s_leaderLostMs)
                        << "ms (vanilla flee, no write)" << std::endl;
            }
        }
    }
    if (RoleIsSignal(role) && role != m.loggedRole) {
        m.loggedRole = role;
        if (role != PACK_ROLE_CALLER && role != PACK_ROLE_COMMAND
            && role != PACK_ROLE_INSUB && role != PACK_ROLE_FLEE) {
            logFile << "PackObserve: ROLE @0x" << std::hex << m.body
                    << std::dec
                    << " " << PackRoleName(role)
                    << " act=" << (m.act[0] ? m.act : "?") << std::endl;
        }
    }
}

void PackObserveInit()
{
    s_armed = true;
    s_admitted = false;
    ResetEncounter("init");
    s_nSkip = 0;
    memset(s_skipLogged, 0, sizeof(s_skipLogged));
    memset(s_skipKind, 0, sizeof(s_skipKind));
    s_lastTick = 0;
    s_lastHeartbeat = 0;
    lstrcpynA(s_status,
              "PackObserve: idle (exact uEm0100, read-only)",
              sizeof(s_status));
    logFile << "PackObserve: armed read-only exact uEm0100;"
            << " wolf writes untouched; no new F12 controls"
            << std::endl;
}

void PackObserveShutdown()
{
    if (s_live > 0) ResetEncounter("shutdown");
    s_armed = false;
    s_admitted = false;
    lstrcpynA(s_status, "PackObserve: off", sizeof(s_status));
}

void PackObserveIngest(const WorldReport& world, uint32_t nowMs)
{
    if (!s_armed) return;

    const bool fresh = world.timestampMs != 0
                    && nowMs >= world.timestampMs
                    && (nowMs - world.timestampMs) <= kWorldFreshMs;

    int nSeen = 0;
    if (fresh) {
        for (int i = 0; i < world.count && nSeen < kMaxMembers; ++i) {
            const WorldPresence& u = world.units[i];
            if (!u.ptr || !u.kind) continue;
            if (GoblinComponent(u.kind)) {
                if (!AlreadySkipped(u.kind)) {
                    logFile << "PackObserve: SKIP " << u.kind
                            << " (component, not full-body uEm0100)"
                            << std::endl;
                }
                continue;
            }
            if (!ExactGoblin(u.kind)) continue;
            ++nSeen;
        }
    }

    if (nSeen == 0) {
        if (s_live > 0) {
            ++s_emptyTicks;
            if (s_emptyTicks >= kEmptyDebounce)
                ResetEncounter(fresh ? "left-view" : "stale-world");
        }
        UpdateStatus();
        return;
    }
    s_emptyTicks = 0;

    if (!s_admitted) {
        const SpeciesCard* card = FindSpeciesCard("uEm0100");
        s_admitted = true;
        logFile << "PackObserve: ADMIT species=uEm0100 size="
                << (card ? card->bodySize : 0)
                << " observe=" << (card && card->observe ? 1 : 0)
                << " tempoRage=" << (card && card->tempoRage ? 1 : 0)
                << " aggroWrite=" << (card && card->aggroWrite ? 1 : 0)
                << std::endl;
    }

    for (int i = 0; i < s_nMem; ++i) s_mem[i].present = false;

    for (int i = 0; i < world.count; ++i) {
        const WorldPresence& u = world.units[i];
        if (!u.ptr || !ExactGoblin(u.kind)) continue;
        Member* m = FindMember(u.ptr);
        const bool freshJoin = (m == 0);
        if (!m) m = AddMember(u.ptr, u.kind, u.x, u.y, u.z, nowMs);
        if (!m) continue;
        m->present = true;
        m->lastSeenMs = nowMs;
        m->x = u.x; m->y = u.y; m->z = u.z;
        m->distM = DistToArisen(u.x, u.y, u.z);
        if (!m->scaleValid) m->scaleValid = ReadScaleH(m->body, &m->scaleH);
        OnAct(*m, u.actName, nowMs);
        if (freshJoin) LogJoin(*m);
    }

    for (int i = 0; i < s_nMem; ++i) {
        if (!s_mem[i].present && s_mem[i].body)
            LeaveMember(s_mem[i], nowMs);
    }

    int compact = 0;
    for (int i = 0; i < s_nMem; ++i) {
        if (!s_mem[i].present) continue;
        if (compact != i) s_mem[compact] = s_mem[i];
        ++compact;
    }
    s_nMem = compact;
    s_live = s_nMem;

    const int oldComp = s_composition;
    const uintptr_t oldCand = s_leaderCand;
    ResolveLeader(nowMs);
    ResolveComposition();
    MaybeScaleHint();
    NoteNeighbors(world);

    if (s_composition == oldComp && s_leaderCand != oldCand
        && s_leaderCand != 0)
        LogPackChange("PACK");

    if (!s_lastHeartbeat || nowMs - s_lastHeartbeat >= kHeartbeatMs) {
        int shield = 0, caller = 0, flee = 0;
        CountRoles(&shield, &caller, &flee);
        logFile << "PackObserve: HEARTBEAT n=" << s_live
                << " " << PackCompositionName(s_composition)
                << " leader=" << s_leaderReason
                << " shield=" << shield
                << " caller=" << caller
                << " flee=" << flee << std::endl;
        s_lastHeartbeat = nowMs;
    }
    UpdateStatus();
}

void PackObserveTick()
{
    if (!s_armed) return;
    const DWORD now = GetTickCount();
    if (s_lastTick && now - s_lastTick < kTickMs) return;
    s_lastTick = now;
    PackObserveIngest(CombatBus::Instance().LastWorld(), now);
}

void PackObserveDump()
{
    int shield = 0, caller = 0, flee = 0;
    CountRoles(&shield, &caller, &flee);
    logFile << "PackObserve dump: n=" << s_live
            << " composition=" << PackCompositionName(s_composition)
            << " leader=" << s_leaderReason
            << " cand=0x" << std::hex << s_leaderCand << std::dec
            << " horn=" << (s_hornSeen ? 1 : 0)
            << " charge=" << (s_chargeSeen ? 1 : 0)
            << " ignore=" << (s_ignoreSeen ? 1 : 0)
            << " fall=" << (s_fallConfirmed ? 1 : 0)
            << " shield=" << shield
            << " caller=" << caller
            << " flee=" << flee
            << " write=off" << std::endl;
    for (int i = 0; i < s_nMem; ++i) {
        const Member& m = s_mem[i];
        logFile << "PackObserve: member @0x" << std::hex << m.body << std::dec
                << " role=" << PackRoleName(m.role)
                << " act=" << (m.act[0] ? m.act : "?")
                << " scaleH=" << (m.scaleValid ? m.scaleH : -1.0f)
                << " dist=" << m.distM
                << " horn=" << m.hornCount
                << " charge=" << m.chargeCount
                << " escape=" << m.escapeCount
                << " ignore=" << m.ignoreCount
                << std::endl;
    }
}

const char* PackObserveStatus() { return s_status; }
int PackObserveCount() { return s_live; }
int PackObserveComposition() { return s_composition; }

} // namespace MonsterAI

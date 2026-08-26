// PawnAI::Possession — primitive. См. Possession.h и SoT §12.1.2.
#include "stdafx.h"
#include "Possession.h"
#include "PawnAI_Common.h"
#include "../runtime/Runtime.h"
#include "../runtime/MemProbe.h"

extern BYTE *codeBase;

// File-scope: naked hooks cannot live in a C++ namespace (MSVC name).
static bool   s_buffSeen[48] = {};
static int    s_buffLogs = 0;
static int    s_nId6 = 0;
static int    s_nId7 = 0;
static LPBYTE s_oBuff = nullptr;
static LPBYTE s_oEnter = nullptr;
static LPBYTE s_fnStart = nullptr;

static uintptr_t s_lastBody = 0;
static uintptr_t s_lastCtx = 0;
static bool      s_ctxNamed = false;
static bool      s_haveEnter = false;
static UINT32    s_entEcx = 0, s_entEdx = 0, s_entStk4 = 0, s_entStk8 = 0;
static bool      s_recipe = false;
static volatile int s_inject = 0;
static float     s_injT = 180.0f, s_injP0 = 0.2f, s_injP1 = 0.35f;

static const uint32_t kOffStatusCtx   = 0x2698; // cStatus, live 84.32/84.33
static const uint32_t kOffStatusParam = 0x007C; // ecx = cStatus+0x7C (84.33)

static void TryBuildRecipe()
{
    if (s_recipe || !s_haveEnter || !s_lastBody || !s_fnStart) return;
    const uintptr_t ctx = s_lastBody + kOffStatusCtx;
    const uintptr_t param = ctx + kOffStatusParam;
    // 84.33 live: ecx=cStatus+0x7C  stk4=cStatus  stk8=id(6)  edx=junk
    const bool ecxOk = (s_entEcx == (UINT32)param);
    const bool stOk  = (s_entStk4 == (UINT32)ctx || s_entStk4 == (UINT32)s_lastCtx);
    const bool idOk  = (s_entStk8 == 6);
    if (!ecxOk || !stOk || !idOk) {
        static bool s_loggedMiss = false;
        if (!s_loggedMiss) {
            s_loggedMiss = true;
            logFile << "Possession: recipe-miss ecx=" << (ecxOk ? 1 : 0)
                    << " stk4=" << (stOk ? 1 : 0)
                    << " stk8id=" << (idOk ? 1 : 0)
                    << " ecx=0x" << std::hex << s_entEcx
                    << " wantParam=0x" << param
                    << " stk4=0x" << s_entStk4
                    << " ctx=0x" << ctx << std::dec << std::endl;
        }
        return;
    }
    s_recipe = true;
    logFile << "Possession: recipe this=cStatus+0x7C arg0=cStatus arg1=id"
            << " fn=0x" << std::hex << (uintptr_t)s_fnStart << std::dec
            << std::endl;
}

extern "C" void __stdcall PossessionLogBuffEnter(UINT32 ecx, UINT32 edx,
                                                 UINT32 stk4, UINT32 stk8)
{
    if (s_haveEnter) return;
    s_haveEnter = true;
    s_entEcx = ecx;
    s_entEdx = edx;
    s_entStk4 = stk4;
    s_entStk8 = stk8;
    logFile << "Possession: BuffEnter ecx=0x" << std::hex << ecx
            << " edx=0x" << edx
            << " stk4=0x" << stk4
            << " stk8=0x" << stk8 << std::dec << std::endl;
    TryBuildRecipe();
}

extern "C" void __declspec(naked) HBuffEnter()
{
    __asm
    {
        pushad
        push    dword ptr [esp + 40]
        push    dword ptr [esp + 40]
        push    edx
        push    ecx
        call    PossessionLogBuffEnter
        popad
        jmp     s_oEnter
    }
}

extern "C" void __stdcall PossessionLogBuffApply(UINT32 id, UINT32 body, UINT32 ctx,
                                                 UINT32 tBits, UINT32 p0Bits, UINT32 p1Bits)
{
    float t = 0, p0 = 0, p1 = 0;
    memcpy(&t, &tBits, 4);
    memcpy(&p0, &p0Bits, 4);
    memcpy(&p1, &p1Bits, 4);
    if (body) s_lastBody = body;
    if (ctx) s_lastCtx = ctx;
    if (!s_ctxNamed && ctx) {
        s_ctxNamed = true;
        char cn[48] = {};
        Runtime::Mem::NameOfLiveObject((uintptr_t)ctx, cn, sizeof(cn));
        logFile << "Possession: BuffCtx ctx=0x" << std::hex << ctx
                << " off=+0x" << (body ? (ctx - body) : 0) << std::dec
                << " class=" << (cn[0] ? cn : "?") << std::endl;
    }
    const bool firstId = (id < 48u) && !s_buffSeen[id];
    if (firstId) s_buffSeen[id] = true;
    if (id == 6) ++s_nId6;
    if (id == 7) ++s_nId7;
    const bool spam6 = (id == 6 && !firstId && s_nId6 != 2 && (s_nId6 % 64) != 0);
    if (spam6 && id != 7) {
        TryBuildRecipe();
        return;
    }
    if (!firstId && id != 6 && id != 7 && s_buffLogs >= 8)
        return;
    if (s_buffLogs < 1000) ++s_buffLogs;
    char kind[48] = {};
    if (body) Runtime::Mem::NameOfLiveObject((uintptr_t)body, kind, sizeof(kind));
    logFile << "Possession: BuffApply id=" << (int)id
            << " body=0x" << std::hex << body
            << " ctx=0x" << ctx << std::dec
            << " kind=" << (kind[0] ? kind : "?")
            << " t=" << t << " p0=" << p0 << " p1=" << p1;
    if (id == 6 && s_nId6 > 1) logFile << " n=" << s_nId6;
    if (s_inject && id == 7)
        logFile << " inject t=" << s_injT
                << " p0=" << s_injP0 << " p1=" << s_injP1;
    logFile << std::endl;
    TryBuildRecipe();
}

extern "C" void __declspec(naked) HBuffApply()
{
    __asm
    {
        sub     esp, 48
        movdqu  [esp], xmm0
        movdqu  [esp + 16], xmm1
        movdqu  [esp + 32], xmm2
        pushad
        sub     esp, 12
        movss   dword ptr [esp], xmm0
        movss   dword ptr [esp + 4], xmm1
        movss   dword ptr [esp + 8], xmm2
        push    ebx
        push    eax
        push    esi
        call    PossessionLogBuffApply
        popad
        movdqu  xmm0, [esp]
        movdqu  xmm1, [esp + 16]
        movdqu  xmm2, [esp + 32]
        add     esp, 48
        cmp     s_inject, 0
        je      noinj
        cmp     esi, 7
        jne     noinj
        movss   xmm0, s_injT
        movss   xmm1, s_injP0
        movss   xmm2, s_injP1
        noinj:
        jmp     s_oBuff
    }
}

namespace PawnAI {
namespace Possession {

using Runtime::Mem::Rd;
using Runtime::Mem::WrSafe;
using Runtime::Mem::InWorld;

static const int32_t kIdEmpty      = -1;
static const int32_t kIdPossession = 7;
static const int     kSlots        = 40;
static const uint32_t kOffCount    = 0x0A2C;
static const uint32_t kOffIds      = 0x0A30;
static const uint32_t kOffTimer    = 0x0AD0;
static const uint32_t kOffP0       = 0x0B70;
static const uint32_t kOffP1       = 0x0C10;
static const float    kTimerFrames = 5400.0f;
static const float    kParam0      = 0.2f;
static const float    kParam1      = 0.35f;
static const DWORD    kWatchMs     = 2500;

static bool     s_armed = false;
static bool     s_applied = false;
static bool     s_watching = false;
static bool     s_held = false;
static bool     s_usedVanilla = false;
static int      s_slot = -1;
static DWORD    s_watchSince = 0;
static int32_t  s_countWas = 0;
static int32_t  s_idWas = kIdEmpty;
static float    s_timerWas = 0.0f;
static float    s_p0Was = 0.0f;
static float    s_p1Was = 0.0f;
static volatile LONG s_reqApply = 0;
static volatile LONG s_reqClear = 0;
static char     s_why[96] = "idle";

static LPBYTE s_pBuff = nullptr;
static bool   s_hookArmed = false;
static bool   s_customOn = false;
static float  s_customT = 180.0f;
static float  s_customP0 = 0.2f;
static float  s_customP1 = 0.35f;

static bool Near(float a, float b)
{
    const float d = a - b;
    return d > -0.02f && d < 0.02f;
}

static float ClampF(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uintptr_t MainRecord()
{
    if (!pBase || !*pBase) return 0;
    return (uintptr_t)(*pBase) + 0xA7000 + 0x7F0;
}

static bool RecordLooksLive(uintptr_t rec)
{
    if (!rec) return false;
    int32_t voc = 0;
    uint16_t lvl = 0;
    float maxHp = 0.0f;
    if (!Rd((void*)(rec + 0x6E0), &voc, 4)) return false;
    if (!Rd((void*)(rec + 0xDD0), &lvl, 2)) return false;
    if (!Rd((void*)(rec + 0x970), &maxHp, 4)) return false;
    if (voc < 1 || voc > 9) return false;
    if (lvl == 0 || lvl > 250) return false;
    if (!(maxHp > 0.0f) || maxHp > 200000.0f) return false;
    return true;
}

static bool PawnStanding()
{
    const uintptr_t body = Runtime::MainPawnBody();
    if (!body) return false;
    char act[48] = {};
    if (!Runtime::ReadLiveAct(body, act, sizeof(act))) return false;
    if (strstr(act, "Neardeath") || strstr(act, "CmcDead")
        || strstr(act, "DownDead") || strstr(act, "CrumbleDead")
        || strstr(act, "CmcReturn") || !strcmp(act, "cPlActDead"))
        return false;
    return true;
}

static bool RdI32(uintptr_t rec, uint32_t off, int32_t* v)
{
    return rec && v && Rd((void*)(rec + off), v, 4);
}
static bool RdF32(uintptr_t rec, uint32_t off, float* v)
{
    return rec && v && Rd((void*)(rec + off), v, 4);
}
static bool WrI32(uintptr_t rec, uint32_t off, int32_t v)
{
    return rec && WrSafe((void*)(rec + off), &v, 4);
}
static bool WrF32(uintptr_t rec, uint32_t off, float v)
{
    return rec && WrSafe((void*)(rec + off), &v, 4);
}

static int OccupiedCount(uintptr_t rec)
{
    int n = 0;
    for (int i = 0; i < kSlots; ++i) {
        int32_t id = kIdEmpty;
        if (!RdI32(rec, kOffIds + (uint32_t)i * 4, &id)) return -1;
        if (id != kIdEmpty) ++n;
    }
    return n;
}

static int FindId(uintptr_t rec, int32_t want)
{
    for (int i = 0; i < kSlots; ++i) {
        int32_t id = kIdEmpty;
        if (!RdI32(rec, kOffIds + (uint32_t)i * 4, &id)) return -1;
        if (id == want) return i;
    }
    return -1;
}

static void SetWhy(const char* w)
{
    lstrcpynA(s_why, w ? w : "?", sizeof(s_why));
}

static void LogState(const char* ev)
{
    int32_t id = kIdEmpty, count = 0;
    float timer = 0, p0 = 0, p1 = 0;
    const uintptr_t rec = MainRecord();
    if (RecordLooksLive(rec)) {
        RdI32(rec, kOffCount, &count);
        int slot = s_slot;
        if (slot < 0) slot = FindId(rec, kIdPossession);
        if (slot >= 0) {
            RdI32(rec, kOffIds + (uint32_t)slot * 4, &id);
            RdF32(rec, kOffTimer + (uint32_t)slot * 4, &timer);
            RdF32(rec, kOffP0 + (uint32_t)slot * 4, &p0);
            RdF32(rec, kOffP1 + (uint32_t)slot * 4, &p1);
        }
    }
    logFile << "Possession: " << ev
            << " slot=" << s_slot
            << " id=" << id
            << " t=" << timer
            << " p0=" << p0
            << " p1=" << p1
            << " count=" << count
            << " applied=" << (s_applied ? 1 : 0)
            << " watch=" << (s_watching ? 1 : 0)
            << " held=" << (s_held ? 1 : 0)
            << " why=" << s_why << std::endl;
}

static bool WriteSlot(uintptr_t rec, int slot, int32_t id, float timer,
                      float p0, float p1, int32_t count)
{
    const uint32_t o = (uint32_t)slot * 4;
    if (!WrI32(rec, kOffIds + o, id)) return false;
    if (!WrF32(rec, kOffTimer + o, timer)) return false;
    if (!WrF32(rec, kOffP0 + o, p0)) return false;
    if (!WrF32(rec, kOffP1 + o, p1)) return false;
    if (!WrI32(rec, kOffCount, count)) return false;
    int32_t idB = 0, cntB = 0;
    float tB = 0, p0B = 0, p1B = 0;
    if (!RdI32(rec, kOffIds + o, &idB) || idB != id) return false;
    if (!RdF32(rec, kOffTimer + o, &tB)) return false;
    if (id == kIdEmpty) {
        if (tB != 0.0f) return false;
    } else if (!(tB > 0.0f)) {
        return false;
    }
    if (!RdF32(rec, kOffP0 + o, &p0B) || !Near(p0B, p0)) return false;
    if (!RdF32(rec, kOffP1 + o, &p1B) || !Near(p1B, p1)) return false;
    if (!RdI32(rec, kOffCount, &cntB) || cntB != count) return false;
    return true;
}

static void DropWatch(const char* why)
{
    s_applied = false;
    s_watching = false;
    s_held = false;
    s_slot = -1;
    s_usedVanilla = false;
    SetWhy(why);
}

static void RollbackSlot(uintptr_t rec, const char* why)
{
    if (s_usedVanilla || s_slot < 0 || !rec) {
        DropWatch(why);
        LogState("failed");
        return;
    }
    WriteSlot(rec, s_slot, s_idWas, s_timerWas, s_p0Was, s_p1Was, s_countWas);
    DropWatch(why);
    LogState("rolled-back");
}

static void DoClear(const char* why)
{
    const uintptr_t rec = MainRecord();
    if (!RecordLooksLive(rec)) {
        s_applied = false;
        s_watching = false;
        s_held = false;
        s_slot = -1;
        s_usedVanilla = false;
        SetWhy(why);
        return;
    }
    int slot = s_slot;
    if (slot < 0) slot = FindId(rec, kIdPossession);
    if (slot < 0) {
        s_applied = false;
        s_watching = false;
        s_held = false;
        s_usedVanilla = false;
        SetWhy("clear-none");
        LogState(why);
        return;
    }
    int32_t count = 0;
    RdI32(rec, kOffCount, &count);
    const int occ = OccupiedCount(rec);
    int32_t nextCount = count;
    if (nextCount > 0) --nextCount;
    if (occ > 0 && nextCount > occ - 1) nextCount = occ - 1;
    if (nextCount < 0) nextCount = 0;
    if (!WriteSlot(rec, slot, kIdEmpty, 0.0f, 0.0f, 0.0f, nextCount)) {
        SetWhy("clear-write-fail");
        LogState("clear-failed");
        return;
    }
    s_applied = false;
    s_watching = false;
    s_held = false;
    s_slot = -1;
    s_usedVanilla = false;
    SetWhy(why);
    LogState("cleared");
}

static bool TryVanillaApply(int id)
{
    if (!s_fnStart) {
        SetWhy("no-buff-fn");
        return false;
    }
    const uintptr_t body = Runtime::MainPawnBody();
    if (!body) {
        SetWhy("no-body");
        return false;
    }
    const uintptr_t status = body + kOffStatusCtx;
    const uintptr_t param  = status + kOffStatusParam;
    uintptr_t back = 0;
    if (!Rd((void*)(status + 0x74), &back, 4) || back != body) {
        SetWhy("cStatus-body-mismatch");
        return false;
    }
    char cn[48] = {};
    Runtime::Mem::NameOfLiveObject(status, cn, sizeof(cn));
    if (cn[0] && strcmp(cn, "cStatus") != 0) {
        SetWhy("not-cStatus");
        return false;
    }
    s_inject = 0;
    if (s_customOn) {
        s_injT = ClampF(s_customT, 5.0f, 180.0f);
        s_injP0 = ClampF(s_customP0, 0.05f, 2.0f);
        s_injP1 = ClampF(s_customP1, 0.05f, 2.0f);
        s_inject = 1;
    }
    logFile << "Possession: vanilla-call id=" << id
            << " status=0x" << std::hex << status
            << " param=0x" << param
            << " fn=0x" << (uintptr_t)s_fnStart << std::dec
            << " layout=1 recipe=" << (s_recipe ? 1 : 0)
            << " inject=" << s_inject;
    if (s_inject)
        logFile << " t=" << s_injT << " p0=" << s_injP0 << " p1=" << s_injP1;
    logFile << std::endl;
    __try {
        // 84.33/84.34: thiscall  ecx=cStatus+0x7C  [esp+4]=cStatus*  [esp+8]=id
        typedef void (__thiscall *Fn)(void* self, void* st, int sid);
        ((Fn)s_fnStart)((void*)param, (void*)status, id);
        s_inject = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_inject = 0;
        logFile << "Possession: vanilla-call AV" << std::endl;
        SetWhy("vanilla-call AV");
        return false;
    }
}

static void BeginWatch(uintptr_t rec, int slot, int32_t countWas,
                       int32_t idWas, float tWas, float p0Was, float p1Was,
                       const char* why)
{
    s_slot = slot;
    s_countWas = countWas;
    s_idWas = idWas;
    s_timerWas = tWas;
    s_p0Was = p0Was;
    s_p1Was = p1Was;
    s_applied = true;
    s_watching = true;
    s_held = false;
    s_watchSince = GetTickCount();
    SetWhy(why);
    LogState("applied");
}

static void DoApply()
{
    if (!s_armed) {
        SetWhy("not-armed");
        LogState("apply-refused");
        return;
    }
    if (!InWorld() || !IsInActiveGameplay()) {
        SetWhy("not-in-world");
        LogState("apply-refused");
        return;
    }
    const uintptr_t rec = MainRecord();
    if (!RecordLooksLive(rec)) {
        SetWhy("main-record-invalid");
        LogState("apply-failed");
        return;
    }
    if (!PawnStanding()) {
        SetWhy("pawn-not-standing");
        LogState("apply-refused");
        return;
    }

    int32_t countWas = 0;
    RdI32(rec, kOffCount, &countWas);
    if (!TryVanillaApply(kIdPossession)) {
        LogState("apply-failed");
        return;
    }
    s_usedVanilla = true;
    const int slot = FindId(rec, kIdPossession);
    if (slot >= 0) {
        BeginWatch(rec, slot, countWas, kIdEmpty, 0, 0, 0,
                   "vanilla-applied WATCH");
        return;
    }
    BeginWatch(rec, -1, countWas, kIdEmpty, 0, 0, 0,
               "vanilla-applied PENDING");
}

void RequestApply() { InterlockedExchange(&s_reqApply, 1); }
void RequestClear() { InterlockedExchange(&s_reqClear, 1); }

void SetArmed(bool on)
{
    s_armed = on;
    if (!on && (s_applied || s_watching))
        InterlockedExchange(&s_reqClear, 1);
}

void SetCustom(bool on) { s_customOn = on; }
void SetCustomTimer(float seconds) { s_customT = ClampF(seconds, 5.0f, 180.0f); }
void SetCustomP0(float v) { s_customP0 = ClampF(v, 0.05f, 2.0f); }
void SetCustomP1(float v) { s_customP1 = ClampF(v, 0.05f, 2.0f); }

void Tick()
{
    if (!InWorld() || !IsInActiveGameplay()) {
        if (s_applied || s_watching)
            DoClear("world-unload");
        return;
    }
    if (InterlockedExchange(&s_reqClear, 0))
        DoClear("user-clear");
    if (InterlockedExchange(&s_reqApply, 0))
        DoApply();

    if (!s_watching) return;
    const uintptr_t rec = MainRecord();
    if (!RecordLooksLive(rec)) {
        RollbackSlot(rec, "watch-record-lost");
        return;
    }
    if (s_slot < 0) {
        const int slot = FindId(rec, kIdPossession);
        if (slot >= 0) {
            s_slot = slot;
            s_watchSince = GetTickCount();
            SetWhy("vanilla-applied WATCH");
            LogState("record-landed");
            return;
        }
        if (GetTickCount() - s_watchSince >= kWatchMs) {
            s_held = true;
            s_watching = false;
            SetWhy("holding-no-record");
            LogState("watch-ok-no-record");
        }
        return;
    }
    int32_t id = kIdEmpty, count = 0;
    float timer = 0.0f;
    RdI32(rec, kOffIds + (uint32_t)s_slot * 4, &id);
    RdI32(rec, kOffCount, &count);
    RdF32(rec, kOffTimer + (uint32_t)s_slot * 4, &timer);
    if (id != kIdPossession || count <= 0 || !(timer > 0.0f)) {
        s_applied = false;
        s_watching = false;
        s_held = false;
        s_slot = -1;
        s_usedVanilla = false;
        SetWhy("watch-engine-cleared");
        LogState("failed");
        return;
    }
    const DWORD now = GetTickCount();
    if (!s_held && now - s_watchSince >= kWatchMs) {
        s_held = true;
        s_watching = false;
        SetWhy("holding");
        LogState("watch-ok");
    }
}

static BYTE* FindPrologue(BYTE* mid)
{
    if (!mid || !codeBase) return 0;
    BYTE* sig = mid;
    BYTE* lo = sig - 384;
    if (lo < codeBase) lo = codeBase;
    for (BYTE* q = sig - 3; q >= lo; --q) {
        if (q[0] == 0x55 && q[1] == 0x8B && q[2] == 0xEC)
            return q;
    }
    return 0;
}

void Init()
{
    s_armed = config.getBool("possession", "enabled", false);
    s_customOn = config.getBool("possession", "customParams", false);
    s_customT = ClampF(config.getFloat("possession", "timer", 180.0f), 5.0f, 180.0f);
    s_customP0 = ClampF(config.getFloat("possession", "param0", 0.2f), 0.05f, 2.0f);
    s_customP1 = ClampF(config.getFloat("possession", "param1", 0.35f), 0.05f, 2.0f);
    s_inject = 0;
    s_applied = false;
    s_watching = false;
    s_held = false;
    s_usedVanilla = false;
    s_slot = -1;
    s_reqApply = 0;
    s_reqClear = 0;
    s_buffLogs = 0;
    s_nId6 = 0;
    s_nId7 = 0;
    s_haveEnter = false;
    s_recipe = false;
    s_ctxNamed = false;
    s_lastBody = 0;
    s_lastCtx = 0;
    memset(s_buffSeen, 0, sizeof(s_buffSeen));
    SetWhy(s_armed ? "armed" : "idle");
    logFile << "Possession: primitive "
            << (s_armed ? "armed" : "idle")
            << " (84.37 xmm-params on our Set only; layout thiscall; no water; no revive)"
            << std::endl;

    BYTE sigBuff[] = {
        0xF3, 0x0F, 0x10, 0x48, 0x0C,
        0xF3, 0x0F, 0x10, 0x50, 0x10,
        0x8B, 0x43, 0x74
    };
    if (Hooks::FindSignature("Possession (buffApply)", sigBuff, &s_pBuff)) {
        BYTE* sigAt = s_pBuff;
        s_fnStart = FindPrologue(sigAt);
        if (s_fnStart) {
            logFile << "Possession: buff-fn 0x" << std::hex
                    << (uintptr_t)s_fnStart << std::dec
                    << " (prologue)" << std::endl;
            Hooks::CreateHook("Possession (buffEnter)", s_fnStart, HBuffEnter,
                              (LPVOID*)&s_oEnter, true);
        } else {
            logFile << "Possession: buff-fn prologue not found" << std::endl;
        }
        s_pBuff = sigAt + sizeof(sigBuff);
        Hooks::CreateHook("Possession (buffApply)", s_pBuff, HBuffApply,
                          (LPVOID*)&s_oBuff, true);
        s_hookArmed = (s_oBuff != nullptr);
    } else {
        s_hookArmed = false;
    }
    logFile << "Possession: buff-hook "
            << (s_hookArmed ? "armed" : "missing")
            << " (layout apply id=7; xmm inject our-Set only)" << std::endl;
}

void Shutdown()
{
    if (s_applied || s_watching)
        DoClear("shutdown");
    logFile << "Possession: shutdown BuffApply6=" << s_nId6
            << " BuffApply7=" << s_nId7
            << " recipe=" << (s_recipe ? 1 : 0)
            << " layout=" << (s_fnStart ? 1 : 0)
            << " custom=" << (s_customOn ? 1 : 0) << std::endl;
}

Status Get()
{
    Status s;
    memset(&s, 0, sizeof(s));
    s.armed = s_armed;
    s.applied = s_applied;
    s.watching = s_watching;
    s.held = s_held;
    s.hookArmed = s_hookArmed;
    s.layout = (s_fnStart != nullptr) && s_hookArmed;
    s.recipe = s_recipe;
    s.customOn = s_customOn;
    s.customT = s_customT;
    s.customP0 = s_customP0;
    s.customP1 = s_customP1;
    s.slot = s_slot;
    lstrcpynA(s.why, s_why, sizeof(s.why));
    const uintptr_t rec = MainRecord();
    if (RecordLooksLive(rec)) {
        RdI32(rec, kOffCount, &s.liveCount);
        int32_t id0 = kIdEmpty;
        RdI32(rec, kOffIds, &id0);
        s.liveId = id0;
        RdF32(rec, kOffTimer, &s.liveTimer);
        RdF32(rec, kOffP0, &s.liveP0);
        RdF32(rec, kOffP1, &s.liveP1);
        const int have = FindId(rec, kIdPossession);
        if (have >= 0) {
            RdI32(rec, kOffIds + (uint32_t)have * 4, &s.liveId);
            RdF32(rec, kOffTimer + (uint32_t)have * 4, &s.liveTimer);
            RdF32(rec, kOffP0 + (uint32_t)have * 4, &s.liveP0);
            RdF32(rec, kOffP1 + (uint32_t)have * 4, &s.liveP1);
        }
    }
    return s;
}

} // namespace Possession
} // namespace PawnAI

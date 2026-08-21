// PawnAI::WandRange — см. WandRange.h.
//
// Резолв — ТА ЖЕ ЦЕПОЧКА, что у эрраты Guardian/Nexus:
//   MainPawnBody -> cAICtrl (имя класса, не голый оффсет)
//                -> cAIActionInterfaceCtrl
//                -> cCmc*  (+0x258 = скомпилированные Range*, Build 40)
// Плюс PlanCtrl(55) = WpnWandAtk, если интерфейс ещё не собрал посох.
// Полный обход тела пешки с NameOf на каждый указатель — это census.
// Мы на нём уже обжигались: прибор молчит, а лог врёт «нет строк».

#include "stdafx.h"
#include "WandRange.h"
#include "../runtime/Runtime.h"
#include "../runtime/RuntimeInternal.h"

namespace PawnAI {
namespace WandRange {

using Runtime::Mem::Rd;
using Runtime::Mem::RdPtr;
using Runtime::Mem::WrSafe;
using Runtime::Mem::LooksHeap;
using Runtime::Mem::NameOfLiveObject;

static bool s_enabled = false;
static bool s_applied = false;
static int  s_nLive = 0;
static int  s_nSeen = 0;     // сколько cCmc* с range-блоком увидели
static float s_band[12][2];  // уникальные (min,max), что реально видели
static char  s_bandNm[12][40];
static int   s_nBand = 0;
static char s_why[96] = "off";

struct Patch {
    uintptr_t addr;
    float     was;
    float     now;
};
static const int kMaxPatch = 192;
static Patch s_patch[kMaxPatch];
static int   s_nPatch = 0;

static bool Near(float a, float b)
{
    const float d = a - b;
    return d > -0.5f && d < 0.5f;
}

static bool WriteFloat(uintptr_t addr, float want)
{
    float cur = 0.0f;
    if (!Rd((void*)addr, &cur, 4)) return false;
    if (Near(cur, want)) {
        if (s_nPatch < kMaxPatch) {
            s_patch[s_nPatch].addr = addr;
            s_patch[s_nPatch].was = cur; // already want, still track
            s_patch[s_nPatch].now = want;
            ++s_nPatch;
        }
        return true;
    }
    if (!WrSafe((void*)addr, &want, 4)) return false;
    float back = 0.0f;
    if (!Rd((void*)addr, &back, 4) || !Near(back, want)) {
        WrSafe((void*)addr, &cur, 4);
        return false;
    }
    if (s_nPatch >= kMaxPatch) return false;
    s_patch[s_nPatch].addr = addr;
    s_patch[s_nPatch].was  = cur;
    s_patch[s_nPatch].now  = want;
    ++s_nPatch;
    return true;
}

// 75.53 live: iface of MainPawnBody is the CURRENT action of the MAIN
// pawn — we patched cCmc100Slash / Dagger / Wait / BowAttack. Magick
// Bolt at 5–7 m is a different cCmc (Wand/Magic*), often on a hired
// pawn. Filter by class name; walk every uCmc, not only main.
static bool IsStaffBand(float mn, float mx)
{
    if (mx < 100.0f || mx > 1000.0f) return false;
    if (mn < 0.0f || mn > mx + 1.0f) return false;
    return true;
}

// IceWalk = Frigor aura: pawn MUST walk into the pack. Leave it.
static bool IsWandCmcName(const char* nm)
{
    if (!nm || strncmp(nm, "cCmc", 4) != 0) return false;
    if (strstr(nm, "IceWalk") || strstr(nm, "Dagger") || strstr(nm, "Sword")
        || strstr(nm, "Bow") || strstr(nm, "Slash") || strstr(nm, "Shield")
        || strstr(nm, "Stinger") || strstr(nm, "Wait") || strstr(nm, "StandOff")
        || strstr(nm, "Follow") || strstr(nm, "Goto") || strstr(nm, "GSword"))
        return false;
    if (strstr(nm, "Wand") || strstr(nm, "Magic") || strstr(nm, "Funnel")
        || strstr(nm, "Whip") || strstr(nm, "Spell") || strstr(nm, "Lightning")
        || strstr(nm, "Ice") || strstr(nm, "Fire") || strstr(nm, "Thunder")
        || strstr(nm, "Silent") || strstr(nm, "Sleep") || strstr(nm, "Plasma")
        || strstr(nm, "Spark") || strstr(nm, "Cloud") || strstr(nm, "Enchant")
        || strstr(nm, "Phantom") || strstr(nm, "Dispel") || strstr(nm, "Holy")
        || strstr(nm, "Dark") || strstr(nm, "Stone") || strstr(nm, "Missle")
        || strstr(nm, "Missile") || strstr(nm, "Emperor") || strstr(nm, "Ball")
        || strstr(nm, "Healing") || strstr(nm, "Cure") || strstr(nm, "Circle"))
        return true;
    return false;
}

static void NoteBand(float mn, float mx, const char* nm)
{
    for (int i = 0; i < s_nBand; ++i)
        if (Near(s_band[i][0], mn) && Near(s_band[i][1], mx)
            && !strcmp(s_bandNm[i], nm ? nm : "?")) return;
    if (s_nBand >= 12) return;
    s_band[s_nBand][0] = mn;
    s_band[s_nBand][1] = mx;
    lstrcpynA(s_bandNm[s_nBand], nm ? nm : "?", sizeof(s_bandNm[0]));
    ++s_nBand;
}

static void LogBands(const char* tag)
{
    char buf[220];
    int n = sprintf_s(buf, "WandRange: %s bands:", tag);
    for (int i = 0; i < s_nBand && n > 0 && n < 200; ++i)
        n += sprintf_s(buf + n, sizeof(buf) - n, " %s %.1f-%.1f",
                       s_bandNm[i],
                       s_band[i][0] / 100.0f, s_band[i][1] / 100.0f);
    logFile << buf << " m" << std::endl;
}

static int PatchSix(uintptr_t base)
{
    float f[6] = {};
    if (!Rd((void*)base, f, sizeof(f))) return 0;
    if (!IsStaffBand(f[0], f[1])) return 0;
    int n = 0;
    if (WriteFloat(base + 4, 1500.0f)) ++n;
    if (f[5] >= 100.0f && f[5] < 1999.0f) {
        if (WriteFloat(base + 20, 2000.0f)) ++n;
    }
    return n;
}

static uintptr_t PawnAICtrl(uintptr_t pawn)
{
    if (!pawn) return 0;
    uintptr_t ctrl = 0;
    if (RdPtr((void*)(pawn + 0x2E64), &ctrl) && ctrl) {
        char nm[48] = {};
        if (!NameOfLiveObject(ctrl, nm, sizeof(nm)) || strcmp(nm, "cAICtrl") != 0)
            ctrl = 0;
    }
    if (!ctrl) ctrl = Runtime::FindChildByClass(pawn, 0x58E0, "cAICtrl", 0);
    return ctrl;
}

static uintptr_t ActionIfaceCtrl(uintptr_t ctrl)
{
    if (!ctrl) return 0;
    uintptr_t iface = 0;
    if (RdPtr((void*)(ctrl + 0x40), &iface) && iface) {
        char nm[48] = {};
        if (!NameOfLiveObject(iface, nm, sizeof(nm))
            || strcmp(nm, "cAIActionInterfaceCtrl") != 0)
            iface = 0;
    }
    if (!iface)
        iface = Runtime::FindChildByClass(ctrl, 704, "cAIActionInterfaceCtrl", 0);
    return iface;
}

static bool AlreadyHave(uintptr_t addr)
{
    for (int i = 0; i < s_nPatch; ++i)
        if (s_patch[i].addr == addr) return true;
    return false;
}

static int ConsiderCmc(uintptr_t obj)
{
    char nm[48] = {};
    if (!NameOfLiveObject(obj, nm, sizeof(nm))) return 0;
    if (strncmp(nm, "cCmc", 4) != 0) return 0;
    if (!strcmp(nm, "cCmcInfo")) return 0;
    float f[6] = {};
    if (!Rd((void*)(obj + 0x258), f, sizeof(f))) return 0;
    // Любой cCmc с шестёркой range — видели блок. Банда посоха — патчим.
    if (f[1] > 1.0f && f[1] < 4000.0f) {
        ++s_nSeen;
        NoteBand(f[0], f[1], nm);
    }
    if (!IsWandCmcName(nm)) return 0;
    if (!IsStaffBand(f[0], f[1])) return 0;
    if (AlreadyHave(obj + 0x25C)) return 0;
    return PatchSix(obj + 0x258);
}

static int WalkObject(uintptr_t obj, uint32_t bytes, int depth)
{
    if (!obj || bytes < 8 || bytes > 0x800 || depth > 2) return 0;
    int n = 0;
    n += ConsiderCmc(obj);
    for (uint32_t off = 0; off + 4 <= bytes; off += 4) {
        uintptr_t p = 0;
        if (!RdPtr((void*)(obj + off), &p) || !LooksHeap(p) || p == obj)
            continue;
        char nm[48] = {};
        if (!NameOfLiveObject(p, nm, sizeof(nm)) || !nm[0]) continue;
        if (!strncmp(nm, "cCmc", 4))
            n += ConsiderCmc(p);
        else if (depth < 2 && (!strcmp(nm, "cAIActionInterfaceCmc")
                            || strstr(nm, "ActionInterface")))
            n += WalkObject(p, 640, depth + 1);
    }
    return n;
}

static int WalkPlanWand(uintptr_t ctrl)
{
    const uintptr_t planner =
        Runtime::FindChildByClass(ctrl, 704, "cAIGoalPlanning", 0);
    if (!planner) return 0;
    // code 55 = WpnWandAtk (PAWN_GOAL_CODES)
    const uintptr_t block = planner + 0x190 + 55u * 0x110u;
    return WalkObject(block, 0x110, 0);
}

void Restore(const char* why)
{
    for (int i = s_nPatch - 1; i >= 0; --i) {
        float cur = 0.0f;
        if (!Rd((void*)s_patch[i].addr, &cur, 4)) continue;
        if (Near(cur, s_patch[i].now) || Near(cur, s_patch[i].was))
            WrSafe((void*)s_patch[i].addr, &s_patch[i].was, 4);
    }
    const bool had = s_nPatch > 0;
    s_nPatch = 0;
    s_applied = false;
    s_nLive = s_nSeen = s_nBand = 0;
    lstrcpynA(s_why, why ? why : "restored", sizeof(s_why));
    if (had)
        logFile << "WandRange: restored (" << s_why << ")" << std::endl;
}

void Init()
{
    s_enabled = config.getBool("errata", "wandRange", false);
    logFile << "WandRange: " << (s_enabled ? "enabled" : "disabled")
            << " (all caster cCmc + Anodyne/Cure/Circle, IceWalk off, 1-10 m -> 15 m)"
            << std::endl;
}

void SetEnabled(bool on)
{
    s_enabled = on;
    if (!on) Restore("off");
}

void Tick()
{
    if (!s_enabled) {
        if (s_nPatch) Restore("off");
        else lstrcpynA(s_why, "off", sizeof(s_why));
        return;
    }

    if (s_applied && s_nPatch > 0) {
        int ok = 0;
        for (int i = 0; i < s_nPatch; ++i) {
            float cur = 0.0f;
            if (Rd((void*)s_patch[i].addr, &cur, 4) && Near(cur, s_patch[i].now))
                ++ok;
        }
        if (ok == s_nPatch) {
            lstrcpynA(s_why, "holding", sizeof(s_why));
            return;
        }
        s_nPatch = 0;
        s_applied = false;
    }

    static DWORD s_lastTry = 0;
    const DWORD now = GetTickCount();
    if (!s_applied && s_lastTry && now - s_lastTry < 1000)
        return;
    s_lastTry = now;

    s_nPatch = 0;
    s_nSeen = 0;
    s_nLive = 0;
    s_nBand = 0;

    int n = 0;
    int nCtrl = 0;
    int nIface = 0;
    const int nPawn = Runtime::PawnBodyCount();
    for (int i = 0; i < nPawn; ++i) {
        bool isMain = false;
        const uintptr_t body = Runtime::PawnBodyAt(i, &isMain);
        const uintptr_t ctrl = PawnAICtrl(body);
        if (!ctrl) continue;
        ++nCtrl;
        const uintptr_t iface = ActionIfaceCtrl(ctrl);
        if (iface) {
            ++nIface;
            n += WalkObject(iface, 60, 0);
        }
        n += WalkPlanWand(ctrl);
    }
    s_nLive = n;
    s_applied = n > 0;

    if (s_applied) {
        sprintf_s(s_why, "APPLIED live cCmc %d (seen %d)", n, s_nSeen);
        logFile << "WandRange: " << s_why << std::endl;
        LogBands("applied");
    } else if (nCtrl == 0) {
        lstrcpynA(s_why, "cAICtrl not resolved", sizeof(s_why));
    } else if (nIface == 0) {
        lstrcpynA(s_why, "cAIActionInterfaceCtrl not on cAICtrl", sizeof(s_why));
    } else if (s_nSeen == 0) {
        lstrcpynA(s_why, "no cCmc range block yet (draw the staff / enter combat)",
                  sizeof(s_why));
    } else {
        sprintf_s(s_why, "saw %d cCmc ranges, none patchable (see log)", s_nSeen);
        logFile << "WandRange: " << s_why << std::endl;
        LogBands("seen");
    }
}

void Shutdown() { Restore("shutdown"); }

Status Get()
{
    Status s;
    s.enabled = s_enabled;
    s.applied = s_applied;
    s.nResource = s_nSeen;
    s.nLive = s_nLive;
    lstrcpynA(s.why, s_why, sizeof(s.why));
    return s;
}

} // namespace WandRange
} // namespace PawnAI

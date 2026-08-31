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
#include "../CombatBus.h"
#include <math.h>

namespace PawnAI {
namespace WandRange {

using Runtime::Mem::Rd;
using Runtime::Mem::RdPtr;
using Runtime::Mem::WrSafe;
using Runtime::Mem::LooksHeap;
using Runtime::Mem::NameOfLiveObject;
using Runtime::Mem::RegionOk;

static bool s_enabled = false;
static bool s_nukeGating = true;
static bool s_lastAllowNukes = true;
static bool s_applied = false;
static int  s_nLive = 0;
static int  s_nSeen = 0;     // сколько cCmc* с range-блоком увидели
static float s_band[12][2];  // уникальные (min,max), что реально видели
static char  s_bandNm[12][40];
static int   s_nBand = 0;
static char s_why[96] = "off";

// --- Caster Watch & HFB Tracking State ---
enum CasterFsmState {
    CFSM_IDLE = 0,
    CFSM_CHARGING_BOLT,
    CFSM_CHANTING_SPELL,
    CFSM_CHANNELING
};

struct PawnCasterTracker {
    CasterFsmState fsmState;
    DWORD          stateStartMs;
    uintptr_t      targetBody;
    char           targetKind[32];
    float          targetDistM;
    char           activeCmc[48];
    char           activeAct[48];
    ElementType    appliedElement;
};

static PawnCasterTracker s_tracker[4] = {};
static ElementType       s_partyElement = ELEM_HOLY; // Holy by default for HFB preview / user Holy Affinity
static DWORD             s_partyElementExpiryMs = 0xFFFFFFFFu; // active unless overridden

static uint32_t s_boltsCharged = 0;
static uint32_t s_boltsFired = 0;
static uint32_t s_spellsChanted = 0;
static uint32_t s_spellsCompleted = 0;
static char     s_lastCasterEvent[128] = "idle";

static const char* ElementName(ElementType elem)
{
    switch (elem) {
    case ELEM_HOLY:      return "Holy / HFB";
    case ELEM_FIRE:      return "Fire";
    case ELEM_ICE:       return "Ice";
    case ELEM_LIGHTNING: return "Lightning";
    case ELEM_DARK:      return "Dark";
    default:             return "Standard";
    }
}

static const char* FriendlySpellName(const char* cmc)
{
    if (!cmc || !cmc[0]) return "Wand Spell";
    if (strstr(cmc, "Meteo")) return "High Bolide";
    if (strstr(cmc, "Tatsumaki")) return "High Maelstrom";
    if (strstr(cmc, "EarthShake")) return "High Seism";
    if (strstr(cmc, "FireWall")) return "High Comestion";
    if (strstr(cmc, "FireBall")) return "High Ingle";
    if (strstr(cmc, "LightningCloud")) return "High Levin";
    if (strstr(cmc, "ThunderSpark")) return "High Fulmination";
    if (strstr(cmc, "IceMissle") || strstr(cmc, "IceMissile")) return "High Frigor";
    if (strstr(cmc, "IceBlock")) return "High Gicel";
    if (strstr(cmc, "Healing")) return "High Anodyne";
    if (strstr(cmc, "Cure")) return "High Halidom";
    if (strstr(cmc, "EnchantHorly") || strstr(cmc, "EnchantHoly")) return "Holy Affinity";
    if (strstr(cmc, "EnchantFire")) return "Fire Affinity";
    if (strstr(cmc, "EnchantFrost") || strstr(cmc, "EnchantIce")) return "Ice Affinity";
    if (strstr(cmc, "EnchantThunder")) return "Thunder Affinity";
    if (strstr(cmc, "EnchantDark")) return "Dark Affinity";
    if (strstr(cmc, "DeathCircle")) return "High Exequy";
    if (strstr(cmc, "Stone")) return "High Petrification";
    if (strstr(cmc, "SilentCircle")) return "High Silentium";
    if (strstr(cmc, "SleepingBell")) return "High Blearing";
    if (strstr(cmc, "Slow")) return "High Lassitude";
    if (strstr(cmc, "DispelMagic")) return "Spellscreen";
    if (strstr(cmc, "PhantomFunnel")) return "High Miasma";
    if (strstr(cmc, "Fannel") || strstr(cmc, "Funnel")) return "Magic Agent";
    if (strstr(cmc, "Whip")) return "High Brontide";
    if (strstr(cmc, "WandDX")) return "Focused Bolt";
    return cmc;
}

// IceWalk = Frigor aura: pawn MUST walk into the pack. Leave it.
static bool IsWandCmcName(const char* nm)
{
    if (!nm || strncmp(nm, "cCmc", 4) != 0) return false;
    if (strstr(nm, "IceWalk") || strstr(nm, "Dagger") || strstr(nm, "Sword")
        || strstr(nm, "Bow") || strstr(nm, "Slash") || strstr(nm, "Shield")
        || strstr(nm, "Stinger") || strstr(nm, "Wait") || strstr(nm, "StandOff")
        || strstr(nm, "Follow") || strstr(nm, "Goto") || strstr(nm, "GSword")
        || strstr(nm, "Jump") || strstr(nm, "Precaution") || strstr(nm, "Provoke")
        || strstr(nm, "Escape") || strstr(nm, "Item") || strstr(nm, "Victory")
        || strstr(nm, "Cling") || strstr(nm, "Climb") || strstr(nm, "OMBreak")
        || strstr(nm, "Recover") || strstr(nm, "Lift") || strstr(nm, "Catapult")
        || strstr(nm, "School") || strstr(nm, "Message") || strstr(nm, "OpenDoor"))
        return false;
    if (strstr(nm, "Wand") || strstr(nm, "Magic") || strstr(nm, "Funnel")
        || strstr(nm, "Whip") || strstr(nm, "Spell") || strstr(nm, "Lightning")
        || strstr(nm, "Ice") || strstr(nm, "Fire") || strstr(nm, "Thunder")
        || strstr(nm, "Silent") || strstr(nm, "Sleep") || strstr(nm, "Plasma")
        || strstr(nm, "Spark") || strstr(nm, "Cloud") || strstr(nm, "Enchant")
        || strstr(nm, "Phantom") || strstr(nm, "Dispel") || strstr(nm, "Holy")
        || strstr(nm, "Dark") || strstr(nm, "Stone") || strstr(nm, "Missle")
        || strstr(nm, "Missile") || strstr(nm, "Emperor") || strstr(nm, "Ball")
        || strstr(nm, "Healing") || strstr(nm, "Cure") || strstr(nm, "Circle")
        || strstr(nm, "Meteo") || strstr(nm, "Tatsumaki") || strstr(nm, "EarthShake")
        || strstr(nm, "Fannel"))
        return true;
    return false;
}

// Тяжелые «ядерные» заклинания с огромным временем каста (10-15 сек)
static bool IsHeavyNukeSpell(const char* nm)
{
    if (!nm) return false;
    return strstr(nm, "Meteo") != nullptr        // Bolide / High Bolide
        || strstr(nm, "Tatsumaki") != nullptr     // Maelstrom / High Maelstrom
        || strstr(nm, "EarthShake") != nullptr    // Seism / High Seism
        || strstr(nm, "ThunderSpark") != nullptr  // Fulmination
        || strstr(nm, "DeathCircle") != nullptr;  // Exequy
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

static bool ReadActiveCmcName(uintptr_t ctrl, char* out, int cap)
{
    if (!out || cap < 2) return false;
    out[0] = 0;
    if (!ctrl) return false;

    // 1. Check cAIActionInterfaceCtrl at ctrl + 0x40
    uintptr_t iface = 0;
    if (RdPtr((void*)(ctrl + 0x40), &iface) && iface && LooksHeap(iface)) {
        for (uint32_t off = 0; off + 4 <= 60; off += 4) {
            uintptr_t cand = 0;
            if (RdPtr((void*)(iface + off), &cand) && LooksHeap(cand)) {
                char nm[48] = {};
                if (NameOfLiveObject(cand, nm, sizeof(nm)) && !strncmp(nm, "cCmc", 4) && strcmp(nm, "cCmcInfo") != 0) {
                    lstrcpynA(out, nm, cap);
                    return true;
                }
            }
        }
    }

    // 2. Fallback: inspect direct child pointers of ctrl
    for (uint32_t off = 0x40; off + 4 <= 0x80; off += 4) {
        uintptr_t cand = 0;
        if (RdPtr((void*)(ctrl + off), &cand) && LooksHeap(cand)) {
            char nm[48] = {};
            if (NameOfLiveObject(cand, nm, sizeof(nm)) && !strncmp(nm, "cCmc", 4) && strcmp(nm, "cCmcInfo") != 0) {
                lstrcpynA(out, nm, cap);
                return true;
            }
        }
    }
    return false;
}

static bool IsPawnCaster(uintptr_t body, int slot)
{
    if (!body) return false;
    int voc = 0, lvl = 0;
    uintptr_t b = 0;
    if (Runtime::PartyRecordInfo(slot, &voc, &lvl, &b)) {
        // 3 = Mage, 9 = Sorcerer
        if (voc == 3 || voc == 9) return true;
    }
    const uintptr_t ctrl = PawnAICtrl(body);
    if (ctrl) {
        char cmcName[48] = {};
        if (ReadActiveCmcName(ctrl, cmcName, sizeof(cmcName))) {
            if (IsWandCmcName(cmcName)) return true;
        }
    }
    return false;
}

// Оценка тактической обстановки для тяжелых заклинаний (Болид, Мэлстром, Сейсм):
// 1. Нет ли прямой угрозы кастеру в упор (< 6.0 м)
// 2. Не является ли бой добиванием единственного оставшегося слабого моба
static bool ShouldAllowNukes()
{
    const WorldReport w = CombatBus::Instance().LastWorld();
    if (w.count <= 0) return false;

    // 1. Поиск кастеров среди пешек и проверка угрозы в упор
    const int nPawns = Runtime::PawnBodyCount();
    for (int p = 0; p < nPawns && p < 3; ++p) {
        bool isMain = false;
        const uintptr_t pawnBody = Runtime::PawnBodyAt(p, &isMain);
        if (!pawnBody) continue;

        const int slot = isMain ? 0 : p;
        if (!IsPawnCaster(pawnBody, slot)) continue;

        float px = 0, py = 0, pz = 0;
        if (!Rd((const void*)(pawnBody + 0x40), &px, 4) ||
            !Rd((const void*)(pawnBody + 0x44), &py, 4) ||
            !Rd((const void*)(pawnBody + 0x48), &pz, 4))
            continue;

        // Проверяем всех живых врагов рядом с этим кастером
        for (int i = 0; i < w.count; ++i) {
            const WorldPresence& u = w.units[i];
            if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
            if (strstr(u.actName, "Die") || strstr(u.actName, "Dead")) continue;

            const float dx = u.x - px, dy = u.y - py, dz = u.z - pz;
            const float distM = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;

            // Если любой враг находится в упор к кастеру (< 6.0м) — тяжелые касты блокируются!
            if (distM < 6.0f) {
                return false;
            }
        }
    }

    // 2. Проверка одиночного слабого моба:
    // Если на поле всего 1 враг и это не босс — не тратить 15 секунд на Болид!
    int liveEnemies = 0;
    bool bossPresent = false;
    for (int i = 0; i < w.count; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
        if (strstr(u.actName, "Die") || strstr(u.actName, "Dead")) continue;

        const char* k = u.kind;
        if (!strncmp(k, "uEm050", 6) || !strncmp(k, "uEm5", 4)
            || !strncmp(k, "uEm83", 5) || !strncmp(k, "uEm84", 5)
            || !strncmp(k, "uEm81", 5)) {
            bossPresent = true;
        }
        ++liveEnemies;
    }

    // Если остался всего 1 мелкий моб (и не босс) — глушим болиды для быстрого добивания
    if (liveEnemies <= 1 && !bossPresent) {
        return false;
    }

    // Во всех остальных случаях (дистанция безопасна, босс или пачка мобов >= 2) — кастуем свободно!
    return true;
}

// Build 008 bounded logging: ephemeral cCmc action objects may force many
// legitimate re-applies. Keep counters, but print only the first proof and
// the first waiting state; the footer preserves the session totals.
static bool     s_firstApplyLogged = false;
static bool     s_firstWaitLogged = false;
static uint32_t s_applyEvents = 0;
static uint32_t s_waitRetries = 0;

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

static bool IsStaffBand(float mn, float mx)
{
    if (mx < 0.0f || mx > 4000.0f) return false;
    if (mn < 0.0f || (mx > 0.0f && mn > mx + 1.0f)) return false;
    return true;
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

static int PatchSix(uintptr_t base, const char* nm, bool allowNukes)
{
    float f[6] = {};
    if (!Rd((void*)base, f, sizeof(f))) return 0;
    if (!IsStaffBand(f[0], f[1])) return 0;

    const bool isNuke = IsHeavyNukeSpell(nm);
    const bool isFocusedBolt = (nm && (strstr(nm, "WandDX") != nullptr || strstr(nm, "Focused") != nullptr));
    int n = 0;

    if (isNuke && !allowNukes && s_nukeGating) {
        // Условия угрозы в упор или одиночного моба: глушим RangeMax и EnableMax
        if (WriteFloat(base + 4, 0.0f)) ++n;
        if (WriteFloat(base + 20, 0.0f)) ++n;
    } else if (isFocusedBolt) {
        // HFB: минимальная дистанция 4.5 м (450), максимальная 15 м (1500), активация 20 м (2000)
        // Вблизи (< 4.5м) пешка не заряжает HFB, а отстреливается легкими атаками/быстрыми спеллами
        if (WriteFloat(base + 0, 450.0f)) ++n;  // RangeMinXZ = 4.5m
        if (WriteFloat(base + 4, 1500.0f)) ++n; // RangeMaxXZ = 15m
        if (f[5] >= 10.0f && f[5] < 1999.0f) {
            if (WriteFloat(base + 20, 2000.0f)) ++n; // EnableMax = 20m
        }
    } else {
        // Обычные спеллы / Nuke при отсутствии прямой угрозы: дальность 15 м (1500) и активация 20 м (2000)
        if (WriteFloat(base + 4, 1500.0f)) ++n;
        if (f[5] >= 10.0f && f[5] < 1999.0f) {
            if (WriteFloat(base + 20, 2000.0f)) ++n;
        }
    }
    return n;
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

static float CalcDistanceM(uintptr_t pawnBody, uintptr_t targetBody)
{
    if (!pawnBody || !targetBody) return 0.0f;
    float px = 0, py = 0, pz = 0;
    float tx = 0, ty = 0, tz = 0;
    if (!Rd((const void*)(pawnBody + 0x40), &px, 4) ||
        !Rd((const void*)(pawnBody + 0x44), &py, 4) ||
        !Rd((const void*)(pawnBody + 0x48), &pz, 4))
        return 0.0f;
    if (!Rd((const void*)(targetBody + 0x40), &tx, 4) ||
        !Rd((const void*)(targetBody + 0x44), &ty, 4) ||
        !Rd((const void*)(targetBody + 0x48), &tz, 4))
        return 0.0f;
    float dx = px - tx, dy = py - ty, dz = pz - tz;
    return sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
}

static bool AlreadyHave(uintptr_t addr)
{
    for (int i = 0; i < s_nPatch; ++i)
        if (s_patch[i].addr == addr) return true;
    return false;
}

static int ConsiderCmc(uintptr_t obj, bool allowNukes)
{
    if (!obj || !LooksHeap(obj)) return 0;
    char nm[48] = {};
    if (!NameOfLiveObject(obj, nm, sizeof(nm))) return 0;
    if (strncmp(nm, "cCmc", 4) != 0) return 0;
    if (!strcmp(nm, "cCmcInfo")) return 0;

    float f[6] = {};
    if (!Rd((void*)(obj + 0x258), f, sizeof(f))) return 0;

    if (f[1] >= 0.0f && f[1] < 4000.0f) {
        ++s_nSeen;
        NoteBand(f[0], f[1], nm);
    }
    if (!IsWandCmcName(nm)) return 0;
    if (AlreadyHave(obj + 0x25C)) return 0;
    return PatchSix(obj + 0x258, nm, allowNukes);
}

static int WalkObject(uintptr_t obj, uint32_t bytes, int depth, bool allowNukes)
{
    if (!obj || bytes < 8 || bytes > 0x800 || depth > 2) return 0;
    int n = 0;
    n += ConsiderCmc(obj, allowNukes);
    for (uint32_t off = 0; off + 4 <= bytes; off += 4) {
        uintptr_t p = 0;
        if (!RdPtr((void*)(obj + off), &p) || !LooksHeap(p) || p == obj)
            continue;
        char nm[48] = {};
        if (!NameOfLiveObject(p, nm, sizeof(nm)) || !nm[0]) continue;
        if (!strncmp(nm, "cCmc", 4))
            n += ConsiderCmc(p, allowNukes);
        else if (depth < 2 && (!strcmp(nm, "cAIActionInterfaceCmc")
                            || strstr(nm, "ActionInterface")))
            n += WalkObject(p, 640, depth + 1, allowNukes);
    }
    return n;
}

static int WalkPlanWand(uintptr_t ctrl, bool allowNukes)
{
    const uintptr_t planner =
        Runtime::FindChildByClass(ctrl, 704, "cAIGoalPlanning", 0);
    if (!planner) return 0;

    struct QNode { uintptr_t addr; uint32_t bytes; int depth; };
    QNode q[64];
    int n = 0;

    for (uint32_t code = 0; code < 91; ++code) {
        const uintptr_t block = planner + 0x190 + code * 0x110u;
        if (!RegionOk(block, 0x110)) continue;

        int nq = 0, head = 0;
        q[nq].addr = block; q[nq].bytes = 0x110; q[nq].depth = 0; ++nq;

        while (head < nq && nq < 64) {
            const QNode cur = q[head++];
            if (!RegionOk(cur.addr, cur.bytes)) continue;

            for (uint32_t off = 0; off + 4 <= cur.bytes; off += 4) {
                uintptr_t p = 0;
                if (!RdPtr((void*)(cur.addr + off), &p)) continue;
                if (!LooksHeap(p) || p == cur.addr) continue;

                char nm[48] = {};
                const bool named = NameOfLiveObject(p, nm, sizeof(nm)) && nm[0];

                if (named && strncmp(nm, "cCmc", 4) == 0 && strcmp(nm, "cCmcInfo") != 0) {
                    n += ConsiderCmc(p, allowNukes);
                    continue;
                }

                if (cur.depth < 3 && nq < 64 && LooksHeap(p)) {
                    if (named && (strncmp(nm, "uPl", 3) == 0 || strncmp(nm, "uCmc", 4) == 0
                               || strncmp(nm, "uEm", 3) == 0 || strncmp(nm, "uNpc", 4) == 0))
                        continue;
                    q[nq].addr = p; q[nq].bytes = 0x40; q[nq].depth = cur.depth + 1; ++nq;
                }
            }
        }
    }
    return n;
}

static void TrackCasterActions()
{
    const DWORD now = GetTickCount();

    // Check party elemental buff expiry
    if (s_partyElement != ELEM_NONE && s_partyElementExpiryMs != 0xFFFFFFFFu && now >= s_partyElementExpiryMs) {
        s_partyElement = ELEM_HOLY; // reset back to Holy baseline
    }

    const int nPawn = Runtime::PawnBodyCount();
    for (int i = 0; i < nPawn && i < 3; ++i) {
        bool isMain = false;
        const uintptr_t body = Runtime::PawnBodyAt(i, &isMain);
        if (!body) continue;

        const int recordSlot = isMain ? 0 : i;
        if (!IsPawnCaster(body, recordSlot)) continue; // фильтруем: только маги и чародеи!

        PawnCasterTracker& trk = s_tracker[i];
        const char* pawnRole = isMain ? "MainPawn" : (i == 1 ? "Hired1" : "Hired2");

        // Read active action from body
        char act[48] = {};
        Runtime::ReadLiveAct(body, act, sizeof(act));

        // Read target
        uintptr_t combatTarget = 0;
        if (!RdPtr((void*)(body + 0x2EB8), &combatTarget) || !combatTarget) {
            RdPtr((void*)(body + 0x14E0), &combatTarget);
        }

        char targetKind[32] = "enemy";
        if (combatTarget) {
            Runtime::Mem::NameOfLiveObjectSafe((const void*)combatTarget, targetKind, sizeof(targetKind));
        }

        const float targetDist = CalcDistanceM(body, combatTarget);

        // Read active cCmc action
        const uintptr_t ctrl = PawnAICtrl(body);
        char cmcName[48] = {};
        if (ctrl) ReadActiveCmcName(ctrl, cmcName, sizeof(cmcName));

        // Check if this action is wand heavy attack (Focused Bolt)
        const bool isChargingBolt = (!strcmp(act, "cPlActWpnWandHAtckLand")
                                  || !strcmp(act, "cPlActWpnWandHAtckAir")
                                  || !strcmp(cmcName, "cCmcWandDXL")
                                  || !strcmp(cmcName, "cCmcWandDXAirL"));

        const bool isFiringBolt = (!strcmp(act, "cPlActWpnWandShot"));

        const bool isRealSpell = IsWandCmcName(cmcName) && strcmp(cmcName, "cCmcWandDXL") && strcmp(cmcName, "cCmcWandDXAirL");

        const bool isChantingSpell = (!strcmp(act, "cPlActWpnWandBase") || isRealSpell);

        const bool isChanneling = (!strcmp(act, "cPlActWpnWandThunderSpark")
                                || !strcmp(act, "cPlActWpnWandWhip")
                                || !strcmp(act, "cPlActWpnWandFunnel"));

        const bool isInterrupted = (strstr(act, "Dmg") != nullptr
                                 || strstr(act, "Dead") != nullptr
                                 || strstr(act, "Evasion") != nullptr
                                 || strstr(act, "Fall") != nullptr
                                 || strstr(act, "Down") != nullptr);

        // --- FSM Transitions ---
        if (isChargingBolt) {
            if (trk.fsmState != CFSM_CHARGING_BOLT) {
                trk.fsmState = CFSM_CHARGING_BOLT;
                trk.stateStartMs = now;
                trk.targetBody = combatTarget;
                lstrcpynA(trk.targetKind, targetKind, sizeof(trk.targetKind));
                trk.targetDistM = targetDist;
                trk.appliedElement = s_partyElement;
                ++s_boltsCharged;

                const char* elemStr = ElementName(trk.appliedElement);
                sprintf_s(s_lastCasterEvent, sizeof(s_lastCasterEvent),
                          "[%s] CHARGING Focused Bolt [%s] -> %s (%.1fm)",
                          pawnRole, elemStr, targetKind, targetDist);

                char l[256];
                sprintf_s(l, "CasterWatch: [%s] START CHARGING Focused Bolt [%s] -> target 0x%08X (%s) dist=%.1fm",
                          pawnRole, elemStr, (unsigned)combatTarget, targetKind, targetDist);
                logFile << l << std::endl;
            }
        }
        else if (isFiringBolt) {
            if (trk.fsmState == CFSM_CHARGING_BOLT) {
                const DWORD dur = trk.stateStartMs ? (now - trk.stateStartMs) : 0;
                trk.fsmState = CFSM_IDLE;
                ++s_boltsFired;

                const char* elemStr = ElementName(trk.appliedElement);
                sprintf_s(s_lastCasterEvent, sizeof(s_lastCasterEvent),
                          "[%s] FIRED Focused Bolt [%s] -> %s (%.1fm, %ums)",
                          pawnRole, elemStr, trk.targetKind, targetDist, (unsigned)dur);

                char l[256];
                sprintf_s(l, "CasterWatch: [%s] FIRED Focused Bolt [%s] -> target 0x%08X (%s) dist=%.1fm (charge=%ums)",
                          pawnRole, elemStr, (unsigned)combatTarget, trk.targetKind, targetDist, (unsigned)dur);
                logFile << l << std::endl;
            }
        }
        else if ((isChantingSpell || isChanneling) && isRealSpell) {
            if (trk.fsmState != CFSM_CHANTING_SPELL && trk.fsmState != CFSM_CHANNELING) {
                trk.fsmState = isChanneling ? CFSM_CHANNELING : CFSM_CHANTING_SPELL;
                trk.stateStartMs = now;
                trk.targetBody = combatTarget;
                lstrcpynA(trk.targetKind, targetKind, sizeof(trk.targetKind));
                trk.targetDistM = targetDist;
                lstrcpynA(trk.activeCmc, cmcName[0] ? cmcName : act, sizeof(trk.activeCmc));
                ++s_spellsChanted;

                const char* friendly = FriendlySpellName(trk.activeCmc);
                sprintf_s(s_lastCasterEvent, sizeof(s_lastCasterEvent),
                          "[%s] CHANTING %s -> %s (%.1fm)",
                          pawnRole, friendly, targetKind, targetDist);

                char l[256];
                sprintf_s(l, "CasterWatch: [%s] START CHANTING %s (%s) -> target 0x%08X (%s) dist=%.1fm",
                          pawnRole, friendly, trk.activeCmc, (unsigned)combatTarget, targetKind, targetDist);
                logFile << l << std::endl;
            }
        }
        else {
            // Action is none of the above (idle, walking, shot finished, or interrupted)
            if (trk.fsmState == CFSM_CHARGING_BOLT) {
                const DWORD dur = trk.stateStartMs ? (now - trk.stateStartMs) : 0;
                if (isInterrupted) {
                    char l[256];
                    sprintf_s(l, "CasterWatch: [%s] Focused Bolt INTERRUPTED (act=%s, charge=%ums)",
                              pawnRole, act, (unsigned)dur);
                    logFile << l << std::endl;
                } else if (dur >= 400) {
                    // Bolt was released
                    ++s_boltsFired;
                    const char* elemStr = ElementName(trk.appliedElement);
                    char l[256];
                    sprintf_s(l, "CasterWatch: [%s] FIRED Focused Bolt [%s] -> target 0x%08X (%s) dist=%.1fm (charge=%ums)",
                              pawnRole, elemStr, (unsigned)trk.targetBody, trk.targetKind, targetDist, (unsigned)dur);
                    logFile << l << std::endl;
                }
                trk.fsmState = CFSM_IDLE;
            }
            else if (trk.fsmState == CFSM_CHANTING_SPELL || trk.fsmState == CFSM_CHANNELING) {
                const DWORD dur = trk.stateStartMs ? (now - trk.stateStartMs) : 0;
                const char* friendly = FriendlySpellName(trk.activeCmc);

                if (isInterrupted) {
                    char l[256];
                    sprintf_s(l, "CasterWatch: [%s] SPELL INTERRUPTED %s (act=%s, chant=%ums)",
                              pawnRole, friendly, act, (unsigned)dur);
                    logFile << l << std::endl;
                } else if (dur >= 800) {
                    ++s_spellsCompleted;
                    sprintf_s(s_lastCasterEvent, sizeof(s_lastCasterEvent),
                              "[%s] CAST COMPLETED %s -> %s (%ums)",
                              pawnRole, friendly, trk.targetKind, (unsigned)dur);

                    char l[256];
                    sprintf_s(l, "CasterWatch: [%s] SPELL CAST COMPLETED %s (%s) -> target 0x%08X (%s) (chant=%ums)",
                              pawnRole, friendly, trk.activeCmc, (unsigned)trk.targetBody, trk.targetKind, (unsigned)dur);
                    logFile << l << std::endl;

                    // If it was an enchantment spell, set party buff!
                    if (strstr(trk.activeCmc, "EnchantHorly") || strstr(trk.activeCmc, "EnchantHoly")) {
                        s_partyElement = ELEM_HOLY;
                        s_partyElementExpiryMs = now + 90000;
                        logFile << "CasterWatch: PARTY BUFF ACTIVATED -> Holy Affinity [HFB Active for 90s]" << std::endl;
                    } else if (strstr(trk.activeCmc, "EnchantFire")) {
                        s_partyElement = ELEM_FIRE;
                        s_partyElementExpiryMs = now + 90000;
                        logFile << "CasterWatch: PARTY BUFF ACTIVATED -> Fire Affinity [Active for 90s]" << std::endl;
                    } else if (strstr(trk.activeCmc, "EnchantFrost") || strstr(trk.activeCmc, "EnchantIce")) {
                        s_partyElement = ELEM_ICE;
                        s_partyElementExpiryMs = now + 90000;
                        logFile << "CasterWatch: PARTY BUFF ACTIVATED -> Ice Affinity [Active for 90s]" << std::endl;
                    } else if (strstr(trk.activeCmc, "EnchantThunder")) {
                        s_partyElement = ELEM_LIGHTNING;
                        s_partyElementExpiryMs = now + 90000;
                        logFile << "CasterWatch: PARTY BUFF ACTIVATED -> Thunder Affinity [Active for 90s]" << std::endl;
                    } else if (strstr(trk.activeCmc, "EnchantDark")) {
                        s_partyElement = ELEM_DARK;
                        s_partyElementExpiryMs = now + 90000;
                        logFile << "CasterWatch: PARTY BUFF ACTIVATED -> Dark Affinity [Active for 90s]" << std::endl;
                    }
                }
                trk.fsmState = CFSM_IDLE;
            }
        }
    }
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
    memset(s_tracker, 0, sizeof(s_tracker));
    lstrcpynA(s_why, why ? why : "restored", sizeof(s_why));
    if (had)
        logFile << "WandRange: restored (" << s_why << ")" << std::endl;
}

void Init()
{
    s_enabled = config.getBool("errata", "wandRange", false);
    s_nukeGating = config.getBool("errata", "nukeGating", true);
    s_firstApplyLogged = false;
    s_firstWaitLogged = false;
    s_applyEvents = 0;
    s_waitRetries = 0;
    s_boltsCharged = 0;
    s_boltsFired = 0;
    s_spellsChanted = 0;
    s_spellsCompleted = 0;
    s_partyElement = ELEM_HOLY;
    s_partyElementExpiryMs = 0xFFFFFFFFu;
    memset(s_tracker, 0, sizeof(s_tracker));
    lstrcpynA(s_lastCasterEvent, "idle", sizeof(s_lastCasterEvent));

    logFile << "WandRange: " << (s_enabled ? "enabled" : "disabled")
            << " (all caster cCmc + Anodyne/Cure/Circle/FocusedBolt, nukeGating=" << (s_nukeGating ? "on" : "off")
            << ", 1-10 m -> 15 m)"
            << std::endl;
    logFile << "CasterWatch: live HFB and spell tracking initialized" << std::endl;
}

void SetEnabled(bool on)
{
    s_enabled = on;
    if (!on) Restore("off");
}

bool NukeGatingOn() { return s_nukeGating; }

void SetNukeGating(bool on)
{
    s_nukeGating = on;
    s_applied = false; // принудительно обновить патчи на следующем тике
}

void SetPartyElementBuff(ElementType elem, uint32_t durationMs)
{
    const DWORD now = GetTickCount();
    s_partyElement = elem;
    s_partyElementExpiryMs = now + durationMs;
    char l[128];
    sprintf_s(l, "CasterWatch: manual party element set to [%s] for %us",
              ElementName(elem), durationMs / 1000);
    logFile << l << std::endl;
}

void Tick()
{
    if (!s_enabled) {
        if (s_nPatch) Restore("off");
        else lstrcpynA(s_why, "off", sizeof(s_why));
        return;
    }

    const bool allowNukes = !s_nukeGating || ShouldAllowNukes();

    if (allowNukes != s_lastAllowNukes) {
        if (allowNukes) {
            logFile << "WandRange: NUKE GATING UNLOCKED (Safe distance / Target pack engaged) -> heavy nukes (Bolide/Maelstrom/Seism) active at 15m" << std::endl;
        } else {
            logFile << "WandRange: NUKE GATING ENGAGED (Enemy in melee <6m or lone trash mob) -> heavy nukes gated to fast spells (Comestion/Levin/HFB)" << std::endl;
        }
        // Сбросить статус удержания, чтобы перепатчить planner
        s_applied = false;
        s_nPatch = 0;
    }

    if (s_applied && s_nPatch > 0 && allowNukes == s_lastAllowNukes) {
        int ok = 0;
        for (int i = 0; i < s_nPatch; ++i) {
            float cur = 0.0f;
            if (Rd((void*)s_patch[i].addr, &cur, 4) && Near(cur, s_patch[i].now))
                ++ok;
        }
        if (ok == s_nPatch) {
            lstrcpynA(s_why, "holding", sizeof(s_why));
            TrackCasterActions();
            return;
        }
        s_nPatch = 0;
        s_applied = false;
    }

    s_lastAllowNukes = allowNukes;

    static DWORD s_lastTry = 0;
    const DWORD now = GetTickCount();
    if (!s_applied && s_lastTry && now - s_lastTry < 1000) {
        TrackCasterActions();
        return;
    }
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
            n += WalkObject(iface, 60, 0, allowNukes);
        }
        n += WalkPlanWand(ctrl, allowNukes);
    }
    s_nLive = n;
    s_applied = n > 0;

    if (s_applied) {
        ++s_applyEvents;
        sprintf_s(s_why, "APPLIED live cCmc %d (seen %d, nukes=%s)", n, s_nSeen, allowNukes ? "UNLOCKED" : "GATED");
        if (!s_firstApplyLogged) {
            s_firstApplyLogged = true;
            logFile << "WandRange: first " << s_why << std::endl;
            LogBands("first applied");
        }
        TrackCasterActions();
        return;
    }

    ++s_waitRetries;
    if (nCtrl == 0) {
        lstrcpynA(s_why, "cAICtrl not resolved", sizeof(s_why));
    } else if (nIface == 0) {
        lstrcpynA(s_why, "cAIActionInterfaceCtrl not on cAICtrl", sizeof(s_why));
    } else if (s_nSeen == 0) {
        lstrcpynA(s_why, "no cCmc range block yet (draw the staff / enter combat)",
                  sizeof(s_why));
    } else {
        sprintf_s(s_why, "saw %d cCmc ranges, none patchable", s_nSeen);
    }

    if (!s_firstWaitLogged) {
        s_firstWaitLogged = true;
        logFile << "WandRange: waiting: " << s_why
                << " (further unchanged retries counted silently)" << std::endl;
        if (s_nSeen) LogBands("first seen");
    }

    TrackCasterActions();
}

void Shutdown()
{
    const bool report = s_enabled || s_applyEvents || s_waitRetries || s_boltsCharged || s_spellsChanted;
    Restore("shutdown");
    if (report) {
        logFile << "WandRange: shutdown summary applyEvents=" << s_applyEvents
                << " waitRetries=" << s_waitRetries
                << " firstApply=" << (s_firstApplyLogged ? 1 : 0)
                << std::endl;
        logFile << "CasterWatch: session summary boltsCharged=" << s_boltsCharged
                << " boltsFired=" << s_boltsFired
                << " spellsChanted=" << s_spellsChanted
                << " spellsCompleted=" << s_spellsCompleted
                << std::endl;
    }
}

Status Get()
{
    Status s;
    s.enabled = s_enabled;
    s.applied = s_applied;
    s.nResource = s_nSeen;
    s.nLive = s_nLive;
    s.nukeGated = s_nukeGating && !s_lastAllowNukes;
    s.boltsCharged = s_boltsCharged;
    s.boltsFired = s_boltsFired;
    s.spellsChanted = s_spellsChanted;
    s.spellsCompleted = s_spellsCompleted;
    s.activeBuff = s_partyElement;

    const DWORD now = GetTickCount();
    s.buffRemainingSec = (s_partyElement != ELEM_NONE && s_partyElementExpiryMs > now && s_partyElementExpiryMs != 0xFFFFFFFFu)
        ? ((s_partyElementExpiryMs - now) / 1000) : 0;

    lstrcpynA(s.lastCasterEvent, s_lastCasterEvent, sizeof(s.lastCasterEvent));
    lstrcpynA(s.why, s_why, sizeof(s.why));
    return s;
}

} // namespace WandRange
} // namespace PawnAI

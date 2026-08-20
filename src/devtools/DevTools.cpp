/**
 * DevTools.cpp — TypeAtlas + vtable scan + sUnit anatomy + heap hunt
 *
 * Confirmed on Steam (no ASLR, base 0x400000):
 *   FactoryPointer slots are 0 — not the live singleton.
 *   sUnit dataObj 0x018B6DBC is a vtable dword in .data, NOT a 1.7MB table.
 *   sizeof(sUnit)=1700720 does not fit in SizeOfImage (ends past 0x019CC000).
 *   Blind dword walk of that range found 0 characters next to goblins.
 *   Pool +0x44 len=50 stride 0x1FE0: workspaces, not MtObjects.
 *   inside[] of those blocks = tagged AABB/geom (50505050 / D0D0D0D0 / …),
 *   not uEm*. Pool is closed as a unit list.
 *
 * DUMP = anatomy only (safe). HUNT = derive instance vt + heap census.
 * HUNT refuses unless a save is loaded (InWorld).
 * Zip 12 facts: TSV FactoryVtable for uEm0100 is a path string, not a vtable.
 * Zip 13: 48 bytes BEFORE Caller. Last LLV dword = rescued. Goblin rescued=0
 *   because the stub uses `push imm32` (68), not C7. uPawnIntel stole sSetManager.
 * Zip 14: parse stub. Dump8: uEm0100 Caller registers uEm0100_20 (nameHex).
 *   DTI in .data: [0]=factory vt, [4]=name*, [+0x18]=size|(flags<<16).
 * Zip 15: DTI by exact name. Dump9: uEm0100 DTI @ 0x01977BA0 size=29632 typeId=5.
 *   DTI[0] is MtDTI vt (create=0x00BFF9A0 shared), NOT create_uEm / instance vt.
 * Zip 16: factory vt is NOT in the 128-byte card. Card is 0x20-stride DTI cluster.
 *   Every DTI vt[0]=shared create; vt[4+] are unique .text methods.
 * Zip 17: scan those methods for `mov [obj], inst_vt` (same as derive).
 * Zip 18 / dump11: vt[4] FIRST hit is a PARENT vt. uEm0100 and uEm0900 both
 *   wrote 0x015497F8 (not gargoyle gold 0x015B5A80). live=3 fake uNpc.
 *   8x gid 0x05 scouts — packed tables, not 29KB actors.
 *   18: harvest ALL DTI slots + one-level E8 callees; pick UNIQUE inst only;
 *   never overwrite a good factory-derived DIFF; xref gold fact/inst in image;
 *   dump DTI +08/+0C/+10/+14; follow heap ptrs out of gid-0x05 / cLinkUnitEnemy.
 * Zip 19 / dump12: DTI tree is MtDTI +08=next +0C=child +10=parent.
 *   uEm0100.parent=uEnemy, child=uEm0101. Unique pick was DTI-neighbor
 *   (+0x50 from card vt) — not a species. Xref cap filled by create_uPlayer.
 *   19: reject inst near any DTI.vt; xref rare gold only, no create, 4/tag;
 *   keep factory-DIFF even if shared; walk uEnemy tree; leads 256B + back-off.
 * Zip 20 / dump13: tree[] is the full uEnemy taxonomy, goblin first child.
 *   xrefs empty: gold inst/fact are .text immediates, not .data tables.
 *   unique pick 0x0157C450 = GUI cluster via callee. live=0 honest.
 *   20: never pick callee as species; scan factory VA +/- 0x200 for LLV;
 *   census .text C7 writes of rare vt; 128B bodies of list nodes;
 *   leads require user-heap ptrs only.
 * Zip 21 / dump14: near=0 because we tested *at, not at. writes[] filled
 *   with early .text 0x01539xxx, gold 0x015B5A80 never entered acc[96].
 *   nodes +40/+44/+48 = Gransys XYZ. List trackers, not 29KB actors.
 *   21: LooksLikeVtable(at); writes only in 0x01580000-0x01620000 band;
 *   node body 256B; signed factory offset.
 * Zip 22 / dump15: near[] filled 48 mid-vtable dwords at 0x015A0274+4.
 *   Never reached factory+0 or +0xD8. writes[] filled 0x01580xxx.
 *   nodes +F8 = heap ptr. 22: only vtable STARTS; writes vt>=0x015A0000
 *   plus gold; dump 32B at node+F8.
 * Zip 23 / dump16: START filter hid packed gold uEm0900 fact/inst.
 *   near has no gargoyle. writes[] = cEm0100Act* 0x48-stride, no 0x015B5A80.
 *   23: always record near off 0 / +D8 / -10 if LLV; emit gold writes always;
 *   also scan B8+89 like DeriveOne; dump 64B of create_uEm0900.
 * Zip 24 / dump17: create_uEm0900 first C7 writes SHARED 0x01574748.
 *   meth4_uEm0100: push DTI 0x01977BA0, push 0x73C0, mov [edi], 0x015852A8.
 *   That vt is the live goblin [0] — we rejected it as DTI-neighbor 0x35C.
 *   24: WatchAdd 0x015852A8 as uEm0100; probe +0x73BF; dump meth4 128B.
 * Zip 25 / dump18: live=4 uEm0100 gid=5 @ 0x10DD0060..  fat29 on first.
 *   meth4 writes movss [edi+0x601C..] and parent vt at +0x6150 — 29KB actor.
 *   +40/+44/+48 = world XYZ. +0C/+10 = doubly-linked live list.
 *   25: actors[] anatomy; walk +0C chain; fat29 per actor.
 * Zip 26 / dump19: 4 live goblins before combat. +0C walk found 13 mixed
 *   units. Far goblins are LOT, not uEm. 26: label kinds; WatchAdd neighbors;
 *   +0x6150 only on goblin vt.
 * Zip 27 / dump21: no crash. world{10,4,cat0}. Follower goblin GPS moved.
 *   TSV create_uEm8000 = stub `mov al,1; ret 4`. create_uEm8600 = method, not ctor.
 * Zip 28 / dump22: tree=93, DTI uEm8000 @ 0x01981868. meth4 writes 0x015BB278
 *   and stamps +0x2D = 0x61. List dies when all seeds unload — tick stuck at 0.
 * Zip 29 / dump23: singles reseeded. Pack of 3 missed — poll started at
 *   lastBand-2MB and walked UP past 0x10DD. meth4_uEm8600 writes 0x015BD9D0.
 * Zip 30 / dump24: hot ring 0x10000000-0x18000000. Packs and singles
 *   register without a second Hunt. Engine unload is hysteretic (LOS +
 *   hundreds of meters) — not a scanner leak. CombatIntel.cpp untouched.
 * Zip 31: first 256B is transform, not HP. Inspect +offset. HUNT stores
 *   +14 / +0x5BD0 / +0x6000. Do not treat +14 0x12 or +4C=0 as death.
 * Zip 32 / cleanup: removed two subsystems this log had already closed.
 *   POOL (FollowPool/CollectInside/ScanBlockForChars/BlockOccupied,
 *     g_pool/g_ins + json blocks[]/inside[] + 2 UI trees) — sUnit+0x44 is
 *     geometry workspaces, never uEm*. Closed in the Zip 11 notes above.
 *   XREFS (ScanXrefs, g_xref, json xrefs[], 1 UI tree) — gold inst/fact are
 *     .text immediates, not .data tables; it returned 0 every run (Zip 20).
 *     ScanTextWrites is the tool that actually finds those writes — kept.
 *   pWorld coverage kept: ScanRegionPtrs(512) is wider than the old
 *     CollectInside(128) call it replaced. Do NOT re-add either subsystem.
 */
#include "stdafx.h"
#include <math.h>
#include "BuildTag.h"
#include "../runtime/MemProbe.h"
#include "../runtime/RuntimeInternal.h"
#include "EnemyTuner.h"
#include "TypeAtlas.Generated.h"
#include "EnemyTypes.Generated.h"
#include "ActMap.Generated.h"
#include "devtools/DevTools.h"
#include "devtools/AnimProbe.h"
#include "devtools/GoapProbe.h"
#include "devtools/TypeCallers.Generated.h"
#include "devtools/generated/PawnPrioritySemantics.inl"
#include "devtools/generated/GoapInterfaceMap.inl"
#include "CombatBus.h"
#include "ModPaths.h"
#include <stdio.h>
#include <stdlib.h>

// Build 69: фундамент (чтение памяти, секции образа, DTI-имена) переехал
// в Runtime::Mem. Директива оставлена, чтобы 200+ существующих обращений
// не переписывать поимённо — слой при этом уже разделён физически.
using namespace Runtime::Mem;
using namespace Runtime;   // Build 69: продуктовый слой переехал в src/runtime/

extern BYTE *codeBase, *codeEnd, *dataBase, *dataEnd;

// ---------------------------------------------------------------------------
// Флаги исследовательского слоя. Build 69: продукт от них НЕ зависит —
// рантайм живёт в src/runtime/ и работает при любом значении.
// ---------------------------------------------------------------------------
// Панель DevTools и её пробы. Дефолт OFF: игроку не нужны.
static bool g_enabled = false;
// Build 56.5: выключатель исследовательских дампов (JSON/CSV). По умолчанию OFF.
// Когда ON, кнопка «Find both + capture baseline» пишет:
//   ddda_party_recon_%03d.json, ddda_pawn_ai_bridge_%03d.json,
//   ddda_pawn_intent_trace_%03d.csv (trace растёт всю сессию).
// Для продуктовой работы (Guardian doctrine) не нужны — включать только на
// время охоты за редкими semantic codes.
static bool g_researchDump = false;
// Файл живой трассировки интентов. Открыт => трассировка идёт.
static FILE* g_intentTrace = nullptr;


// Build 56.5: выключатель исследовательских дампов (JSON/CSV). По умолчанию OFF.
// Когда ON, кнопка «Find both + capture baseline» пишет:
//   ddda_party_recon_%03d.json, ddda_pawn_ai_bridge_%03d.json,
//   ddda_pawn_intent_trace_%03d.csv (trace растёт всю сессию).
// Для продуктовой работы (Guardian doctrine) не нужны — включать только на
// время охоты за редкими semantic codes.


static bool IsRepeatTag(uint32_t v)
{
    if (!v || v == 0xFFFFFFFFu) return false;
    uint8_t b = (uint8_t)v;
    return v == (uint32_t)b * 0x01010101u;
}

// ─── factory-slot probe (first experiment — returns 0) ─────────
enum ProbeKind { PK_UNREADABLE = 0, PK_INPLACE, PK_POINTER, PK_EMPTY, PK_UNKNOWN };

struct Probe {
    ProbeKind   kind;
    uintptr_t   slot, raw, instance, instanceVt;
};

static const char* KindName(ProbeKind k)
{
    switch (k) {
    case PK_INPLACE:  return "inplace";
    case PK_POINTER:  return "pointer";
    case PK_EMPTY:    return "empty";
    case PK_UNKNOWN:  return "unknown";
    default:          return "unreadable";
    }
}

static Probe ProbeType(const TypeAtlas::Info& t)
{
    Probe r{};
    r.kind = PK_UNREADABLE;
    if (!g_base || !t.factoryRVA) return r;
    r.slot = g_base + t.factoryRVA;
    uintptr_t d0 = 0;
    if (!RdPtr((void*)r.slot, &d0)) return r;
    r.raw = d0;
    if (d0 == 0) { r.kind = PK_EMPTY; return r; }

    const uintptr_t wantVt = g_base + t.factoryVtRVA;
    if (d0 == wantVt) {
        r.kind = PK_INPLACE; r.instance = r.slot; r.instanceVt = d0; return r;
    }
    uintptr_t vt = 0;
    if (d0 > 0x10000 && RdPtr((void*)d0, &vt)) {
        r.instanceVt = vt;
        if (vt == wantVt) { r.kind = PK_POINTER; r.instance = d0; return r; }
    }
    r.kind = PK_UNKNOWN;
    return r;
}

uintptr_t DevTools::ModuleBase() { return g_base; }
uintptr_t DevTools::Rebase(uint32_t rva) { return (g_base && rva) ? (g_base + rva) : 0; }

static const uint32_t kStubVtRVA = 0x11CEF40;

const TypeAtlas::Info* DevTools::Identify(const void* ptr)
{
    if (!g_base || !ptr) return nullptr;
    uintptr_t vt = 0;
    if (!RdPtr(ptr, &vt) || vt < g_base) return nullptr;
    uint32_t rva = (uint32_t)(vt - g_base);
    if (rva == kStubVtRVA) return nullptr; // hundreds of types share this
    return TypeAtlas::FindByFactoryVTable(rva);
}

static const char* kManagers[] = {
    "sEnemyManager", "sHumanEnemyManager", "sPlayerManager", "sPawnManager",
    "sNpcManager", "sSetManager", "sUnit", "sUnitExt", "sWeatherManager",
    "sQuestManager", "sLockOnManager", "sArchiveManager", "sItemManager",
    "sCharacterBaseManager", "sEventManager", "sResource", "sArea",
    "sSave", "uPlayer", "uCameraGame",
    "sRecognition", "sUnitSearchManager", "sBbsRpgMain", "sGameSys",
    "sMain", "sAreaExt", nullptr
};

// ─── vtable scan inside the exe image ────────────────────────
#define MAX_HITS 8
struct TypeScan {
    const TypeAtlas::Info* t;
    uintptr_t wantVt;
    uintptr_t dataObj[MAX_HITS];
    uintptr_t dataPtr[MAX_HITS];
    int nDataObj, nDataPtr, nCode;
    bool skipped;
};

static TypeScan g_scans[32];
static int g_nScans = 0;
static bool g_scanned = false;
static DWORD g_scanMs = 0;

static bool IsSharedStub(uint32_t factoryVtRVA)
{
    return factoryVtRVA == kStubVtRVA;
}

static void AddHit(uintptr_t* arr, int* n, uintptr_t v)
{
    if (*n >= MAX_HITS) return;
    for (int i = 0; i < *n; ++i) if (arr[i] == v) return;
    arr[(*n)++] = v;
}

static void ScanImage()
{
    g_nScans = 0;
    g_scanned = false;
    if (!g_base || !g_imageSize) return;

    DWORD t0 = MsNow();

    for (int i = 0; kManagers[i] && g_nScans < 32; ++i) {
        const TypeAtlas::Info* t = TypeAtlas::FindByName(kManagers[i]);
        if (!t) continue;
        TypeScan& s = g_scans[g_nScans++];
        memset(&s, 0, sizeof(s));
        s.t = t;
        s.wantVt = g_base + t->factoryVtRVA;
        s.skipped = IsSharedStub(t->factoryVtRVA);
    }

    auto dos = (IMAGE_DOS_HEADER*)g_base;
    auto nt  = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    const int nsec = nt->FileHeader.NumberOfSections;

    __try {
        for (int si = 0; si < nsec; ++si) {
            const bool exec  = (sec[si].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            const bool write = (sec[si].Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            uintptr_t s0 = g_base + sec[si].VirtualAddress;
            uint32_t  sz = sec[si].Misc.VirtualSize;
            if (!sz) continue;
            if (sec[si].VirtualAddress + sz > g_imageSize)
                sz = g_imageSize - sec[si].VirtualAddress;

            uint32_t* p = (uint32_t*)s0;
            uint32_t  n = sz / 4;
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t val = p[i];
                uintptr_t here = (uintptr_t)&p[i];

                for (int k = 0; k < g_nScans; ++k) {
                    TypeScan& s = g_scans[k];
                    if (s.skipped) continue;
                    if ((uintptr_t)val == s.wantVt) {
                        if (exec) s.nCode++;
                        else AddHit(s.dataObj, &s.nDataObj, here);
                    }
                }
            }
            (void)write;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        logFile << "DevTools: scan exception" << std::endl;
    }

    g_scanMs = MsNow() - t0;
    g_scanned = true;

    logFile << "DevTools: image scan " << g_scanMs << " ms, imageSize=0x"
            << std::hex << g_imageSize << std::dec << std::endl;
    for (int k = 0; k < g_nScans; ++k) {
        TypeScan& s = g_scans[k];
        logFile << "  " << s.t->name;
        if (s.skipped) { logFile << " SKIP shared stub vt" << std::endl; continue; }
        logFile << " codeRefs=" << s.nCode
                << " dataObj=" << s.nDataObj
                << " dataPtr=" << s.nDataPtr;
        if (s.nDataObj) logFile << " obj0=0x" << std::hex << s.dataObj[0] << std::dec;
        logFile << std::endl;
    }
}

static void WriteScanJson()
{
    FILE* f = nullptr;
    if (fopen_s(&f, "ddda_address_map.json", "w") != 0 || !f) return;
    fprintf(f,
        "{\n  \"moduleBase\":\"0x%08X\",\n  \"imageSize\":\"0x%X\",\n  \"imageEnd\":\"0x%08X\",\n  \"scanMs\":%u,\n"
        "  \"note\":\"dataObj = vtable dword in a data section. sizeof(sUnit)=1.7MB does NOT fit in the image — that hit is not a unit table. Use DUMP for anatomy.\",\n"
        "  \"managers\":[\n",
        (unsigned)g_base, g_imageSize, (unsigned)ImageEnd(), g_scanMs);

    for (int k = 0; k < g_nScans; ++k) {
        TypeScan& s = g_scans[k];
        if (k) fputs(",\n", f);
        fprintf(f,
            "    {\"name\":\"%s\",\"size\":%u,\"factoryVtVA\":\"0x%08X\",\"skipped\":%s,"
            "\"codeRefs\":%d,\"dataObj\":[",
            s.t->name, s.t->size, (unsigned)s.wantVt, s.skipped ? "true" : "false", s.nCode);
        for (int i = 0; i < s.nDataObj; ++i)
            fprintf(f, "%s\"0x%08X\"", i ? "," : "", (unsigned)s.dataObj[i]);
        fputs("]}", f);
    }
    fputs("\n  ]\n}\n", f);
    fclose(f);
    logFile << "DevTools: wrote ddda_address_map.json (scan results)" << std::endl;
}

// ─── watch list: small, used on every hop. NOT the full 4405 atlas. ─
enum WatchKind { WK_OTHER = 0, WK_CHAR, WK_WRAPPER, WK_MOVELINE, WK_UNITGROUP, WK_LAYOUT, WK_MANAGER };

struct Watch {
    uint32_t    rva;
    const char* name;
    uint8_t     gid;
    uint8_t     kind;
};
static Watch g_watch[224];
static int   g_nWatch = 0;

static void WatchAdd(uint32_t rva, const char* name, uint8_t gid, uint8_t kind)
{
    if (!rva || !name || g_nWatch >= 224) return;
    if (rva == kStubVtRVA) return;
    for (int i = 0; i < g_nWatch; ++i)
        if (g_watch[i].rva == rva) return;
    Watch& w = g_watch[g_nWatch++];
    w.rva = rva; w.name = name; w.gid = gid; w.kind = kind;
}

static uint8_t KindForName(const char* n)
{
    if (!n) return WK_OTHER;
    if (!strcmp(n, "sUnit::MoveLine")) return WK_MOVELINE;
    if (!strcmp(n, "sUnit::UnitGroup")) return WK_UNITGROUP;
    if (!strcmp(n, "uPawnIntel"))
        return WK_WRAPPER; // 56-byte component. Dump7 signed sSetManager guts as live.
    if (!strncmp(n, "uEm", 3) || !strncmp(n, "uNpc", 4) || !strncmp(n, "uCmc", 4)
        || !strcmp(n, "uPlayer") || !strcmp(n, "uHumanEnemy")
        || !strcmp(n, "uMultiNpc"))
        return WK_CHAR;
    if (!strcmp(n, "uCoord") || !strcmp(n, "cUnit") || !strcmp(n, "uCharacterBase")
        || !strcmp(n, "uEnemy") || !strcmp(n, "uPlayerBase"))
        return WK_WRAPPER;
    if (!strncmp(n, "cLayoutSet", 10) || !strncmp(n, "cSetInfoEnemy", 13)
        || !strncmp(n, "cLinkUnit", 9) || strstr(n, "cUnitData"))
        return WK_LAYOUT;
    if (n[0] == 's') return WK_MANAGER;
    return WK_OTHER;
}

static void BuildWatch()
{
    g_nWatch = 0;
    for (const EnemyTypeInfo* e = g_enemyTypes; e->uEmName; ++e)
        WatchAdd(e->vtableRVA, e->uEmName, e->groupId, WK_CHAR);

    static const char* kExtra[] = {
        "uPlayer", "uCmc", "uNpc", "uHumanEnemy", "uPawnIntel", "uMultiNpc",
        "uCharacterBase", "uEnemy", "uCoord", "cUnit", "uPlayerBase",
        "sUnit::MoveLine", "sUnit::UnitGroup",
        "cLayoutSetEnemy", "cLayoutSetNpc", "cLayoutSetOm", "cLinkUnit",
        "cLinkUnitEnemy", "cSetInfoEnemy", "sSetManager::cUnitData",
        "sSetManager::cLotMgr<cLayoutSetEnemy>",
        "sEnemyManager", "sHumanEnemyManager", "sPawnManager", "sNpcManager",
        "sUnit", "sUnitExt", "sSetManager", "sPlayerManager",
        "sRecognition", "sRecognition::cEnemyInfo",
        "sLockOnManager", "sLockOnManager::cLockOnTarget",
        "sUnitSearchManager",
        "sBbsRpgMain", "sGameSys", "sMain", "sArea", "sAreaExt",
        "uCameraGame", "uCamera", "uCameraCtrl",
        nullptr
    };
    for (int i = 0; kExtra[i]; ++i) {
        const TypeAtlas::Info* t = TypeAtlas::FindByName(kExtra[i]);
        if (t) WatchAdd(t->factoryVtRVA, t->name, (uint8_t)t->typeId, KindForName(t->name));
    }
}

static const Watch* WatchVt(uint32_t rva)
{
    for (int i = 0; i < g_nWatch; ++i)
        if (g_watch[i].rva == rva) return &g_watch[i];
    return nullptr;
}

struct Named {
    const char* name;
    uint8_t     gid;
    uint8_t     kind;
    uint32_t    rva;
};

static Named NameOf(uintptr_t obj)
{
    Named n{};
    uintptr_t vt = 0;
    if (!RdPtr((void*)obj, &vt) || !InImage(vt)) return n;
    n.rva = (uint32_t)(vt - g_base);
    if (n.rva == kStubVtRVA) { n.name = "(shared stub)"; n.kind = WK_OTHER; return n; }
    if (const Watch* w = WatchVt(n.rva)) {
        n.name = w->name; n.gid = w->gid; n.kind = w->kind; return n;
    }
    if (const EnemyTypeInfo* e = FindByVTable(n.rva)) {
        n.name = e->uEmName; n.gid = e->groupId; n.kind = WK_CHAR; return n;
    }
    if (const TypeAtlas::Info* a = TypeAtlas::FindByFactoryVTable(n.rva)) {
        n.name = a->name; n.gid = (uint8_t)a->typeId; n.kind = KindForName(a->name);
        return n;
    }
    return n;
}

static bool IsCharKind(uint8_t k) { return k == WK_CHAR; }
static bool IsHopKind(uint8_t k)
{
    return k == WK_WRAPPER || k == WK_MOVELINE || k == WK_UNITGROUP
        || k == WK_LAYOUT || k == WK_MANAGER;
}

struct LiveUnit {
    uintptr_t   ptr;
    const char* name;
    uint8_t     gid;
};
static LiveUnit g_lives[192];
static int g_nLives = 0;

struct DumpPtr {
    uintptr_t   from, ptr;
    uint32_t    off;
    const char* fromName;
    const char* name;
    uint8_t     kind;
};
static DumpPtr g_dptrs[80];
static int g_nDptrs = 0;

struct NameCount { const char* name; int n; };
static NameCount g_hist[80];
static int g_nHist = 0;

struct HuntHit {
    const char* name;
    uintptr_t   slot, instance;
};
static HuntHit g_hunts[24];
static int g_nHunts = 0;

static int   g_nMoveLine = 0;
static int   g_nUnitGroup = 0;
static int   g_nLayout = 0;
static uint32_t g_embedBytes = 0;
static bool  g_dumped = false;
static DWORD g_dumpMs = 0;
static uintptr_t g_pWorldObj = 0;
static const char* g_pWorldName = nullptr;
static uintptr_t g_sUnit = 0, g_sSet = 0;
static uint32_t  g_sUnitInImg = 0, g_sSetInImg = 0;
static bool      g_sUnitFits = false;
static const char* g_sUnitId = nullptr;
static BYTE      g_hdr[256];
static bool      g_hdrOk = false;
static BYTE      g_worldHdr[128];
static bool      g_worldHdrOk = false;

// Zip 32: pool subsystem removed. sUnit+0x44 len=50 stride 0x1FE0 was proven
// to be geometry workspaces (tagged AABB 50505050 / D0D0D0D0), never uEm*.
// Closed as a unit list in the Zip 11 header notes — do not re-add.

static BYTE      g_setHdr[256];
static bool      g_setHdrOk = false;

struct WinDump { uint32_t off; BYTE hex[64]; bool ok; };
static WinDump   g_win[4];
static int       g_nWin = 0;

struct HuntKey { uintptr_t va; const char* name; uint8_t gid, kind; };
static HuntKey   g_hvt[320];
static int       g_nHvt = 0;
static uintptr_t g_vtMin = 0, g_vtMax = 0;

struct HeapMgr { const char* name; uintptr_t ptr; BYTE head[32]; bool headOk; };
static HeapMgr   g_mgrs[32];
static int       g_nMgrs = 0;

struct GidHit {
    uint8_t     gid;
    uintptr_t   ptr, vt;
    const char* name;
    BYTE        head[32];
    bool        headOk;
    bool        want; // gid 0x05 (goblin) — kept even when general filters fire
};
struct FactDump {
    const char* name;
    char        gotName[32]; // name string the stub actually registered
    uintptr_t   va, slot0, caller, rescued;
    uintptr_t   ecx, ecx0, namePtr, pushVt, sizeImm, c7dest, c7imm;
    BYTE        hex[32];
    BYTE        callHex[48];
    BYTE        afterHex[32];
    BYTE        ecxHex[32];
    BYTE        nameHex[32];
    BYTE        pushHex[32];
    bool        ok, callOk, afterOk, ecxOk, nameOk, pushOk;
};
static FactDump  g_fact[16];
static int       g_nFact = 0;
static GidHit    g_gid[48];
static int       g_nGid = 0;

struct Holder {
    const char* where;
    const char* name;
    uintptr_t   slot, live;
    uint32_t    off;
};
static Holder    g_hold[48];
static int       g_nHold = 0;

static DWORD     g_huntMs = 0;
static int       g_huntRegions = 0;
static unsigned  g_huntBytes = 0;


struct DerivedVt {
    const char* name;
    uintptr_t   factory, create, inst;
    bool        shared; // same inst vt claimed by several types = base class, not uEm*
};
static DerivedVt g_der[160];
static int       g_nDer = 0;

struct Census {
    uintptr_t vt;
    int       n;
    uintptr_t sample[2];
};
static Census    g_cen[96];
static int       g_nCen = 0;

// Dump8: factory object (ecx) is an MtDTI in .data.
// [0] factory vt, [4] name*, [+0x18] size | (flags<<16).
struct DtiCand {
    int         slot;
    uintptr_t   func, inst;
    int         callee; // 1 = found in a called function, not the method itself
};
struct DtiHit {
    const char* want;
    char        got[32];
    uintptr_t   dti, vt, namePtr, strVa;
    uint32_t    sizeRaw, size, typeId;
    uintptr_t   create0, foundFact, foundCreate, foundInst;
    uint32_t    foundOff;
    int         foundSlot;
    int         nCands;
    uintptr_t   meth4, meth8;
    BYTE        head[32];
    BYTE        body[128];
    BYTE        vtHex[64];
    bool        headOk, bodyOk, vtOk;
    DtiCand     cands[8];
};
static DtiHit    g_dti[32];
static int       g_nDti = 0;

// Dump11 gold / banned instance vts. Do not hunt these as a species.
static const uintptr_t kSharedCreate = 0x00BFF9A0u;
static const uintptr_t kSharedInst   = 0x015E0378u;
static const uintptr_t kParentVt     = 0x015497F8u; // DTI slot1 goblin+garg
static const uintptr_t kSharedBase   = 0x01574748u; // hunt 09/10
static const uintptr_t kHotEnemyVt   = 0x015F87D8u; // census 3093, DTI sEnemy slot1
static const uintptr_t kGoldGargInst = 0x015B5A80u;
static const uintptr_t kGoldGargFact = 0x015B59A8u;
static const uintptr_t kGoldPlrInst  = 0x015EFD38u;
static const uintptr_t kGoldPlrFact  = 0x015E4F34u;
static const uintptr_t kGoldPawnInst = 0x0155ADA4u;
static const uintptr_t kGoldPawnFact = 0x0155ADB4u;
 // meth4 writes this onto new uEm0100
 // dump19: uNpc DTI vt - 0x344
 // dump19 list, gid 0x61
static bool IsBannedInst(uintptr_t vt); // body after ScanDti — TryGidScout calls it first

struct DtiLink {
    const char* owner;
    uint32_t    off;
    uintptr_t   ptr;
    char        name[28];
    BYTE        head[32];
    bool        headOk;
};
static DtiLink   g_dlink[80];
static int       g_nDlink = 0;

struct Lead {
    const char* fromName;
    const char* name;
    uintptr_t   from, ptr, vt;
    uint32_t    off;
    uint8_t     gid;
    bool        fat; // readable 8KB — possible actor
    BYTE        head[32];
    bool        headOk;
};
static Lead      g_lead[32];
static int       g_nLead = 0;

struct TreeNode {
    char        name[28];
    uintptr_t   dti, parent, child, next;
    uint32_t    size, typeId;
};
static TreeNode  g_tree[96];
static int       g_nTree = 0;

struct NearFact {
    const char* owner;
    int         off;
    uintptr_t   at, val;
};
static NearFact  g_near[48];
static int       g_nNear = 0;

struct TextWrite {
    uintptr_t   vt, site;
    int         n;
};
static TextWrite g_wr[48];
static int       g_nWr = 0;

struct NodeDump {
    uintptr_t   ptr, vt;
    BYTE        body[256];
    bool        ok;
};
static NodeDump  g_node[8];
static int       g_nNode = 0;

struct GoldCtor {
    const char* tag;
    uintptr_t   va;
    BYTE        hex[256];
    bool        ok;
};
static GoldCtor  g_ctor[10];
static int       g_nCtor = 0;


// Build 62: боевая цель пешки (uCmc+0x2EB8), читается в PartyReadPositions.
// Глобал объявлен до PublishWorldFromActors, чтобы та могла его читать.


static int       g_emptyPoll = 0;
 // dump22 list, gid 0x84
 // meth4_uEm8600 dump23

 // all dump18-23 actors live here

static uintptr_t g_seen[160];
static int       g_nSeen = 0;

static void HistAdd(const char* n)
{
    if (!n) n = "(unidentified)";
    for (int i = 0; i < g_nHist; ++i)
        if (g_hist[i].name == n) { g_hist[i].n++; return; }
    if (g_nHist < 80) { g_hist[g_nHist].name = n; g_hist[g_nHist].n = 1; g_nHist++; }
}

static bool Seen(uintptr_t p)
{
    for (int i = 0; i < g_nSeen; ++i) if (g_seen[i] == p) return true;
    if (g_nSeen < 160) g_seen[g_nSeen++] = p;
    return false;
}

static void AddLive(uintptr_t p, const Named& n)
{
    if (!p || !n.name || !IsCharKind(n.kind)) return;
    for (int i = 0; i < g_nLives; ++i) if (g_lives[i].ptr == p) return;
    if (g_nLives >= 192) return;
    BYTE liveGid = n.gid, tmp = 0;
    if (Rd((void*)(p + 0x2D), &tmp, 1) && tmp) {
        if (tmp == 0xFF) return; // dump12 fake uNpc
        // factory/instance name claims a typeId — reject if the object disagrees
        if (n.gid && tmp != n.gid) return;
        liveGid = tmp;
    } else if (n.gid) {
        return;
    }
    g_lives[g_nLives].ptr = p;
    g_lives[g_nLives].name = n.name;
    g_lives[g_nLives].gid = liveGid;
    g_nLives++;
}

static void AddDPtr(uintptr_t from, uint32_t off, uintptr_t ptr, const char* fromName, const Named& n)
{
    if (g_nDptrs >= 80) return;
    DumpPtr& d = g_dptrs[g_nDptrs++];
    d.from = from; d.off = off; d.ptr = ptr;
    d.fromName = fromName ? fromName : "?";
    d.name = n.name ? n.name : "(unidentified)";
    d.kind = n.kind;
}

static uintptr_t ScanObjOf(const char* name)
{
    for (int k = 0; k < g_nScans; ++k)
        if (g_scans[k].t && !strcmp(g_scans[k].t->name, name) && g_scans[k].nDataObj)
            return g_scans[k].dataObj[0];
    // Steam, no ASLR — last confirmed dataObj RVAs. Still prefer scan results.
    if (!strcmp(name, "sUnit"))       return g_base + 0x14B6DBC;
    if (!strcmp(name, "sSetManager")) return g_base + 0x1571D0C;
    return 0;
}

static uint32_t BytesInImage(uintptr_t addr, uint32_t claimed)
{
    if (!InImage(addr)) return 0;
    uintptr_t room = ImageEnd() - addr;
    if (room > 0x1000000) room = 0x1000000;
    if ((uintptr_t)claimed < room) return claimed;
    return (uint32_t)room;
}

static void Hop(uintptr_t obj, const char* fromName, int depth);

static void Consider(uintptr_t from, uint32_t off, uintptr_t ptr, const char* fromName, int depth)
{
    if (!LooksHeap(ptr)) return;
    if (Seen(ptr)) return;
    Named n = NameOf(ptr);
    HistAdd(n.name);
    if (n.name) AddDPtr(from, off, ptr, fromName, n);
    AddLive(ptr, n);
    if (depth < 2 && IsHopKind(n.kind))
        Hop(ptr, n.name ? n.name : fromName, depth + 1);
}

static void Hop(uintptr_t obj, const char* fromName, int depth)
{
    if (depth > 2 || !obj) return;
    BYTE buf[256];
    uint32_t n = 256;
    Named self = NameOf(obj);
    if (self.kind == WK_MOVELINE) n = 24;
    else if (self.kind == WK_UNITGROUP) n = 28;
    else if (self.kind == WK_LAYOUT) n = 160;
    if (!Rd((void*)obj, buf, n)) return;
    uint32_t words = n / 4;
    for (uint32_t i = 1; i < words; ++i) {
        uintptr_t v = *(uint32_t*)(buf + i * 4);
        if (LooksHeap(v))
            Consider(obj, i * 4, v, fromName, depth);
    }
}

static void ScanEmbedded(uintptr_t base, uint32_t bytes, const char* fromName)
{
    if (!base || bytes < 8) return;
    const uintptr_t vtML = g_base + 0x1025EFC; // sUnit::MoveLine
    const uintptr_t vtUG = g_base + 0x1025F04; // sUnit::UnitGroup
    const uintptr_t vtLE = g_base + 0x118FA10; // cLayoutSetEnemy
    const uintptr_t vtLN = g_base + 0x118FB10; // cLayoutSetNpc
    const uintptr_t vtLM = g_base + 0x115E114; // cLotMgr<cLayoutSetEnemy>
    const uintptr_t vtUD = g_base + 0x115E070; // sSetManager::cUnitData
    int followed = 0;
    __try {
        uint32_t* p = (uint32_t*)base;
        uint32_t  n = bytes / 4;
        for (uint32_t i = 0; i < n; ++i) {
            uintptr_t val = p[i];
            uintptr_t here = base + i * 4;
            if (val == vtML) {
                g_nMoveLine++;
                HistAdd("sUnit::MoveLine");
                if (followed < 48) {
                    followed++;
                    uintptr_t a = 0, b = 0;
                    RdPtr((void*)(here + 4), &a);
                    RdPtr((void*)(here + 8), &b);
                    if (LooksHeap(a)) Consider(here, 4, a, "MoveLine", 0);
                    if (LooksHeap(b)) Consider(here, 8, b, "MoveLine", 0);
                }
            } else if (val == vtUG) {
                g_nUnitGroup++;
                HistAdd("sUnit::UnitGroup");
                if (followed < 48) {
                    followed++;
                    BYTE rec[28];
                    if (Rd((void*)here, rec, 28)) {
                        for (int k = 4; k < 28; k += 4) {
                            uintptr_t v = *(uint32_t*)(rec + k);
                            if (LooksHeap(v)) Consider(here, (uint32_t)k, v, "UnitGroup", 0);
                        }
                    }
                }
            } else if (val == vtLE || val == vtLN || val == vtLM || val == vtUD) {
                const char* nm = (val == vtLE) ? "cLayoutSetEnemy"
                    : (val == vtLN) ? "cLayoutSetNpc"
                    : (val == vtLM) ? "cLotMgr<Enemy>"
                    : "sSetManager::cUnitData";
                HistAdd(nm);
                g_nLayout++;
                if (g_nDptrs < 80) {
                    Named named = NameOf(here);
                    if (!named.name) named.name = nm;
                    AddDPtr(base, (uint32_t)(here - base), here, fromName, named);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        logFile << "DevTools: embed scan exception at " << fromName << std::endl;
    }
}

static void ScanRegionPtrs(uintptr_t base, uint32_t bytes, const char* fromName, uint32_t cap)
{
    if (!base || bytes < 8) return;
    if (bytes > cap) bytes = cap;
    __try {
        uint32_t* p = (uint32_t*)base;
        uint32_t  n = bytes / 4;
        for (uint32_t i = 0; i < n; ++i) {
            uintptr_t v = p[i];
            if (!LooksHeap(v)) continue;
            uintptr_t vt = 0;
            if (!RdPtr((void*)v, &vt) || !InImage(vt)) continue;
            uint32_t rva = (uint32_t)(vt - g_base);
            if (rva == kStubVtRVA) continue;
            if (!WatchVt(rva) && !FindByVTable(rva)) continue;
            Consider(base, i * 4, v, fromName, 0);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        logFile << "DevTools: region scan exception at " << fromName << std::endl;
    }
}


static void DumpHeader(uintptr_t addr)
{
    g_hdrOk = Rd((void*)addr, g_hdr, sizeof(g_hdr));
}

static void HuntHeapSingletons()
{
    g_nHunts = 0;
    if (!g_base) return;

    uintptr_t want[16];
    const char* names[16];
    int nw = 0;
    static const char* kHunt[] = {
        "sUnitExt", "sEnemyManager", "sHumanEnemyManager", "sPawnManager",
        "sNpcManager", "sRecognition", "sLockOnManager", "sUnitSearchManager",
        "sPlayerManager", "sBbsRpgMain", "sGameSys", "sMain", nullptr
    };
    for (int i = 0; kHunt[i] && nw < 16; ++i) {
        const TypeAtlas::Info* t = TypeAtlas::FindByName(kHunt[i]);
        if (!t || IsSharedStub(t->factoryVtRVA)) continue;
        want[nw] = g_base + t->factoryVtRVA;
        names[nw] = t->name;
        nw++;
    }

    auto dos = (IMAGE_DOS_HEADER*)g_base;
    auto nt  = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    const int nsec = nt->FileHeader.NumberOfSections;

    __try {
        for (int si = 0; si < nsec && g_nHunts < 24; ++si) {
            if (!(sec[si].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            uintptr_t s0 = g_base + sec[si].VirtualAddress;
            uint32_t  sz = sec[si].Misc.VirtualSize;
            if (!sz) continue;
            if (sec[si].VirtualAddress + sz > g_imageSize)
                sz = g_imageSize - sec[si].VirtualAddress;
            uint32_t* p = (uint32_t*)s0;
            uint32_t  n = sz / 4;
            for (uint32_t i = 0; i < n && g_nHunts < 24; ++i) {
                uintptr_t val = p[i];
                if (!LooksHeap(val)) continue;
                uintptr_t vt = 0;
                if (!RdPtr((void*)val, &vt)) continue;
                for (int k = 0; k < nw; ++k) {
                    if (vt != want[k]) continue;
                    bool dup = false;
                    for (int h = 0; h < g_nHunts; ++h)
                        if (g_hunts[h].instance == val) { dup = true; break; }
                    if (dup) break;
                    g_hunts[g_nHunts].name = names[k];
                    g_hunts[g_nHunts].slot = (uintptr_t)&p[i];
                    g_hunts[g_nHunts].instance = val;
                    g_nHunts++;
                    HistAdd(names[k]);
                    Consider(s0, i * 4, val, names[k], 0);
                    break;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        logFile << "DevTools: heap hunt exception" << std::endl;
    }
}

static uintptr_t FollowJmp(uintptr_t p)
{
    BYTE op = 0;
    if (!Rd((void*)p, &op, 1)) return p;
    if (op == 0xE9) {
        int32_t rel = 0;
        if (Rd((void*)(p + 1), &rel, 4)) return p + 5 + rel;
    } else if (op == 0xEB) {
        int8_t rel = 0;
        if (Rd((void*)(p + 1), &rel, 1)) return p + 2 + rel;
    }
    return p;
}

// factory vt[0] is usually create_*. Some types leave a non-exec dword
// there — try the first four slots, then one dereference.
static uintptr_t CreateFromFactory(uintptr_t factoryVa)
{
    uintptr_t slots[4] = {};
    if (!Rd((void*)factoryVa, slots, sizeof(slots))) return 0;
    for (int i = 0; i < 4; ++i) {
        if (InExec(slots[i])) return FollowJmp(slots[i]);
    }
    for (int i = 0; i < 4; ++i) {
        if (!InImage(slots[i]) || InExec(slots[i])) continue;
        uintptr_t p = 0;
        if (RdPtr((void*)slots[i], &p) && InExec(p)) return FollowJmp(p);
    }
    return 0;
}

static void ConsiderImm(uintptr_t factoryVa, uintptr_t imm,
    uintptr_t* last, uintptr_t* nearVt, uint32_t* nearDist)
{
    if (!LooksLikeVtable(imm)) return;
    *last = imm;
    if (imm == factoryVa) return;
    uint32_t d = (imm > factoryVa)
        ? (uint32_t)(imm - factoryVa) : (uint32_t)(factoryVa - imm);
    // real uEm* inst vt sits next to its factory (~0xD8 on gargoyle)
    if (d < 0x1000u && d < *nearDist) {
        *nearDist = d;
        *nearVt = imm;
    }
}

// factory vtable[0] = create_Type. Scan the ctor for the last
// `mov dword ptr [reg], imm32` whose imm is a real vtable.
// That is the INSTANCE vtable written onto the new object.
static bool IsMovToRegPtr(uint8_t modrm)
{
    // 89 /r  mov [reg], r32   mod=00, r/m not SIB/disp32
    if ((modrm & 0xC0) != 0x00) return false;
    uint8_t rm = modrm & 7;
    return rm != 4 && rm != 5;
}

static uintptr_t DeriveOne(uintptr_t factoryVa, uintptr_t* outCreate, uintptr_t* outNear)
{
    uintptr_t last = 0, nearVt = 0;
    uint32_t nearDist = 0x7FFFFFFFu;
    if (outNear) *outNear = 0;
    __try {
        uintptr_t create = CreateFromFactory(factoryVa);
        if (outCreate) *outCreate = create;
        if (!create || !InExec(create)) return 0;
        BYTE buf[512];
        if (!Rd((void*)create, buf, sizeof(buf))) return 0;
        for (int i = 0; i < (int)sizeof(buf) - 6; ++i) {
            // C7 /0  mov dword ptr [reg], imm32
            if (buf[i] == 0xC7) {
                uint8_t modrm = buf[i + 1];
                int immAt = -1;
                if ((modrm & 0x38) == 0x00 && (modrm & 0xC0) == 0x00) {
                    uint8_t rm = modrm & 7;
                    if (rm != 4 && rm != 5) immAt = i + 2;
                } else if ((modrm & 0x38) == 0x00 && (modrm & 0xC0) == 0x40 && buf[i + 2] == 0x00) {
                    immAt = i + 3;
                }
                if (immAt >= 0 && immAt + 4 <= (int)sizeof(buf)) {
                    uintptr_t imm = *(uint32_t*)(buf + immAt);
                    ConsiderImm(factoryVa, imm, &last, &nearVt, &nearDist);
                }
            }
            // B8+r imm32 / 89 /r   mov eax, vt; mov [esi], eax
            if (buf[i] >= 0xB8 && buf[i] <= 0xBB) {
                uintptr_t imm = *(uint32_t*)(buf + i + 1);
                if (!LooksLikeVtable(imm)) continue;
                uint8_t src = buf[i] - 0xB8; // eax..ebx
                int hi = i + 16;
                if (hi > (int)sizeof(buf) - 2) hi = (int)sizeof(buf) - 2;
                for (int j = i + 5; j < hi; ++j) {
                    if (buf[j] == 0x89 && IsMovToRegPtr(buf[j + 1]) && ((buf[j + 1] >> 3) & 7) == src) {
                        ConsiderImm(factoryVa, imm, &last, &nearVt, &nearDist);
                        break;
                    }
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (outNear) *outNear = nearVt;
    // last write — zip 12 near-only lost sPawnManager (heap [0] was the last write)
    return last;
}

// Scan a .text function for the last `mov [reg], imm32` that looks like a vtable.
// Dump10: DTI vt[0] is shared create; species ctor is a later slot.
static uintptr_t ScanFuncForInst(uintptr_t func)
{
    uintptr_t last = 0, nearVt = 0;
    uint32_t nearDist = 0x7FFFFFFFu;
    if (!func || !InExec(func)) return 0;
    BYTE buf[512];
    if (!Rd((void*)func, buf, sizeof(buf))) return 0;
    for (int i = 0; i < (int)sizeof(buf) - 6; ++i) {
        if (buf[i] == 0xC7) {
            uint8_t modrm = buf[i + 1];
            int immAt = -1;
            if ((modrm & 0x38) == 0x00 && (modrm & 0xC0) == 0x00) {
                uint8_t rm = modrm & 7;
                if (rm != 4 && rm != 5) immAt = i + 2;
            } else if ((modrm & 0x38) == 0x00 && (modrm & 0xC0) == 0x40 && buf[i + 2] == 0x00) {
                immAt = i + 3;
            }
            if (immAt >= 0 && immAt + 4 <= (int)sizeof(buf)) {
                uintptr_t imm = *(uint32_t*)(buf + immAt);
                ConsiderImm(func, imm, &last, &nearVt, &nearDist);
            }
        }
        if (buf[i] >= 0xB8 && buf[i] <= 0xBB) {
            uintptr_t imm = *(uint32_t*)(buf + i + 1);
            if (!LooksLikeVtable(imm)) continue;
            uint8_t src = buf[i] - 0xB8;
            int hi = i + 16;
            if (hi > (int)sizeof(buf) - 2) hi = (int)sizeof(buf) - 2;
            for (int j = i + 5; j < hi; ++j) {
                if (buf[j] == 0x89 && IsMovToRegPtr(buf[j + 1]) && ((buf[j + 1] >> 3) & 7) == src) {
                    ConsiderImm(func, imm, &last, &nearVt, &nearDist);
                    break;
                }
            }
        }
    }
    return last;
}

static void DeriveInstanceVts()
{
    g_nDer = 0;
    if (!g_nExec) InitSections();
    if (!g_nWatch) BuildWatch();
    const int n0 = g_nWatch; // WatchAdd below must not extend this loop
    for (int i = 0; i < n0 && g_nDer < 160; ++i) {
        const Watch& w = g_watch[i];
        if (w.kind != WK_CHAR && w.kind != WK_WRAPPER && w.kind != WK_MANAGER) continue;
        uintptr_t factory = g_base + w.rva;
        uintptr_t create = 0, nearVt = 0;
        uintptr_t inst = DeriveOne(factory, &create, &nearVt);
        DerivedVt& d = g_der[g_nDer++];
        d.name = w.name;
        d.factory = factory;
        d.create = create;
        d.inst = inst;
        d.shared = false;
    }
    // A vtable written by many create_* is a base/component, not a species.
    // Dump 09 named 94 "gargoyles" because 7 uEm* shared one inst vt.
    for (int i = 0; i < g_nDer; ++i) {
        if (!g_der[i].inst || g_der[i].inst == g_der[i].factory) continue;
        int n = 0;
        for (int j = 0; j < g_nDer; ++j)
            if (g_der[j].inst == g_der[i].inst) n++;
        if (n >= 2) g_der[i].shared = true;
    }
    for (int i = 0; i < g_nDer; ++i) {
        if (!g_der[i].inst || g_der[i].shared) continue;
        if (g_der[i].inst == g_der[i].factory) continue;
        const Watch* w = nullptr;
        for (int k = 0; k < n0; ++k)
            if (g_watch[k].name == g_der[i].name) { w = &g_watch[k]; break; }
        if (!w) continue;
        uint32_t rva = (uint32_t)(g_der[i].inst - g_base);
        WatchAdd(rva, w->name, w->gid, w->kind);
    }
}

// Printable C string at p (name argument of register_type_factory).
static bool LooksAsciiPtr(uintptr_t p)
{
    BYTE s[16];
    if (!p || (p & 3) || !Rd((void*)p, s, 16)) return false;
    int n = 0;
    for (int i = 0; i < 16; ++i) {
        if (s[i] == 0) break;
        if (s[i] < 32 || s[i] >= 127) return false;
        n++;
    }
    return n >= 4;
}

// Dump7: uPawnIntel rescued sSetManager's factory (0x0155E04C) from the previous function.
static bool IsForeignFactory(uintptr_t va, const char* self)
{
    if (!va || !g_base || va == g_base + kStubVtRVA) return true;
    uint32_t rva = (uint32_t)(va - g_base);
    if (const TypeAtlas::Info* t = TypeAtlas::FindByFactoryVTable(rva)) {
        if (t->name && self && strcmp(t->name, self) != 0) return true;
    }
    for (int i = 0; i < g_nDer; ++i) {
        if (g_der[i].factory == va && g_der[i].name && self
            && strcmp(g_der[i].name, self) != 0)
            return true;
    }
    return false;
}

// Atvaark rename_type_factories.py: 6 pushes, mov ecx, call, push reset, C7 05 vt.
// Zip 13 scanned every dword for LLV and missed `68 imm32`. Window now includes
// 80 bytes before the Caller and 48 after, so we see ecx / name / the C7 write.
static uintptr_t RescueFactory(const char* name, FactDump* fact)
{
    if (!name || !g_base) return 0;
    const TypeCaller* c = FindCaller(name);
    if (!c || !c->callerVA) return 0;
    uintptr_t call = g_base + (c->callerVA - 0x400000);
    if (fact) fact->caller = call;

    const int PRE = 80;
    const int POST = 48;
    BYTE win[128];
    memset(win, 0, sizeof(win));
    if (call < (uintptr_t)PRE) return 0;
    if (!Rd((void*)(call - PRE), win, PRE + POST)) return 0;

    if (fact) {
        memcpy(fact->callHex, win + (PRE - 48), 48);
        fact->callOk = true;
        memcpy(fact->afterHex, win + PRE, 32);
        fact->afterOk = true;
    }

    int stub = -1;
    for (int i = 0; i <= PRE - 6; ++i) {
        if (win[i] == 0x6A && win[i + 1] == 0
            && win[i + 2] == 0x6A && win[i + 3] == 0
            && win[i + 4] == 0x6A && win[i + 5] == 0)
            stub = i;
    }

    uintptr_t pushes[8];
    memset(pushes, 0, sizeof(pushes));
    int nPush = 0;
    uintptr_t ecx = 0;
    if (stub >= 0) {
        int i = stub;
        while (i < PRE + 16 && nPush < 8) {
            if (win[i] == 0x6A) {
                pushes[nPush++] = win[i + 1];
                i += 2;
                continue;
            }
            if (win[i] == 0x68 && i + 5 <= PRE + POST) {
                pushes[nPush++] = *(uint32_t*)(win + i + 1);
                i += 5;
                continue;
            }
            if (win[i] == 0xB9 && i + 5 <= PRE + POST) {
                ecx = *(uint32_t*)(win + i + 1);
                break;
            }
            break;
        }
    }

    uintptr_t c7dest = 0, c7imm = 0;
    for (int i = PRE; i + 10 <= PRE + POST; ++i) {
        if (win[i] == 0xC7 && win[i + 1] == 0x05) {
            c7dest = *(uint32_t*)(win + i + 2);
            c7imm = *(uint32_t*)(win + i + 6);
            break;
        }
    }
    if (!c7imm) {
        for (int i = PRE - 10; i >= 0 && i < PRE; ++i) {
            if (win[i] == 0xC7 && win[i + 1] == 0x05 && i + 10 <= PRE + POST) {
                c7dest = *(uint32_t*)(win + i + 2);
                c7imm = *(uint32_t*)(win + i + 6);
                break;
            }
        }
    }

    uintptr_t namePtr = 0, pushVt = 0, sizeImm = 0;
    if (nPush >= 6) {
        sizeImm = pushes[3];
        namePtr = pushes[5];
    } else if (nPush >= 3) {
        sizeImm = pushes[nPush - 3];
        namePtr = pushes[nPush - 1];
    }
    for (int p = nPush - 1; p >= 0; --p) {
        uintptr_t imm = pushes[p];
        if (!InImage(imm) || (imm & 3)) continue;
        if (LooksAsciiPtr(imm)) {
            if (!namePtr) namePtr = imm;
            continue;
        }
        pushVt = imm;
        break;
    }

    if (fact) {
        fact->ecx = ecx;
        fact->namePtr = namePtr;
        fact->pushVt = pushVt;
        fact->sizeImm = sizeImm;
        fact->c7dest = c7dest;
        fact->c7imm = c7imm;
        if (ecx) {
            fact->ecxOk = Rd((void*)ecx, fact->ecxHex, 32);
            if (fact->ecxOk) fact->ecx0 = *(uint32_t*)fact->ecxHex;
        }
        if (namePtr)
            fact->nameOk = Rd((void*)namePtr, fact->nameHex, 32);
        if (pushVt)
            fact->pushOk = Rd((void*)pushVt, fact->pushHex, 32);
    }

    uintptr_t best = 0;
    uintptr_t ecx0 = 0;
    if (ecx) RdPtr((void*)ecx, &ecx0);

    if (ecx0 && LooksLikeVtable(ecx0) && !IsForeignFactory(ecx0, name))
        best = ecx0;
    if (!best && ecx && LooksLikeVtable(ecx) && !IsForeignFactory(ecx, name))
        best = ecx;
    if (!best && pushVt && LooksLikeVtable(pushVt) && !IsForeignFactory(pushVt, name))
        best = pushVt;
    if (!best && c7imm && LooksLikeVtable(c7imm) && !IsForeignFactory(c7imm, name))
        best = c7imm;
    if (!best && ecx0 && !LooksAsciiPtr(ecx0) && !IsForeignFactory(ecx0, name)
        && CreateFromFactory(ecx0))
        best = ecx0;
    if (!best && pushVt && !LooksAsciiPtr(pushVt) && !IsForeignFactory(pushVt, name)
        && CreateFromFactory(pushVt))
        best = pushVt;

    if (fact) {
        fact->gotName[0] = 0;
        if (fact->nameOk) {
            int n = 0;
            while (n < 31 && fact->nameHex[n] >= 32 && fact->nameHex[n] < 127) {
                fact->gotName[n] = (char)fact->nameHex[n];
                n++;
            }
            fact->gotName[n] = 0;
        }
        // Dump8: TSV Caller for uEm0100 registered uEm0100_20. Do not hunt a neighbor.
        if (fact->gotName[0] && name && strcmp(fact->gotName, name) != 0)
            best = 0;
        fact->rescued = best;
    }
    return best;
}


static void CensusAdd(uintptr_t vt, uintptr_t obj)
{
    if (!vt || !obj) return;
    for (int i = 0; i < g_nCen; ++i) {
        if (g_cen[i].vt != vt) continue;
        g_cen[i].n++;
        if (!g_cen[i].sample[1] && g_cen[i].sample[0] != obj)
            g_cen[i].sample[1] = obj;
        return;
    }
    if (g_nCen >= 96) return;
    Census& c = g_cen[g_nCen++];
    c.vt = vt; c.n = 1; c.sample[0] = obj; c.sample[1] = 0;
}

static const char* NameVt(uintptr_t vt)
{
    if (!vt || !g_base || vt < g_base) return nullptr;
    uint32_t rva = (uint32_t)(vt - g_base);
    if (const Watch* w = WatchVt(rva)) return w->name;
    if (const EnemyTypeInfo* e = FindByVTable(rva)) return e->uEmName;
    if (const TypeAtlas::Info* a = TypeAtlas::FindByFactoryVTable(rva)) return a->name;
    for (int i = 0; i < g_nDer; ++i)
        if (g_der[i].shared && g_der[i].inst == vt) return "shared-base";
    return nullptr;
}

static int CountMgrName(const char* name)
{
    int n = 0;
    for (int i = 0; i < g_nMgrs; ++i)
        if (g_mgrs[i].name == name) n++;
    return n;
}

static void HuntAddKey(uint32_t rva, const char* name, uint8_t gid, uint8_t kind)
{
    if (!rva || !name || rva == kStubVtRVA || g_nHvt >= 320 || !g_base) return;
    uintptr_t va = g_base + rva;
    for (int i = 0; i < g_nHvt; ++i) if (g_hvt[i].va == va) return;
    HuntKey& k = g_hvt[g_nHvt++];
    k.va = va; k.name = name; k.gid = gid; k.kind = kind;
    if (!g_vtMin || va < g_vtMin) g_vtMin = va;
    if (va > g_vtMax) g_vtMax = va;
}

static void BuildHuntTable()
{
    g_nHvt = 0;
    g_vtMin = 0;
    g_vtMax = 0;
    if (!g_nWatch) BuildWatch();
    for (int i = 0; i < g_nWatch; ++i) {
        const Watch& w = g_watch[i];
        if (w.kind == WK_CHAR || w.kind == WK_MANAGER || w.kind == WK_WRAPPER || w.kind == WK_LAYOUT)
            HuntAddKey(w.rva, w.name, w.gid, w.kind);
    }
    // insertion sort by va — binary search needs it
    for (int i = 1; i < g_nHvt; ++i) {
        HuntKey t = g_hvt[i];
        int j = i;
        while (j > 0 && g_hvt[j - 1].va > t.va) { g_hvt[j] = g_hvt[j - 1]; --j; }
        g_hvt[j] = t;
    }
}

static const HuntKey* HuntLookup(uintptr_t va)
{
    if (!g_nHvt || va < g_vtMin || va > g_vtMax) return nullptr;
    int lo = 0, hi = g_nHvt;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (g_hvt[mid].va < va) lo = mid + 1;
        else hi = mid;
    }
    if (lo < g_nHvt && g_hvt[lo].va == va) return &g_hvt[lo];
    return nullptr;
}

static void AddHeapMgr(const char* name, uintptr_t ptr)
{
    if (!name || !ptr || g_nMgrs >= 32) return;
    for (int i = 0; i < g_nMgrs; ++i) if (g_mgrs[i].ptr == ptr) return;
    g_mgrs[g_nMgrs].name = name;
    g_mgrs[g_nMgrs].ptr = ptr;
    g_mgrs[g_nMgrs].headOk = Rd((void*)ptr, g_mgrs[g_nMgrs].head, 32);
    g_nMgrs++;
    HistAdd(name);
}

static int CountGidOf(uint8_t gid)
{
    int n = 0;
    for (int i = 0; i < g_nGid; ++i) if (g_gid[i].gid == gid) n++;
    return n;
}
static int CountGidVtOf(uintptr_t vt)
{
    int n = 0;
    for (int i = 0; i < g_nGid; ++i) if (g_gid[i].vt == vt) n++;
    return n;
}
static int CensusN(uintptr_t vt)
{
    for (int i = 0; i < g_nCen; ++i) if (g_cen[i].vt == vt) return g_cen[i].n;
    return 0;
}
static bool SameVtNearby(uintptr_t obj, uintptr_t vt)
{
    uintptr_t a = 0;
    if (RdPtr((void*)(obj + 0x10), &a) && a == vt) return true;
    if (RdPtr((void*)(obj + 0x40), &a) && a == vt) return true;
    if (RdPtr((void*)(obj + 0x50), &a) && a == vt) return true;
    return false;
}

// Zip 11 filled 48 slots with packed 16-byte records and hot component vts.
// Goblin (gid 0x05) never got a seat. Filters + reserved want-slots.
static void TryGidScout(uintptr_t obj, uintptr_t vt, bool llv)
{
    BYTE gidb = 0;
    if (!Rd((void*)(obj + 0x2D), &gidb, 1) || !gidb || gidb == 0xFF) return;
    const EnemyTypeInfo* ei = FindByGroupId(gidb);
    if (!ei) return;
    const bool want = (gidb == 0x05); // uEm0100
    // zip 12 reserved 0x05 without LLV — filled with UTF-16 "WINMM.dll"
    if (!llv) return;
    if (vt & 3) return;

    for (int di = 0; di < g_nDer; ++di)
        if (g_der[di].shared && g_der[di].inst == vt) return;
    if (IsRepeatTag((uint32_t)vt)) return;
    if (SameVtNearby(obj, vt)) return;
    if (CensusN(vt) > 32) return;
    if (obj < 0x10000000u) return;
    if (IsBannedInst(vt)) return;
    // sUnit neighborhood: unregistered geom vts (dump11 0x014263C0 / 0x01426440)
    if (vt >= 0x01425000u && vt < 0x01428000u) return;
    if (!want && CountGidOf(gidb) >= 2) return;
    if (!want && CountGidVtOf(vt) >= 2) return;
    if (want && CountGidOf(0x05) >= 8) return;

    uint32_t need = want ? 8192u : 2048u;
    BYTE probe = 0;
    if (!Rd((void*)(obj + need - 1), &probe, 1)) return;

    for (int gi = 0; gi < g_nGid; ++gi)
        if (g_gid[gi].ptr == obj) return;

    int slot = -1;
    if (g_nGid < 48) {
        slot = g_nGid++;
    } else if (want) {
        for (int i = 0; i < g_nGid; ++i)
            if (!g_gid[i].want) { slot = i; break; }
    }
    if (slot < 0) return;

    GidHit& g = g_gid[slot];
    memset(&g, 0, sizeof(g));
    g.gid = gidb;
    g.ptr = obj;
    g.vt = vt;
    g.name = ei->uEmName;
    g.want = want;
    g.headOk = Rd((void*)obj, g.head, 32);
    if (g.headOk) {
        int z = 0;
        for (int b = 1; b < 16; b += 2) if (g.head[b] == 0) z++;
        bool hex8 = true;
        for (int b = 8; b < 16; ++b) {
            BYTE c = g.head[b];
            bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!ok) { hex8 = false; break; }
        }
        if (z >= 6 || hex8) { // UTF-16le or "1fa439c0" guid string (dump7)
            if (slot == g_nGid - 1 && g_nGid > 0) g_nGid--;
            else memset(&g, 0, sizeof(g));
            return;
        }
    }
}

static const char* kDtiNames[] = {
    "uEm0100", "uEm0100_20", "uEm0100_0", "uEm0101", "uEm0200", "uEm0500", "uEm0900",
    "uPlayer", "uNpc", "uHumanEnemy", "uCharacterBase", "uPawnIntel",
    "sEnemyManager", "sPawnManager", "sPlayerManager", "sNpcManager",
    "sRecognition", "sUnit", "sSetManager",
    "uEnemy", "uEmWolfBase", "uEmZombiBase",
    "sHumanEnemyManager", "sCharacterBaseManager",
    "uEm8000", "uEm8600", "uEm8500", nullptr
};

static const DtiHit* FindDti(const char* name)
{
    if (!name) return nullptr;
    for (int i = 0; i < g_nDti; ++i)
        if (g_dti[i].want && !strcmp(g_dti[i].want, name)) return &g_dti[i];
    return nullptr;
}

// Image strings + .data objects whose [+4] is that string. Independent of TSV Caller.
static void ScanDti()
{
    g_nDti = 0;
    if (!g_base || !g_imageSize) return;

    struct SN { const char* n; uintptr_t va; int len; };
    SN sn[32];
    int nsn = 0;
    for (int i = 0; kDtiNames[i] && nsn < 32; ++i) {
        sn[nsn].n = kDtiNames[i];
        sn[nsn].va = 0;
        sn[nsn].len = (int)strlen(kDtiNames[i]);
        nsn++;
    }

    __try {
        const BYTE* img = (const BYTE*)g_base;
        uint32_t lim = g_imageSize;
        for (uint32_t o = 0; o + 32 < lim; ++o) {
            BYTE c = img[o];
            if (c != 'u' && c != 's' && c != 'c') continue;
            for (int i = 0; i < nsn; ++i) {
                if (sn[i].va) continue;
                if (img[o] != (BYTE)sn[i].n[0]) continue;
                if (memcmp(img + o, sn[i].n, (size_t)sn[i].len) == 0
                    && img[o + sn[i].len] == 0)
                    sn[i].va = g_base + o;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    auto dos = (IMAGE_DOS_HEADER*)g_base;
    auto nt  = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    const int nsec = nt->FileHeader.NumberOfSections;
    __try {
        for (int si = 0; si < nsec; ++si) {
            const bool exec  = (sec[si].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            const bool write = (sec[si].Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            if (exec || !write) continue;
            uintptr_t s0 = g_base + sec[si].VirtualAddress;
            uint32_t  sz = sec[si].Misc.VirtualSize;
            if (!sz) continue;
            if (sec[si].VirtualAddress + sz > g_imageSize)
                sz = g_imageSize - sec[si].VirtualAddress;
            if (sz < 32) continue;
            uint32_t* p = (uint32_t*)s0;
            uint32_t  n = sz / 4;
            for (uint32_t i = 0; i + 7 < n && g_nDti < 32; ++i) {
                uintptr_t np = p[i + 1];
                int hit = -1;
                for (int k = 0; k < nsn; ++k)
                    if (sn[k].va && np == sn[k].va) { hit = k; break; }
                if (hit < 0) continue;
                uintptr_t vt = p[i];
                uintptr_t dti = s0 + (uintptr_t)i * 4;
                // prefer the row whose size matches the atlas, else first
                int exist = -1;
                for (int d = 0; d < g_nDti; ++d)
                    if (g_dti[d].want == sn[hit].n) { exist = d; break; }
                uint32_t sizeRaw = p[i + 6];
                uint32_t size = sizeRaw & 0xFFFFu;
                const TypeAtlas::Info* ta = TypeAtlas::FindByName(sn[hit].n);
                bool sizeOk = ta && ta->size && size == ta->size;
                if (exist >= 0) {
                    bool oldOk = ta && ta->size && g_dti[exist].size == ta->size;
                    if (oldOk || !sizeOk) continue;
                }
                DtiHit& d = g_dti[exist >= 0 ? exist : g_nDti];
                if (exist < 0) g_nDti++;
                memset(&d, 0, sizeof(d));
                d.want = sn[hit].n;
                memcpy(d.got, sn[hit].n, 31); d.got[31] = 0;
                d.dti = dti;
                d.vt = vt;
                d.namePtr = np;
                d.strVa = sn[hit].va;
                d.sizeRaw = sizeRaw;
                d.size = size;
                d.headOk = Rd((void*)dti, d.head, 32);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    logFile << "DevTools: DTI scan hits=" << g_nDti << std::endl;
    for (int i = 0; i < g_nDti; ++i)
        logFile << "  DTI " << g_dti[i].want << " dti=0x" << std::hex << g_dti[i].dti
                << " vt=0x" << g_dti[i].vt << " size=" << std::dec << g_dti[i].size << std::endl;
}

// Dump9: DTI[0] is the card's own vtable (shared create 0x00BFF9A0).
// Real factory vt (TSV col 9 when it is a vt) sits deeper in the card.
static bool IsBannedInst(uintptr_t vt)
{
    return !vt || vt == kSharedInst || vt == kParentVt
        || vt == kSharedBase || vt == kHotEnemyVt;
}

static void AddCand(DtiHit& d, int slot, uintptr_t func, uintptr_t inst, int callee)
{
    if (!inst || inst == d.vt || IsBannedInst(inst)) return;
    if (!LooksLikeVtable(inst)) return;
    for (int i = 0; i < d.nCands; ++i)
        if (d.cands[i].inst == inst) return;
    if (d.nCands >= 8) return;
    d.cands[d.nCands].slot = slot;
    d.cands[d.nCands].func = func;
    d.cands[d.nCands].inst = inst;
    d.cands[d.nCands].callee = callee;
    d.nCands++;
}

static void HarvestFunc(DtiHit& d, uintptr_t func, int slot, int callee)
{
    if (!func || !InExec(func)) return;
    AddCand(d, slot, func, ScanFuncForInst(func), callee);
    if (callee) return; // one level only
    BYTE buf[384];
    if (!Rd((void*)func, buf, sizeof(buf))) return;
    int ncall = 0;
    for (int i = 0; i < (int)sizeof(buf) - 5; ++i) {
        if (buf[i] != 0xE8) continue;
        int32_t rel = 0;
        memcpy(&rel, buf + i + 1, 4);
        uintptr_t tgt = func + (uintptr_t)i + 5 + rel;
        if (!InExec(tgt) || tgt == func) continue;
        HarvestFunc(d, tgt, slot, 1);
        if (++ncall >= 6) break;
    }
}

static int CountCandInst(uintptr_t inst)
{
    int n = 0;
    if (!inst) return 0;
    for (int i = 0; i < g_nDti; ++i)
        for (int c = 0; c < g_dti[i].nCands; ++c)
            if (g_dti[i].cands[c].inst == inst) n++;
    return n;
}

static uintptr_t GoldForName(const char* n)
{
    if (!n) return 0;
    if (!strcmp(n, "uEm0900")) return kGoldGargInst;
    if (!strcmp(n, "uPlayer")) return kGoldPlrInst;
    if (!strcmp(n, "sPawnManager")) return kGoldPawnInst;
    return 0;
}

static bool NearAnyDtiVt(uintptr_t inst)
{
    // dump12: unique pick 0x01585654 sat +0x50 from goblin DTI vt 0x01585604
    if (!inst || inst == kGoblinInst) return false;
    for (int i = 0; i < g_nDti; ++i) {
        if (!g_dti[i].vt) continue;
        uintptr_t a = inst, b = g_dti[i].vt;
        uintptr_t dlt = (a > b) ? (a - b) : (b - a);
        if (dlt < 0x800u) return true;
    }
    return false;
}

static void PickUniqueDti()
{
    for (int i = 0; i < g_nDti; ++i) {
        DtiHit& d = g_dti[i];
        d.foundInst = 0;
        d.foundCreate = 0;
        d.foundSlot = 0;
        uintptr_t gold = GoldForName(d.want);
        int pick = -1;
        for (int c = 0; c < d.nCands; ++c) {
            if (gold && d.cands[c].inst == gold) { pick = c; break; }
            if (d.cands[c].callee) continue;
            if (IsBannedInst(d.cands[c].inst)) continue;
            if (NearAnyDtiVt(d.cands[c].inst)) continue;
            if (CountCandInst(d.cands[c].inst) != 1) continue;
            pick = c;
        }
        if (pick >= 0) {
            d.foundInst = d.cands[pick].inst;
            d.foundCreate = d.cands[pick].func;
            d.foundSlot = d.cands[pick].slot;
        }
    }
}

// Dump11: first DTI method writes a parent vt. Harvest every .text slot
// and one level of E8 callees. Pick only an inst no other type claimed.
static void EnrichDti()
{
    if (!g_nExec) InitSections();
    for (int i = 0; i < g_nDti; ++i) {
        DtiHit& d = g_dti[i];
        d.bodyOk = Rd((void*)d.dti, d.body, 128);
        if (d.bodyOk) {
            memcpy(d.head, d.body, 32);
            d.headOk = true;
            d.typeId = *(uint32_t*)(d.body + 0x1C);
        }
        d.foundFact = 0;
        d.foundOff = 0;
        d.foundCreate = 0;
        d.foundInst = 0;
        d.foundSlot = 0;
        d.nCands = 0;
        d.meth4 = 0;
        d.meth8 = 0;
        memset(d.cands, 0, sizeof(d.cands));
        if (!d.vt) continue;
        uintptr_t slots[16];
        memset(slots, 0, sizeof(slots));
        d.vtOk = Rd((void*)d.vt, slots, sizeof(slots));
        if (d.vtOk) memcpy(d.vtHex, slots, sizeof(d.vtHex));
        d.create0 = slots[0];
        d.meth4 = slots[1];
        d.meth8 = slots[2];
        for (int sidx = 1; sidx < 16; ++sidx) {
            uintptr_t fn = slots[sidx];
            if (!fn || !InExec(fn)) break; // ran off the vtable into strings
            if (fn == kSharedCreate) continue;
            HarvestFunc(d, fn, sidx, 0);
        }
    }
    PickUniqueDti();
}

static void ApplyDtiToFacts()
{
    for (int i = 0; i < g_nFact; ++i) {
        const DtiHit* d = FindDti(g_fact[i].name);
        if (!d || !d->vt) continue;
        g_fact[i].ecx = d->dti;
        g_fact[i].ecx0 = d->vt;
        g_fact[i].namePtr = d->namePtr;
        if (d->headOk) {
            memcpy(g_fact[i].ecxHex, d->head, 32);
            g_fact[i].ecxOk = true;
        }
        if (d->got[0])
            memcpy(g_fact[i].gotName, d->got, 32);
        // Only a UNIQUE species inst. Shared parent (dump11 0x015497F8) stays 0.
        g_fact[i].rescued = d->foundInst;
    }
}

static void ScanDtiLinks()
{
    g_nDlink = 0;
    static const uint32_t kOff[4] = { 0x08, 0x0C, 0x10, 0x14 };
    for (int i = 0; i < g_nDti && g_nDlink < 80; ++i) {
        if (!g_dti[i].bodyOk) continue;
        for (int k = 0; k < 4 && g_nDlink < 80; ++k) {
            uintptr_t ptr = *(uint32_t*)(g_dti[i].body + kOff[k]);
            if (!ptr || !InImage(ptr) || (ptr & 3)) continue;
            DtiLink& L = g_dlink[g_nDlink];
            memset(&L, 0, sizeof(L));
            L.owner = g_dti[i].want;
            L.off = kOff[k];
            L.ptr = ptr;
            L.headOk = Rd((void*)ptr, L.head, 32);
            if (L.headOk) {
                uintptr_t np = *(uint32_t*)(L.head + 4);
                BYTE nm[28];
                if (LooksAsciiPtr(np) && Rd((void*)np, nm, 27)) {
                    nm[27] = 0;
                    memcpy(L.name, nm, 28);
                }
            }
            g_nDlink++;
        }
    }
}

static void WalkDtiTree()
{
    g_nTree = 0;
    uintptr_t seeds[8];
    int ns = 0;
    for (int i = 0; i < g_nDti && ns < 8; ++i) {
        if (!g_dti[i].want || !g_dti[i].dti) continue;
        if (!strcmp(g_dti[i].want, "uEnemy") || !strcmp(g_dti[i].want, "uEm0100")
            || !strcmp(g_dti[i].want, "uEmWolfBase") || !strcmp(g_dti[i].want, "uCharacterBase")
            || !strcmp(g_dti[i].want, "uEm8000") || !strcmp(g_dti[i].want, "uEm8600"))
            seeds[ns++] = g_dti[i].dti;
    }
    uintptr_t q[96];
    int qn = 0;
    for (int i = 0; i < ns; ++i) q[qn++] = seeds[i];
    for (int qi = 0; qi < qn && g_nTree < 96; ++qi) {
        uintptr_t dti = q[qi];
        int exist = 0;
        for (int t = 0; t < g_nTree; ++t) if (g_tree[t].dti == dti) { exist = 1; break; }
        if (exist) continue;
        BYTE head[32];
        if (!Rd((void*)dti, head, 32)) continue;
        TreeNode& T = g_tree[g_nTree];
        memset(&T, 0, sizeof(T));
        T.dti = dti;
        T.next = *(uint32_t*)(head + 0x08);
        T.child = *(uint32_t*)(head + 0x0C);
        T.parent = *(uint32_t*)(head + 0x10);
        T.size = (*(uint32_t*)(head + 0x18)) & 0xFFFFu;
        T.typeId = *(uint32_t*)(head + 0x1C);
        uintptr_t np = *(uint32_t*)(head + 4);
        BYTE nm[28];
        if (LooksAsciiPtr(np) && Rd((void*)np, nm, 27)) {
            nm[27] = 0;
            memcpy(T.name, nm, 28);
        }
        g_nTree++;
        if (T.child && InImage(T.child) && qn < 96) q[qn++] = T.child;
        if (T.next && InImage(T.next) && qn < 96) q[qn++] = T.next;
    }
}

static void AddLead(const char* fromName, uintptr_t from, uint32_t off, uintptr_t ptr)
{
    if (!ptr || g_nLead >= 32) return;
    for (int i = 0; i < g_nLead; ++i)
        if (g_lead[i].ptr == ptr) return;
    Lead& L = g_lead[g_nLead];
    memset(&L, 0, sizeof(L));
    L.fromName = fromName ? fromName : "?";
    L.from = from;
    L.off = off;
    L.ptr = ptr;
    L.headOk = Rd((void*)ptr, L.head, 32);
    if (L.headOk) L.vt = *(uint32_t*)L.head;
    Named nm = NameOf(ptr);
    L.name = nm.name;
    BYTE gidb = 0;
    if (Rd((void*)(ptr + 0x2D), &gidb, 1)) L.gid = gidb;
    BYTE probe = 0;
    L.fat = Rd((void*)(ptr + 8191), &probe, 1) != false;
    g_nLead++;
}

static void TryBackActor(uintptr_t node)
{
    static const uint32_t kBack[6] = { 0x10, 0x20, 0x40, 0x50, 0x80, 0x100 };
    for (int i = 0; i < 6; ++i) {
        uintptr_t a = node - kBack[i];
        if (!LooksHeap(a)) continue;
        uintptr_t vt = 0;
        if (!RdPtr((void*)a, &vt) || !LooksLikeVtable(vt)) continue;
        if (IsBannedInst(vt) || NearAnyDtiVt(vt)) continue;
        BYTE gidb = 0;
        Rd((void*)(a + 0x2D), &gidb, 1);
        AddLead("back", node, kBack[i], a);
        (void)gidb;
    }
}

static void CollectLeads()
{
    g_nLead = 0;
    for (int i = 0; i < g_nGid; ++i) {
        if (!g_gid[i].want) continue;
        BYTE buf[256];
        if (!Rd((void*)g_gid[i].ptr, buf, sizeof(buf))) continue;
        for (uint32_t o = 4; o < sizeof(buf); o += 4) {
            uintptr_t v = *(uint32_t*)(buf + o);
            if (LooksHeap(v) && v >= 0x10000000u && v < 0x40000000u)
                AddLead("gid05", g_gid[i].ptr, o, v);
        }
        TryBackActor(g_gid[i].ptr);
    }
    for (int i = 0; i < g_nMgrs; ++i) {
        if (!g_mgrs[i].name || strcmp(g_mgrs[i].name, "cLinkUnitEnemy")) continue;
        BYTE buf[96];
        if (!Rd((void*)g_mgrs[i].ptr, buf, sizeof(buf))) continue;
        for (uint32_t o = 4; o < sizeof(buf); o += 4) {
            uintptr_t v = *(uint32_t*)(buf + o);
            if (LooksHeap(v) && v >= 0x10000000u && v < 0x40000000u)
                AddLead("cLinkUnitEnemy", g_mgrs[i].ptr, o, v);
        }
    }
}


static void ScanNearFactory()
{
    g_nNear = 0;
    if (!g_base) return;
    static const char* kN[] = {
        "uEm0100", "uEm0101", "uEm0200", "uEm0500", "uEm0900",
        "uPlayer", "uEnemy", "sPawnManager", 0
    };
    for (int t = 0; kN[t] && g_nNear < 48; ++t) {
        const TypeAtlas::Info* info = TypeAtlas::FindByName(kN[t]);
        if (!info || !info->factoryVtRVA) continue;
        uintptr_t fact = g_base + info->factoryVtRVA;
        int got = 0;
        // dump16: packed vtables make at-4 also LLV, hiding gold fact/inst.
        // Always keep the factory itself and the known neighbor slots.
        static const int kForce[4] = { 0, 0xD8, -0x10, 0x80 };
        for (int fi = 0; fi < 4 && g_nNear < 48; ++fi) {
            int off = kForce[fi];
            uintptr_t at = (off >= 0) ? (fact + (unsigned)off) : (fact - (unsigned)(-off));
            if (!InImage(at) || (at & 3)) continue;
            if (!LooksLikeVtable(at) || IsBannedInst(at)) continue;
            NearFact& N = g_near[g_nNear];
            N.owner = kN[t];
            N.off = off;
            N.at = at;
            N.val = at;
            g_nNear++;
            got++;
        }
        for (int off = -0x200; off <= 0x200 && g_nNear < 48 && got < 8; off += 4) {
            uintptr_t at = (off >= 0) ? (fact + (unsigned)off) : (fact - (unsigned)(-off));
            if (!InImage(at) || (at & 3)) continue;
            if (!LooksLikeVtable(at) || IsBannedInst(at)) continue;
            if (at >= 4 && LooksLikeVtable(at - 4)) continue;
            int dup = 0;
            for (int k = 0; k < g_nNear; ++k)
                if (g_near[k].at == at) { dup = 1; break; }
            if (dup) continue;
            NearFact& N = g_near[g_nNear];
            N.owner = kN[t];
            N.off = off;
            N.at = at;
            N.val = at;
            g_nNear++;
            got++;
        }
    }
}

static bool InInstBand(uintptr_t vt)
{
    if (vt == kGoldGargInst || vt == kGoldPlrInst || vt == kGoldPawnInst) return true;
    if (vt == kGoldGargFact || vt == kGoldPlrFact || vt == kGoldPawnFact) return true;
    // dump15: 0x01580xxx ate the cap. Goblin/gargoyle/player live at 0x015A+
    return vt >= 0x015A0000u && vt < 0x01600000u;
}

static void ScanTextWrites()
{
    g_nWr = 0;
    if (!g_nExec) InitSections();
    struct Acc { uintptr_t vt, site; int n; };
    Acc acc[128];
    int na = 0;
    __try {
        for (int e = 0; e < g_nExec; ++e) {
            uintptr_t lo = g_exec[e].lo, hi = g_exec[e].hi;
            if (hi - lo < 8) continue;
            const BYTE* buf = (const BYTE*)lo;
            uint32_t sz = (uint32_t)(hi - lo);
            for (uint32_t i = 0; i + 6 < sz; ++i) {
                if (buf[i] != 0xC7) continue;
                uint8_t modrm = buf[i + 1];
                int immAt = -1;
                if ((modrm & 0x38) == 0 && (modrm & 0xC0) == 0) {
                    uint8_t rm = modrm & 7;
                    if (rm != 4 && rm != 5) immAt = (int)i + 2;
                } else if ((modrm & 0x38) == 0 && (modrm & 0xC0) == 0x40 && buf[i + 2] == 0) {
                    immAt = (int)i + 3;
                }
                if (immAt < 0 || immAt + 4 > (int)sz) continue;
                uintptr_t imm = *(uint32_t*)(buf + immAt);
                if (!InInstBand(imm)) continue;
                if (!LooksLikeVtable(imm) || IsBannedInst(imm)) continue;
                int f = -1;
                for (int k = 0; k < na; ++k) if (acc[k].vt == imm) { f = k; break; }
                if (f < 0) {
                    if (na >= 128) continue;
                    f = na++;
                    acc[f].vt = imm;
                    acc[f].site = lo + i;
                    acc[f].n = 1;
                } else acc[f].n++;
            }
            // B8+r imm32 / 89 /r   same as DeriveOne — gargoyle ctor may not use C7
            for (uint32_t i = 0; i + 7 < sz; ++i) {
                if (buf[i] < 0xB8 || buf[i] > 0xBB) continue;
                uintptr_t imm = *(uint32_t*)(buf + i + 1);
                if (!InInstBand(imm) || !LooksLikeVtable(imm) || IsBannedInst(imm)) continue;
                uint8_t src = buf[i] - 0xB8;
                uint32_t hi = i + 16;
                if (hi > sz - 2) hi = sz - 2;
                int hit = 0;
                for (uint32_t j = i + 5; j < hi; ++j) {
                    if (buf[j] == 0x89 && IsMovToRegPtr(buf[j + 1]) && ((buf[j + 1] >> 3) & 7) == src)
                    { hit = 1; break; }
                }
                if (!hit) continue;
                int f = -1;
                for (int k = 0; k < na; ++k) if (acc[k].vt == imm) { f = k; break; }
                if (f < 0) {
                    if (na >= 128) continue;
                    f = na++;
                    acc[f].vt = imm;
                    acc[f].site = lo + i;
                    acc[f].n = 1;
                } else acc[f].n++;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    // gold first, then rare. dump16 lost 0x015B5A80 behind 0x48-stride Act* writes.
    for (int pass = 0; pass < 2 && g_nWr < 48; ++pass) {
        for (int k = 0; k < na && g_nWr < 48; ++k) {
            int gold = (acc[k].vt == kGoldGargInst || acc[k].vt == kGoldPlrInst
                     || acc[k].vt == kGoldPawnInst);
            if (pass == 0 && !gold) continue;
            if (pass == 1 && gold) continue;
            if (pass == 1 && acc[k].n > 2) continue;
            g_wr[g_nWr].vt = acc[k].vt;
            g_wr[g_nWr].site = acc[k].site;
            g_wr[g_nWr].n = acc[k].n;
            g_nWr++;
        }
    }
}

static void DumpGidNodes()
{
    g_nNode = 0;
    for (int i = 0; i < g_nGid && g_nNode < 8; ++i) {
        if (!g_gid[i].want) continue;
        if (g_gid[i].vt != 0x015852A8u && g_gid[i].vt != 0x015BB278u) continue;
        NodeDump& N = g_node[g_nNode];
        memset(&N, 0, sizeof(N));
        N.ptr = g_gid[i].ptr;
        N.vt = g_gid[i].vt;
        N.ok = Rd((void*)N.ptr, N.body, 256);
        g_nNode++;
        if (N.ok) {
            uintptr_t kid = *(uint32_t*)(N.body + 0xF8);
            if (LooksHeap(kid) && kid >= 0x10000000u && kid < 0x40000000u)
                AddLead("nodeF8", N.ptr, 0xF8, kid);
        }
    }
}

static void DumpGoldCtors()
{
    g_nCtor = 0;
    static const struct { const char* tag; uintptr_t va; } kC[6] = {
        { "create_uEm0900", 0x00A3B130u },
        { "create_uPlayer", 0x00C76030u },
        { "create_sPawn",   0x005B4AA0u },
        { "meth4_uEm0100",  0x008CAFA0u },
        { "create_uEm8000", 0x00C48AC0u },
        { "create_uEm8600", 0x01052BF0u }
    };
    for (int i = 0; i < 6; ++i) {
        GoldCtor& C = g_ctor[g_nCtor];
        memset(&C, 0, sizeof(C));
        C.tag = kC[i].tag;
        C.va = kC[i].va;
        C.ok = InExec(C.va) && Rd((void*)C.va, C.hex, 256);
        g_nCtor++;
    }
    // dump21: TSV create_uEm8000/8600 are not actor ctors. Real create is DTI.vt[4].
    static const char* kW[] = { "uEm8000", "uEm8600", "uEm8500", nullptr };
    static char wtag[3][20];
    for (int i = 0; kW[i] && g_nCtor < 10; ++i) {
        const DtiHit* d = FindDti(kW[i]);
        if (!d || !d->meth4) continue;
        sprintf_s(wtag[i], "meth4_%s", kW[i]);
        GoldCtor& C = g_ctor[g_nCtor];
        memset(&C, 0, sizeof(C));
        C.tag = wtag[i];
        C.va = d->meth4;
        C.ok = InExec(C.va) && Rd((void*)C.va, C.hex, 256);
        g_nCtor++;
    }
}


// Zip 32 — ActScan.
// Find the slot in the 29KB body that holds the current cEm*Act object.
//
// Cost matters: DumpActorsFrom also runs from WorldScan_Tick every 150 ms.
// A naive dword-by-dword walk would issue ~14700 IsBadReadPtr calls per actor
// per tick and tank the framerate. So:
//   * the body is copied in 4KB chunks (8 guarded reads, not 7360), then
//     scanned in our own buffer;
//   * the full search runs only on HUNT (g_actFullScan). Once the offset is
//     known, the tick path re-reads that ONE slot.
// Zip 34: the action slot, proven by alive/dead diff on uEm0100 AND uEm8000.

      // learned offset, sticky across ticks


// ─── Player + Main Pawn recon and transactional priority profiles ─────────
//
// This is deliberately separate from the enemy HUNT path:
//   * uPlayer is 0x5A10 bytes, not the 0x73C0 enemy body;
//   * enemy +0x2DC8 must not be assumed to be the player Act slot;
//   * we only need the Arisen and the main pawn for controlled experiments.
//
// FIND performs one dynamic DTI heap census: each distinct live vtable is
// named once by the game itself. No Steam/GOG-specific uPlayer vtable is
// hardcoded. Later '=' captures reuse the two addresses, so taking a snapshot
// while running does not repeat the heap hunt.
 // max: uPlayer = 23056
 // TypeAtlas: uCmc = 22752

 // uCmc also exists on non-party actors; rank after scan


   // power of two


static const int      kPawnAiDumpBytes = 1024;

// Test-save values supplied with build 35. We search both int32 and float
// representations. They are clues only: an offset is not documented until a
// controlled HP/stamina change confirms it.


// Small DTI objects potentially holding changing health/stamina state.  The
// heap census records every instance (not merely one sample per vtable), then
// the live CSV follows their first 32 bytes.  rPlStamina is a rule resource;
// cPlStamina or a Health-named object is the interesting runtime candidate.


// Build 40: candidates for the pawn's upper AI pipeline. The census sees
// inline heap subobjects too because it tests every aligned address carrying a
// genuine vtable. Association with the chosen uCmc/cCmcInfo is done later.


// Build 59 — разведка target-selection слоя. Объекты выбора цели:
// sRecognition/sRecognition::cEnemyInfo, sLockOnManager/*, sUnitSearchManager,
// cTarget* (все есть в TypeAtlas). Census их отсекал (PawnAiRelevantName не
// пропускал) — здесь отдельная коллекция с сырым дампом для offline-анализа.


static int            g_partyAiSeq = 0;
static char           g_partyAiLastFile[MAX_PATH] = "";
static char           g_partyAiStatus[192] = "AI bridge not captured";


// Build 46: generalized persistent priority profile. Every entry identifies
// one cCodeParam by the complete cPrioParam tuple plus personality rule index.
// No transient address is persisted. Switching profiles is transactional:
// validate all -> restore old -> apply all -> verify, otherwise rollback.


// Build 39 live trace. It is intentionally write-free with respect to game
// memory: only a CSV file is written. Find starts it; '-' stops/starts it.
static FILE*          g_partyTrace = nullptr;
static int            g_partyTraceSeq = 0;
static DWORD          g_partyTraceStartMs = 0;
static char           g_partyTraceFile[MAX_PATH] = "";

// Build 53: compact semantic transition trace. One baseline JSON provides the
// GOAP ActionInterfaceParam address map; CSV rows only record changing intent,
// exact cPlAct, current target and selected PlanCtrl node links.

static int            g_intentTraceSeq = 0;
static DWORD          g_intentTraceStartMs = 0;
static DWORD          g_intentTraceLastMs = 0;
static uint32_t       g_intentTraceLastCode = 0xFFFFFFFEu;
static uintptr_t      g_intentTraceLastActionVt = 0;
static uint32_t       g_intentTraceLastPacked = 0xFFFFFFFFu;
static uintptr_t      g_intentTraceLastTarget = 0;
static char           g_intentTraceFile[MAX_PATH] = "";
static bool           g_intentLiveValid = false;
static uint32_t       g_intentLiveCode = 0xFFFFFFFFu;
static char           g_intentLiveAction[64] = "";
static uintptr_t      g_intentLiveTarget = 0;
static char           g_intentLiveTargetName[64] = "";


// Build 56.2 — Guardian doctrine anchor/pawn world positions.
// +0x40/+0x44/+0x48 = world XYZ (SOURCE_OF_TRUTH §2, live units).
// Тела Аризена/пешки резолвятся тем же census'ом, что и priority-профиль
// (PartyFindBodies → PartyAssignRoles); позиции читаются ДЁШЕВО каждый тик,
// а сам census — throttled, только когда тела невалидны/не найдены.


  // для dedupe cleanup'а «world unload»


  // rate-limit диагностики


// Ленивый census + дешёвое чтение позиций каждый тик.
// Build 60:
//  - census в fast-режиме (partyOnly) — ранний выход по обоим телам, ~в разы быстрее;
//  - если тела устарели (vtable не совпал) — НЕМЕДЛЕННЫЙ пере-резолв без троттла
//    (иначе позиции висели 0,0,0 до смены мира — баг холодного старта);
//  - при неполном наборе (пешка ещё не найдена) — бэкофф повторов
//    (2с → 8с → 20с), а не постоянные 5с (не молотит полный скан).


static void PartyWriteJson()
{
    char fileName[64];
    sprintf_s(fileName, sizeof(fileName), "ddda_party_recon_%03d.json", g_partySeq);
    const char* path = ModPaths::File(fileName, 4);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) {
        sprintf_s(g_partyStatus, sizeof(g_partyStatus), "cannot write %s", fileName);
        return;
    }

    uintptr_t playerRecord = (pBase && *pBase) ? (uintptr_t)(*pBase + 0xA7000) : 0;
    uintptr_t mainPawnRecord = playerRecord ? playerRecord + 0x7F0 : 0;
    fprintf(f,
        "{\n  \"build\":\"%s\",\n  \"seq\":%d,\n  \"moduleBase\":\"0x%08X\",\n"
        "  \"bodySize\":%u,\n  \"findMs\":%u,\n  \"playerRecord\":\"0x%08X\",\n"
        "  \"mainPawnRecord\":\"0x%08X\",\n  \"pawnManagers\":%d,\n"
        "  \"testStats\":{\"player\":{\"hpCurrent\":331,\"hpMax\":498,\"stamina\":600},"
        "\"mainPawn\":{\"hpCurrent\":327,\"hpMax\":505,\"stamina\":595}},\n"
        "  \"discovery\":{\"vtChecked\":%d,\"vtNamed\":%d,\"rawCandidates\":%d},\n"
        "  \"nearTypes\":[",
        MOD_BUILD_TAG, g_partySeq, (unsigned)g_base, kPartyBodySize, g_partyFindMs,
        (unsigned)playerRecord, (unsigned)mainPawnRecord, g_nPartyPawnMgr,
        g_partyVtChecked, g_partyVtNamed, g_partyRawCandidates);
    for (int i = 0; i < g_nPartyNear; ++i)
        fprintf(f, "%s{\"name\":\"%s\",\"vt\":\"0x%08X\",\"sample\":\"0x%08X\"}",
            i ? "," : "", g_partyNear[i].name,
            (unsigned)g_partyNear[i].vt, (unsigned)g_partyNear[i].sample);
    fputs("],\n  \"runtimeProbes\":[", f);
    for (int i = 0; i < g_nPartyRuntime; ++i) {
        PartyRuntimeProbe& R = g_partyRuntime[i];
        fprintf(f, "%s{\"name\":\"%s\",\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\",\"headHex\":\"",
            i ? "," : "", R.name, (unsigned)R.ptr, (unsigned)R.vt);
        BYTE now[kPartyRuntimeProbeBytes] = {};
        bool ok = Rd((void*)R.ptr, now, sizeof(now));
        if (ok)
            for (int b = 0; b < kPartyRuntimeProbeBytes; ++b) fprintf(f, "%02X", now[b]);
        fputs("\"}", f);
    }
    fputs("],\n  \"bodies\":[\n", f);

    for (int i = 0; i < g_nParty; ++i) {
        PartyBodyDump& P = g_party[i];
        fprintf(f,
            "    %s{\"role\":\"%s\",\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\","
            "\"dti\":\"%s\",\"bodySize\":%u,\"playerRecordRef\":%s,\"mainPawnRecordRef\":%s,"
            "\"pawnManagerRef\":%s,\"hasPawnIntel\":%s,"
            "\"action\":{\"off\":\"0x%04X\",\"ptr\":\"0x%08X\",\"name\":\"%s\",\"ownerRef\":%s},"
            "\"bodyHex\":\"",
            i ? "," : " ", P.role, (unsigned)P.ptr, (unsigned)P.vt, P.dti, P.bodySize,
            P.playerRecordRef ? "true" : "false",
            P.mainPawnRecordRef ? "true" : "false",
            P.pawnManagerRef ? "true" : "false",
            P.hasPawnIntel ? "true" : "false",
            P.actOff, (unsigned)P.actPtr, P.actName,
            P.actOwnerRef ? "true" : "false");
        if (P.bodyOk)
            for (uint32_t b = 0; b < P.bodySize; ++b) fprintf(f, "%02X", P.body[b]);
        fputs("\",\"children\":[", f);
        for (int c = 0; c < P.nChild; ++c) {
            PartyChildDump& C = P.child[c];
            fprintf(f,
                "%s{\"off\":\"0x%04X\",\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\","
                "\"name\":\"%s\",\"ownerRef\":%s,\"headHex\":\"",
                c ? "," : "", C.off, (unsigned)C.ptr, (unsigned)C.vt,
                C.name, C.ownerRef ? "true" : "false");
            if (C.headOk)
                for (int b = 0; b < kPartyChildHeadSize; ++b) fprintf(f, "%02X", C.head[b]);
            fputs("\"}", f);
        }
        fputs("],\"knownValueHits\":[", f);
        for (int h = 0; h < P.nValueHit; ++h) {
            PartyValueHit& H = P.valueHit[h];
            fprintf(f,
                "%s{\"containerOff\":\"0x%04X\",\"container\":\"%s\","
                "\"valueOff\":\"0x%04X\",\"label\":\"%s\",\"encoding\":\"%s\"}",
                h ? "," : "", H.containerOff, H.container,
                H.valueOff, H.label, H.encoding);
        }
        fputs("]}\n", f);
    }
    fputs("  ]\n}\n", f);
    fclose(f);

    lstrcpynA(g_partyLastFile, path, sizeof(g_partyLastFile));
    sprintf_s(g_partyStatus, sizeof(g_partyStatus),
        "snapshot %03d: %d party bodies, %u ms find", g_partySeq, g_nParty, g_partyFindMs);
}

static PartyBodyDump* PartyRoleBody(const char* role)
{
    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].role, role)) return &g_party[i];
    return nullptr;
}

static int PartyFindPtrOffset(const BYTE* data, uint32_t bytes, uintptr_t want)
{
    if (!data || !want || bytes < 4) return -1;
    for (uint32_t off = 0; off + 4 <= bytes; off += 4)
        if (*(const uint32_t*)(data + off) == (uint32_t)want) return (int)off;
    return -1;
}

static uint32_t PartyHashBytes(const BYTE* data, uint32_t bytes)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < bytes; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static uintptr_t PartyMainCmcInfo(PartyBodyDump* pawn)
{
    if (!pawn) return 0;
    uintptr_t info = 0;
    if (!RdPtr((void*)(pawn->ptr + 0x3DEC), &info) || !info) return 0;
    char name[48] = {};
    if (!NameOfLiveObject(info, name, sizeof(name)) || strcmp(name, "cCmcInfo")) return 0;
    return info;
}

static void PartyWriteAiBridgeJson()
{
    PartyBodyDump* pawn = PartyRoleBody("Main Pawn");
    if (!pawn) {
        lstrcpynA(g_partyAiStatus, "AI bridge: main pawn not resolved", sizeof(g_partyAiStatus));
        return;
    }
    uintptr_t cmcInfo = PartyMainCmcInfo(pawn);
    const TypeAtlas::Info* cmcType = TypeAtlas::FindByName("cCmcInfo");
    uint32_t cmcSize = cmcType ? cmcType->size : 5728;
    BYTE* cmcBytes = (BYTE*)malloc(cmcSize);
    if (!cmcInfo || !cmcBytes || !Rd((void*)cmcInfo, cmcBytes, cmcSize)) {
        if (cmcBytes) free(cmcBytes);
        lstrcpynA(g_partyAiStatus, "AI bridge: cCmcInfo unavailable", sizeof(g_partyAiStatus));
        return;
    }

    ++g_partyAiSeq;
    char fileName[72];
    sprintf_s(fileName, sizeof(fileName), "ddda_pawn_ai_bridge_%03d.json", g_partyAiSeq);
    const char* path = ModPaths::File(fileName, 6);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) {
        free(cmcBytes);
        sprintf_s(g_partyAiStatus, sizeof(g_partyAiStatus), "AI bridge: cannot write %s", fileName);
        return;
    }

    PartyPriorityProfileUpdateState();
    uintptr_t pawnAction = 0;
    uint32_t pawnPackedAction = 0xFFFFFFFFu;
    char pawnActionName[64] = {};
    if (pawn->bodyOk) {
        pawnAction = *(uint32_t*)(pawn->body + 0x2DC8);
        pawnPackedAction = *(uint32_t*)(pawn->body + 0x2DD4);
        if (pawnAction) NameOfLiveObject(pawnAction, pawnActionName, sizeof(pawnActionName));
    }
    int profileAppliedRules = 0;
    int profileResolvedRules = 0;
    for (int i = 0; i < g_nPriorityProfileRules; ++i) {
        if (g_priorityProfileRules[i].resolved) ++profileResolvedRules;
        if (g_priorityProfileRules[i].applied) ++profileAppliedRules;
    }

    fprintf(f,
        "{\n  \"build\":\"%s\",\n  \"seq\":%d,\n"
        "  \"pawnBody\":\"0x%08X\",\n  \"cmcInfo\":\"0x%08X\",\n"
        "  \"pawnAction\":{\"ptr\":\"0x%08X\",\"name\":\"%s\","
        "\"packedCode\":\"0x%08X\"},\n"
        "  \"cmcInfoSize\":%u,\n  \"censusCandidates\":%d,\n"
        "  \"priorityProfile\":{\"active\":\"%s\","
        "\"ruleCount\":%d,\"resolvedRules\":%d,\"appliedRules\":%d,"
        "\"fileOk\":%s,\"applied\":%s,\"converged\":%s,"
        "\"writes\":%d,\"restores\":%d},\n"
        "  \"cmcInfoHex\":\"",
        MOD_BUILD_TAG, g_partyAiSeq, (unsigned)pawn->ptr, (unsigned)cmcInfo,
        (unsigned)pawnAction, pawnActionName, pawnPackedAction,
        cmcSize, g_nPawnAi, g_priorityProfileActive,
        g_nPriorityProfileRules, profileResolvedRules, profileAppliedRules,
        g_priorityProfileFileOk ? "true" : "false",
        g_priorityProfileApplied ? "true" : "false",
        g_priorityProfileConverged ? "true" : "false",
        g_priorityProfileWrites, g_priorityProfileRestores);
    for (uint32_t b = 0; b < cmcSize; ++b) fprintf(f, "%02X", cmcBytes[b]);
    fputs("\",\n  \"objects\":[\n", f);

    int written = 0;
    for (int i = 0; i < g_nPawnAi; ++i) {
        PawnAiCandidate& A = g_pawnAi[i];
        uintptr_t currentVt = 0;
        if (!RdPtr((void*)A.ptr, &currentVt) || currentVt != A.vt) continue;

        uint32_t size = A.typeSize;
        if (!size || size > 0x10000u) size = kPawnAiDumpBytes;
        BYTE* bytes = (BYTE*)malloc(size);
        if (!bytes || !Rd((void*)A.ptr, bytes, size)) {
            if (bytes) free(bytes);
            continue;
        }

        int bodyRefOff = pawn->bodyOk ? PartyFindPtrOffset(pawn->body, pawn->bodySize, A.ptr) : -1;
        int infoRefOff = PartyFindPtrOffset(cmcBytes, cmcSize, A.ptr);
        int containsBodyOff = PartyFindPtrOffset(bytes, size, pawn->ptr);
        int containsInfoOff = PartyFindPtrOffset(bytes, size, cmcInfo);
        int inlineInfoOff = (A.ptr >= cmcInfo && A.ptr < cmcInfo + cmcSize)
            ? (int)(A.ptr - cmcInfo) : -1;
        uint32_t hash = PartyHashBytes(bytes, size);
        uint32_t dump = size < (uint32_t)kPawnAiDumpBytes ? size : (uint32_t)kPawnAiDumpBytes;

        fprintf(f,
            "    %s{\"name\":\"%s\",\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\","
            "\"size\":%u,\"hash\":\"0x%08X\",\"bodyRefOff\":%d,"
            "\"infoRefOff\":%d,\"containsBodyOff\":%d,\"containsInfoOff\":%d,"
            "\"inlineInfoOff\":%d,\"headHex\":\"",
            written ? "," : "", A.name, (unsigned)A.ptr, (unsigned)A.vt,
            size, hash, bodyRefOff, infoRefOff, containsBodyOff, containsInfoOff,
            inlineInfoOff);
        for (uint32_t b = 0; b < dump; ++b) fprintf(f, "%02X", bytes[b]);
        fputs("\"}\n", f);
        ++written;
        free(bytes);
    }
    fputs("  ],\n  \"aiCtrlGraph\":{", f);
    uintptr_t aiCtrl = 0;
    if (pawn->bodyOk) aiCtrl = *(uint32_t*)(pawn->body + 0x2E64);
    const TypeAtlas::Info* aiCtrlType = TypeAtlas::FindByName("cAICtrl");
    const uint32_t aiCtrlSize = aiCtrlType ? aiCtrlType->size : 704u;
    BYTE aiCtrlRaw[704] = {};
    const bool aiCtrlOk = aiCtrl && aiCtrlSize <= sizeof(aiCtrlRaw)
        && Rd((void*)aiCtrl, aiCtrlRaw, aiCtrlSize);
    fprintf(f,
        "\"ptr\":\"0x%08X\",\"ok\":%s,\"size\":%u,\"hex\":\"",
        (unsigned)aiCtrl, aiCtrlOk ? "true" : "false", aiCtrlSize);
    if (aiCtrlOk)
        for (uint32_t b = 0; b < aiCtrlSize; ++b) fprintf(f, "%02X", aiCtrlRaw[b]);
    fputs("\",\"children\":[", f);

    int aiChildWritten = 0;
    uintptr_t seenAiChild[128] = {};
    int nSeenAiChild = 0;
    if (aiCtrlOk) {
        for (uint32_t off = 0; off + 4u <= aiCtrlSize; off += 4u) {
            const uintptr_t child = *(uint32_t*)(aiCtrlRaw + off);
            if (!LooksHeap(child)) continue;
            bool duplicate = false;
            for (int s = 0; s < nSeenAiChild; ++s)
                if (seenAiChild[s] == child) { duplicate = true; break; }
            if (duplicate || nSeenAiChild >= 128) continue;
            char childName[64] = {};
            if (!NameOfLiveObject(child, childName, sizeof(childName))) continue;
            const TypeAtlas::Info* childType = TypeAtlas::FindByName(childName);
            uint32_t childSize = childType ? childType->size : 0u;
            if (!childSize || childSize > 0x1000u) childSize = 0x1000u;
            BYTE* childRaw = (BYTE*)malloc(childSize);
            if (!childRaw || !Rd((void*)child, childRaw, childSize)) {
                if (childRaw) free(childRaw);
                continue;
            }
            seenAiChild[nSeenAiChild++] = child;
            fprintf(f,
                "%s{\"fieldOff\":\"0x%04X\",\"ptr\":\"0x%08X\","
                "\"name\":\"%s\",\"size\":%u,\"mainBodyRefOff\":%d,"
                "\"targetRefs\":[",
                aiChildWritten ? "," : "", off, (unsigned)child, childName,
                childSize, PartyFindPtrOffset(childRaw, childSize, pawn->ptr));
            int aiTargetWritten = 0;
            for (int e = 0; e < g_nAct; ++e) {
                if (!g_act[e].ptr) continue;
                const int hit = PartyFindPtrOffset(childRaw, childSize, g_act[e].ptr);
                if (hit < 0) continue;
                fprintf(f,
                    "%s{\"off\":\"0x%04X\",\"targetPtr\":\"0x%08X\","
                    "\"targetKind\":\"%s\"}",
                    aiTargetWritten ? "," : "", hit, (unsigned)g_act[e].ptr,
                    g_act[e].kind ? g_act[e].kind : "?");
                ++aiTargetWritten;
            }
            fputs("]}", f);
            ++aiChildWritten;
            free(childRaw);
        }
    }
    fputs("]},\n  \"decisionRoots\":[", f);
    int decisionWritten = 0;
    for (int i = 0; i < g_nPawnAi; ++i) {
        PawnAiCandidate& A = g_pawnAi[i];
        const bool plannerRoot = !strcmp(A.name, "cAIGoalPlanning");
        const bool priorityRoot = !strcmp(A.name, "cAIPriorityThink");
        if (!plannerRoot && !priorityRoot) continue;

        uint32_t size = A.typeSize;
        if (!size || size > 0x10000u) continue;
        BYTE* raw = (BYTE*)malloc(size);
        if (!raw || !Rd((void*)A.ptr, raw, size)) {
            if (raw) free(raw);
            continue;
        }
        const uintptr_t field4 = size >= 8 ? *(uint32_t*)(raw + 0x04) : 0;
        const uintptr_t field8 = size >= 12 ? *(uint32_t*)(raw + 0x08) : 0;
        char field4Name[64] = {};
        char field8Name[64] = {};
        if (LooksHeap(field4)) NameOfLiveObject(field4, field4Name, sizeof(field4Name));
        if (LooksHeap(field8)) NameOfLiveObject(field8, field8Name, sizeof(field8Name));
        uint32_t selectedCode = 0xFFFFFFFFu;
        if (plannerRoot && size >= 0x180u) selectedCode = *(uint32_t*)(raw + 0x17C);
        const int bodyRef = pawn->bodyOk
            ? PartyFindPtrOffset(raw, size, pawn->ptr) : -1;
        const int infoRef = PartyFindPtrOffset(raw, size, cmcInfo);
        BYTE ownerHex[0x100] = {};
        bool ownerHexOk = LooksHeap(field4)
            && Rd((void*)field4, ownerHex, sizeof(ownerHex));
        const uint32_t selectedPlanOff = selectedCode < 91u
            ? 0x190u + selectedCode * 0x110u : 0u;
        const bool selectedPlanOk = plannerRoot && selectedCode < 91u
            && selectedPlanOff + 0x110u <= size;

        fprintf(f,
            "%s{\"censusIndex\":%d,\"name\":\"%s\","
            "\"ptr\":\"0x%08X\",\"size\":%u,\"hash\":\"0x%08X\","
            "\"field4\":\"0x%08X\",\"field4Name\":\"%s\","
            "\"field8\":\"0x%08X\",\"field8Name\":\"%s\","
            "\"selectedCode\":%u,\"mainBodyRefOff\":%d,"
            "\"mainInfoRefOff\":%d,\"ownerHex\":\"",
            decisionWritten ? "," : "", i, A.name, (unsigned)A.ptr,
            size, PartyHashBytes(raw, size), (unsigned)field4, field4Name,
            (unsigned)field8, field8Name, selectedCode, bodyRef, infoRef);
        if (ownerHexOk)
            for (int b = 0; b < (int)sizeof(ownerHex); ++b)
                fprintf(f, "%02X", ownerHex[b]);
        fprintf(f, "\",\"selectedPlanHash\":\"0x%08X\",\"selectedPlanHex\":\"",
            selectedPlanOk
                ? PartyHashBytes(raw + selectedPlanOff, 0x110u) : 0u);
        if (selectedPlanOk)
            for (uint32_t b = 0; b < 0x110u; ++b)
                fprintf(f, "%02X", raw[selectedPlanOff + b]);
        fputs("\",\"selectedPlanTargets\":[", f);

        int planTargetWritten = 0;
        uintptr_t seenPlanTarget[32] = {};
        int nSeenPlanTarget = 0;
        if (selectedPlanOk) {
            BYTE* plan = raw + selectedPlanOff;
            for (int off = 0; off + 4 <= 0x110; off += 4) {
                const uintptr_t target = *(uint32_t*)(plan + off);
                if (!LooksHeap(target)) continue;
                bool duplicate = false;
                for (int s = 0; s < nSeenPlanTarget; ++s)
                    if (seenPlanTarget[s] == target) { duplicate = true; break; }
                if (duplicate || nSeenPlanTarget >= 32) continue;
                BYTE targetRaw[0x200] = {};
                if (!Rd((void*)target, targetRaw, sizeof(targetRaw))) continue;
                seenPlanTarget[nSeenPlanTarget++] = target;
                fprintf(f,
                    "%s{\"fieldOff\":\"0x%02X\",\"ptr\":\"0x%08X\","
                    "\"hash\":\"0x%08X\",\"hex\":\"",
                    planTargetWritten ? "," : "", off, (unsigned)target,
                    PartyHashBytes(targetRaw, sizeof(targetRaw)));
                for (int b = 0; b < (int)sizeof(targetRaw); ++b)
                    fprintf(f, "%02X", targetRaw[b]);
                fputs("\",\"children\":[", f);

                int planChildWritten = 0;
                uintptr_t seenPlanChild[64] = {};
                int nSeenPlanChild = 0;
                for (int po = 0; po + 4 <= 0x100; po += 4) {
                    const uintptr_t child = *(uint32_t*)(targetRaw + po);
                    if (!LooksHeap(child)) continue;
                    bool childDuplicate = false;
                    for (int s = 0; s < nSeenPlanChild; ++s)
                        if (seenPlanChild[s] == child) { childDuplicate = true; break; }
                    if (childDuplicate || nSeenPlanChild >= 64) continue;
                    char childName[64] = {};
                    if (!NameOfLiveObject(child, childName, sizeof(childName))) continue;
                    if (!(PartyStartsWith(childName, "cAIGoalPlanning::")
                        || PartyStartsWith(childName, "cCmc")
                        || !strcmp(childName, "cAIActionInterfaceCmc")))
                        continue;
                    const TypeAtlas::Info* childType = TypeAtlas::FindByName(childName);
                    uint32_t childSize = childType ? childType->size : 0;
                    if (!childSize || childSize > 0x400u) continue;
                    BYTE childRaw[0x400] = {};
                    if (!Rd((void*)child, childRaw, childSize)) continue;
                    seenPlanChild[nSeenPlanChild++] = child;
                    fprintf(f,
                        "%s{\"payloadOff\":\"0x%02X\","
                        "\"ptr\":\"0x%08X\",\"name\":\"%s\","
                        "\"size\":%u,\"hex\":\"",
                        planChildWritten ? "," : "", po, (unsigned)child,
                        childName, childSize);
                    for (uint32_t b = 0; b < childSize; ++b)
                        fprintf(f, "%02X", childRaw[b]);
                    const bool planningNode = !strcmp(
                        childName, "cAIGoalPlanning::cGoalPlanningNode");
                    const uintptr_t linkPtr = planningNode && childSize >= 8u
                        ? *(uint32_t*)(childRaw + 0x04) : 0;
                    fprintf(f, "\",\"linkPtr\":\"0x%08X\",\"nearbyCmc\":[",
                        (unsigned)linkPtr);
                    int nearbyWritten = 0;
                    uintptr_t seenNearby[32] = {};
                    int nSeenNearby = 0;
                    if (LooksHeap(linkPtr)) {
                        const uintptr_t begin = linkPtr > 0x100u
                            ? linkPtr - 0x100u : linkPtr;
                        const uintptr_t end = linkPtr + 0x800u;
                        for (uintptr_t at = begin; at + 4u <= end; at += 4u) {
                            uintptr_t vt = 0;
                            if (!RdPtr((void*)at, &vt) || !LooksLikeVtable(vt))
                                continue;
                            char nearbyName[64] = {};
                            if (!NameOfLiveObject(at, nearbyName, sizeof(nearbyName))
                                || !PartyStartsWith(nearbyName, "cCmc"))
                                continue;
                            bool duplicateNearby = false;
                            for (int s = 0; s < nSeenNearby; ++s)
                                if (seenNearby[s] == at) { duplicateNearby = true; break; }
                            if (duplicateNearby || nSeenNearby >= 32) continue;
                            seenNearby[nSeenNearby++] = at;
                            const TypeAtlas::Info* nearbyType = TypeAtlas::FindByName(nearbyName);
                            fprintf(f,
                                "%s{\"ptr\":\"0x%08X\",\"delta\":%d,"
                                "\"name\":\"%s\",\"size\":%u}",
                                nearbyWritten ? "," : "", (unsigned)at,
                                (int)(at - linkPtr), nearbyName,
                                nearbyType ? nearbyType->size : 0u);
                            ++nearbyWritten;
                        }
                    }
                    fputs("]}", f);
                    ++planChildWritten;
                }
                fputs("]}", f);
                ++planTargetWritten;
            }
        }
        fputs("]}", f);
        ++decisionWritten;
        free(raw);
    }

    fputs("],\n  \"goapResources\":[", f);
    int goapWritten = 0;
    for (int i = 0; i < g_nPawnAi; ++i) {
        PawnAiCandidate& A = g_pawnAi[i];
        if (strcmp(A.name, "rAIGoalPlanning")) continue;
        BYTE rootRaw[160] = {};
        if (!Rd((void*)A.ptr, rootRaw, sizeof(rootRaw))) continue;
        fprintf(f,
            "%s{\"ptr\":\"0x%08X\",\"hash\":\"0x%08X\","
            "\"hex\":\"",
            goapWritten ? "," : "", (unsigned)A.ptr,
            PartyHashBytes(rootRaw, sizeof(rootRaw)));
        for (int b = 0; b < (int)sizeof(rootRaw); ++b) fprintf(f, "%02X", rootRaw[b]);
        fputs("\",\"targets\":[", f);

        uintptr_t seenTarget[32] = {};
        int nSeenTarget = 0;
        int targetWritten = 0;
        for (int off = 0; off + 4 <= (int)sizeof(rootRaw); off += 4) {
            const uintptr_t target = *(uint32_t*)(rootRaw + off);
            if (!LooksHeap(target)) continue;
            bool duplicate = false;
            for (int s = 0; s < nSeenTarget; ++s)
                if (seenTarget[s] == target) { duplicate = true; break; }
            if (duplicate || nSeenTarget >= 32) continue;
            BYTE targetRaw[0x100] = {};
            if (!Rd((void*)target, targetRaw, sizeof(targetRaw))) continue;
            seenTarget[nSeenTarget++] = target;
            fprintf(f,
                "%s{\"fieldOff\":\"0x%02X\",\"ptr\":\"0x%08X\","
                "\"hash\":\"0x%08X\",\"hex\":\"",
                targetWritten ? "," : "", off, (unsigned)target,
                PartyHashBytes(targetRaw, sizeof(targetRaw)));
            for (int b = 0; b < (int)sizeof(targetRaw); ++b)
                fprintf(f, "%02X", targetRaw[b]);
            fputs("\",\"children\":[", f);

            int childWritten = 0;
            uintptr_t seenChild[32] = {};
            int nSeenChild = 0;
            for (int po = 0; po + 4 <= 0x80; po += 4) {
                const uintptr_t child = *(uint32_t*)(targetRaw + po);
                if (!LooksHeap(child)) continue;
                bool childDuplicate = false;
                for (int s = 0; s < nSeenChild; ++s)
                    if (seenChild[s] == child) { childDuplicate = true; break; }
                if (childDuplicate || nSeenChild >= 32) continue;
                char childName[64] = {};
                if (!NameOfLiveObject(child, childName, sizeof(childName))
                    || !PartyStartsWith(childName, "rAIGoalPlanning::"))
                    continue;
                const TypeAtlas::Info* childType = TypeAtlas::FindByName(childName);
                uint32_t childSize = childType ? childType->size : 0;
                if (!childSize || childSize > 0xA0u) continue;
                BYTE childRaw[0xA0] = {};
                if (!Rd((void*)child, childRaw, childSize)) continue;
                seenChild[nSeenChild++] = child;
                fprintf(f,
                    "%s{\"payloadOff\":\"0x%02X\",\"ptr\":\"0x%08X\","
                    "\"name\":\"%s\",\"size\":%u,\"hex\":\"",
                    childWritten ? "," : "", po, (unsigned)child,
                    childName, childSize);
                for (uint32_t b = 0; b < childSize; ++b) fprintf(f, "%02X", childRaw[b]);
                fputs("\"}", f);
                ++childWritten;
            }
            fputs("]}", f);
            ++targetWritten;
        }
        fputs("]}", f);
        ++goapWritten;
    }

    fputs("],\n  \"plannerSlots\":[", f);
    uintptr_t mainPlanner = 0;
    if (aiCtrlOk && aiCtrlSize >= 0x6Cu)
        mainPlanner = *(uint32_t*)(aiCtrlRaw + 0x68);
    int plannerSlotWritten = 0;
    static const uint32_t kPlanArrayDesc[] = { 0x04u, 0x38u, 0x4Cu, 0x68u };
    for (uint32_t code = 0; code < 91u; ++code) {
        const uintptr_t plan = mainPlanner + 0x190u + code * 0x110u;
        BYTE planRaw[0x110] = {};
        if (!mainPlanner || !Rd((void*)plan, planRaw, sizeof(planRaw))) continue;
        fprintf(f,
            "%s{\"code\":%u,\"ptr\":\"0x%08X\","
            "\"hash\":\"0x%08X\",\"nodeLinks\":[",
            plannerSlotWritten ? "," : "", code, (unsigned)plan,
            PartyHashBytes(planRaw, sizeof(planRaw)));

        uintptr_t seenLinks[64] = {};
        int nSeenLinks = 0;
        int linkWritten = 0;
        for (int d = 0; d < (int)(sizeof(kPlanArrayDesc) / sizeof(kPlanArrayDesc[0])); ++d) {
            const uint32_t desc = kPlanArrayDesc[d];
            const uint32_t count = *(uint32_t*)(planRaw + desc + 0x04u);
            const uint32_t capacity = *(uint32_t*)(planRaw + desc + 0x08u);
            const uintptr_t arrayPtr = *(uint32_t*)(planRaw + desc + 0x10u);
            if (!count || count > 16u || capacity > 16u || count > capacity
                || !LooksHeap(arrayPtr))
                continue;
            uintptr_t entries[16] = {};
            if (!Rd((void*)arrayPtr, entries, count * sizeof(uintptr_t))) continue;
            for (uint32_t n = 0; n < count; ++n) {
                const uintptr_t node = entries[n];
                char nodeName[64] = {};
                if (!NameOfLiveObject(node, nodeName, sizeof(nodeName))
                    || strcmp(nodeName, "cAIGoalPlanning::cGoalPlanningNode"))
                    continue;
                uint32_t nodeRaw[8] = {};
                if (!Rd((void*)node, nodeRaw, sizeof(nodeRaw))) continue;
                const uintptr_t link = nodeRaw[1];
                bool duplicate = false;
                for (int s = 0; s < nSeenLinks; ++s)
                    if (seenLinks[s] == link) { duplicate = true; break; }
                if (duplicate || nSeenLinks >= 64) continue;
                seenLinks[nSeenLinks++] = link;
                fprintf(f,
                    "%s{\"descriptorOff\":\"0x%02X\","
                    "\"nodePtr\":\"0x%08X\",\"linkPtr\":\"0x%08X\"}",
                    linkWritten ? "," : "", desc, (unsigned)node,
                    (unsigned)link);
                ++linkWritten;
            }
        }
        fputs("]}", f);
        ++plannerSlotWritten;
    }

    fputs("],\n  \"mainPawnTargetSlots\":[", f);
    static const uint32_t kTargetSlotOffs[] = { 0x14E0u, 0x2EB8u, 0x4B28u };
    for (int i = 0; i < (int)(sizeof(kTargetSlotOffs) / sizeof(kTargetSlotOffs[0])); ++i) {
        const uint32_t off = kTargetSlotOffs[i];
        uintptr_t value = 0;
        if (pawn->bodyOk && off + 4u <= pawn->bodySize)
            value = *(uint32_t*)(pawn->body + off);
        char valueName[64] = {};
        if (LooksHeap(value)) NameOfLiveObject(value, valueName, sizeof(valueName));
        const char* knownKind = "";
        for (int e = 0; e < g_nAct; ++e)
            if (g_act[e].ptr == value) {
                knownKind = g_act[e].kind ? g_act[e].kind : "?";
                break;
            }
        fprintf(f,
            "%s{\"off\":\"0x%04X\",\"value\":\"0x%08X\","
            "\"name\":\"%s\",\"knownEnemyKind\":\"%s\"}",
            i ? "," : "", off, (unsigned)value, valueName, knownKind);
    }

    fputs("],\n  \"targetRefs\":[", f);
    int targetRefWritten = 0;
    // Direct references from the selected main pawn body and cCmcInfo.
    for (int e = 0; e < g_nAct; ++e) {
        if (!g_act[e].ptr) continue;
        int hit = pawn->bodyOk
            ? PartyFindPtrOffset(pawn->body, pawn->bodySize, g_act[e].ptr) : -1;
        if (hit >= 0) {
            fprintf(f,
                "%s{\"owner\":\"uCmc\",\"ownerPtr\":\"0x%08X\","
                "\"off\":\"0x%04X\",\"targetPtr\":\"0x%08X\","
                "\"targetKind\":\"%s\"}",
                targetRefWritten ? "," : "", (unsigned)pawn->ptr, hit,
                (unsigned)g_act[e].ptr, g_act[e].kind ? g_act[e].kind : "?");
            ++targetRefWritten;
        }
        hit = PartyFindPtrOffset(cmcBytes, cmcSize, g_act[e].ptr);
        if (hit >= 0) {
            fprintf(f,
                "%s{\"owner\":\"cCmcInfo\",\"ownerPtr\":\"0x%08X\","
                "\"off\":\"0x%04X\",\"targetPtr\":\"0x%08X\","
                "\"targetKind\":\"%s\"}",
                targetRefWritten ? "," : "", (unsigned)cmcInfo, hit,
                (unsigned)g_act[e].ptr, g_act[e].kind ? g_act[e].kind : "?");
            ++targetRefWritten;
        }
    }
    for (int i = 0; i < g_nPawnAi; ++i) {
        PawnAiCandidate& A = g_pawnAi[i];
        if (!(PartyStartsWith(A.name, "cCmc")
            || !strcmp(A.name, "cAICheckSituationCmc")
            || !strcmp(A.name, "cAIGoalPlanning")
            || !strcmp(A.name, "cAIPriorityThink")))
            continue;
        uint32_t scanSize = A.typeSize;
        if (!scanSize || scanSize > 0x1000u) scanSize = 0x1000u;
        BYTE* raw = (BYTE*)malloc(scanSize);
        if (!raw || !Rd((void*)A.ptr, raw, scanSize)) {
            if (raw) free(raw);
            continue;
        }
        for (int e = 0; e < g_nAct; ++e) {
            if (!g_act[e].ptr) continue;
            const int hit = PartyFindPtrOffset(raw, scanSize, g_act[e].ptr);
            if (hit < 0) continue;
            fprintf(f,
                "%s{\"owner\":\"%s\",\"ownerPtr\":\"0x%08X\","
                "\"off\":\"0x%04X\",\"targetPtr\":\"0x%08X\","
                "\"targetKind\":\"%s\"}",
                targetRefWritten ? "," : "", A.name, (unsigned)A.ptr, hit,
                (unsigned)g_act[e].ptr, g_act[e].kind ? g_act[e].kind : "?");
            ++targetRefWritten;
        }
        free(raw);
    }

    fputs("],\n  \"priorityBuckets\":[", f);

    // Build 42 proved that cAIPriorityThink owns 48 cArray descriptors, not
    // separate 0x90-byte score objects. Each descriptor is 0x14 bytes at
    // +0x38; mpArray is +0x10 and contains exactly count cPrioParam pointers.
    // Capture descriptor -> payload coherently by verifying the descriptor did
    // not change during the read. Heap buffers can rotate while the game runs.
    PawnAiCandidate* priority = nullptr;
    for (int i = 0; i < g_nPawnAi; ++i)
        if (!strcmp(g_pawnAi[i].name, "cAIPriorityThink")) {
            priority = &g_pawnAi[i];
            break;
        }

    int bucketWritten = 0;
    if (priority) {
        for (int slot = 0; slot < 48; ++slot) {
            const uint32_t descriptorOff = 0x38u + (uint32_t)slot * 0x14u;
            uint32_t before[5] = {};
            uint32_t after[5] = {};
            uintptr_t entries[16] = {};
            bool coherent = false;
            bool payloadOk = false;

            for (int attempt = 0; attempt < 3 && !coherent; ++attempt) {
                memset(before, 0, sizeof(before));
                memset(after, 0, sizeof(after));
                memset(entries, 0, sizeof(entries));
                if (!Rd((void*)(priority->ptr + descriptorOff), before, sizeof(before)))
                    break;
                const uint32_t count = before[1];
                const uint32_t capacity = before[2];
                const uintptr_t arrayPtr = before[4];
                if (count > 16u || capacity > 16u || count > capacity)
                    break;
                payloadOk = count == 0u
                    || (LooksHeap(arrayPtr)
                        && Rd((void*)arrayPtr, entries, count * sizeof(uintptr_t)));
                if (!payloadOk) break;
                if (!Rd((void*)(priority->ptr + descriptorOff), after, sizeof(after)))
                    break;
                coherent = memcmp(before, after, sizeof(before)) == 0;
            }

            fprintf(f,
                "%s{\"slotIndex\":%d,\"descriptorOff\":\"0x%04X\","
                "\"pointerOff\":\"0x%04X\",\"coherent\":%s,"
                "\"vtable\":\"0x%08X\",\"count\":%u,\"capacity\":%u,"
                "\"flags\":%u,\"ptr\":\"0x%08X\",\"payloadOk\":%s,"
                "\"pointers\":[",
                bucketWritten ? "," : "", slot, descriptorOff,
                descriptorOff + 0x10u, coherent ? "true" : "false",
                before[0], before[1], before[2], before[3], before[4],
                payloadOk ? "true" : "false");
            for (uint32_t n = 0; n < before[1] && n < 16u; ++n)
                fprintf(f, "%s\"0x%08X\"", n ? "," : "", (unsigned)entries[n]);
            fputs("]}", f);
            ++bucketWritten;
        }
    }

    fputs("],\n  \"priorityRules\":[", f);

    // cPrioParam contains two cArray descriptors:
    //   +0x18 count +0x1C, capacity +0x20, mpArray +0x28 (cCodeParam*)
    //   +0x2C count +0x30, capacity +0x34, mpArray +0x3C (cOrderValue*)
    // Dump pointer arrays and full children. cCodeParam pointer fields are
    // followed once so nested personality checks can be mapped offline.
    int ruleWritten = 0;
    for (int i = 0; i < g_nPawnAi; ++i) {
        PawnAiCandidate& A = g_pawnAi[i];
        if (strcmp(A.name, "rAIPriorityThink::cPrioParam")) continue;

        BYTE prioRaw[64] = {};
        uintptr_t currentVt = 0;
        if (!RdPtr((void*)A.ptr, &currentVt) || currentVt != A.vt
            || !Rd((void*)A.ptr, prioRaw, sizeof(prioRaw)))
            continue;

        const uint32_t sensor = *(uint32_t*)(prioRaw + 0x04);
        const uint32_t code = *(uint32_t*)(prioRaw + 0x08);
        uint32_t personalityCount = *(uint32_t*)(prioRaw + 0x1C);
        const uint32_t personalityCapacity = *(uint32_t*)(prioRaw + 0x20);
        const uintptr_t personalityArray = *(uint32_t*)(prioRaw + 0x28);
        uint32_t orderCount = *(uint32_t*)(prioRaw + 0x30);
        const uint32_t orderCapacity = *(uint32_t*)(prioRaw + 0x34);
        const uintptr_t orderArray = *(uint32_t*)(prioRaw + 0x3C);
        if (personalityCount > 16u) personalityCount = 0;
        if (orderCount > 16u) orderCount = 0;

        uintptr_t personalityPtrs[16] = {};
        uintptr_t orderPtrs[16] = {};
        const bool personalityArrayOk = personalityCount == 0u
            || (LooksHeap(personalityArray)
                && Rd((void*)personalityArray, personalityPtrs,
                    personalityCount * sizeof(uintptr_t)));
        const bool orderArrayOk = orderCount == 0u
            || (LooksHeap(orderArray)
                && Rd((void*)orderArray, orderPtrs, orderCount * sizeof(uintptr_t)));

        fprintf(f,
            "%s{\"prioPtr\":\"0x%08X\",\"sensor\":%u,\"code\":%u,"
            "\"personalityCount\":%u,\"personalityCapacity\":%u,"
            "\"personalityArray\":\"0x%08X\",\"personalityArrayOk\":%s,"
            "\"personalityItems\":[",
            ruleWritten ? "," : "", (unsigned)A.ptr, sensor, code,
            personalityCount, personalityCapacity, (unsigned)personalityArray,
            personalityArrayOk ? "true" : "false");

        if (personalityArrayOk) {
            for (uint32_t n = 0; n < personalityCount; ++n) {
                BYTE child[104] = {};
                const uintptr_t childPtr = personalityPtrs[n];
                const bool childOk = LooksHeap(childPtr)
                    && Rd((void*)childPtr, child, sizeof(child));
                fprintf(f, "%s{\"ptr\":\"0x%08X\",\"ok\":%s,\"hex\":\"",
                    n ? "," : "", (unsigned)childPtr, childOk ? "true" : "false");
                if (childOk)
                    for (int b = 0; b < (int)sizeof(child); ++b) fprintf(f, "%02X", child[b]);
                fputs("\",\"heapTargets\":[", f);

                int targetWritten = 0;
                if (childOk) {
                    for (int off = 0; off + 4 <= (int)sizeof(child); off += 4) {
                        const uintptr_t target = *(uint32_t*)(child + off);
                        if (!LooksHeap(target)) continue;
                        BYTE targetRaw[0x80] = {};
                        if (!Rd((void*)target, targetRaw, sizeof(targetRaw))) continue;
                        fprintf(f,
                            "%s{\"fieldOff\":\"0x%02X\",\"ptr\":\"0x%08X\","
                            "\"hex\":\"",
                            targetWritten ? "," : "", off, (unsigned)target);
                        for (int b = 0; b < (int)sizeof(targetRaw); ++b)
                            fprintf(f, "%02X", targetRaw[b]);
                        fputs("\"}", f);
                        ++targetWritten;
                    }
                }
                fputs("]}", f);
            }
        }

        fprintf(f,
            "],\"orderCount\":%u,\"orderCapacity\":%u,"
            "\"orderArray\":\"0x%08X\",\"orderArrayOk\":%s,"
            "\"orderItems\":[",
            orderCount, orderCapacity, (unsigned)orderArray,
            orderArrayOk ? "true" : "false");
        if (orderArrayOk) {
            for (uint32_t n = 0; n < orderCount; ++n) {
                BYTE child[12] = {};
                const uintptr_t childPtr = orderPtrs[n];
                const bool childOk = LooksHeap(childPtr)
                    && Rd((void*)childPtr, child, sizeof(child));
                fprintf(f, "%s{\"ptr\":\"0x%08X\",\"ok\":%s,\"hex\":\"",
                    n ? "," : "", (unsigned)childPtr, childOk ? "true" : "false");
                if (childOk)
                    for (int b = 0; b < (int)sizeof(child); ++b) fprintf(f, "%02X", child[b]);
                fputs("\"}", f);
            }
        }
        fputs("]}", f);
        ++ruleWritten;
    }

    fputs("],\n  \"profileRules\":[", f);
    for (int i = 0; i < g_nPriorityProfileRules; ++i) {
        PartyPriorityProfileRule& R = g_priorityProfileRules[i];
        fprintf(f,
            "%s{\"index\":%d,\"sensor\":%u,\"code\":%u,"
            "\"category\":%u,\"objectId\":%u,\"extra\":%u,"
            "\"ruleIndex\":%u,\"expectedAddS32\":%d,"
            "\"desiredAddS32\":%d,\"expectedBreak\":%u,"
            "\"expectedCheckCount\":%u,\"expectedSlot\":%d,"
            "\"prioPtr\":\"0x%08X\",\"rulePtr\":\"0x%08X\","
            "\"resolved\":%s,\"applied\":%s,\"currentAddS32\":%d,"
            "\"liveSlot\":%d}",
            i ? "," : "", i, R.sensor, R.code, R.category, R.objectId,
            R.extra, R.ruleIndex, R.expectedAddS32, R.desiredAddS32,
            R.expectedBreak, R.expectedCheckCount, R.expectedSlot,
            (unsigned)R.prioPtr, (unsigned)R.rulePtr,
            R.resolved ? "true" : "false", R.applied ? "true" : "false",
            R.currentAddS32, R.liveSlot);
    }
    fputs("],\n  \"cmcInfoNamedPointers\":[", f);
    int namedWritten = 0;
    for (uint32_t off = 0; off + 4 <= cmcSize; off += 4) {
        uintptr_t child = *(uint32_t*)(cmcBytes + off);
        if (!LooksHeap(child)) continue;
        uintptr_t childVt = 0;
        if (!RdPtr((void*)child, &childVt) || !LooksLikeVtable(childVt)) continue;
        char childName[64] = {};
        if (!NameOfLiveObject(child, childName, sizeof(childName)) || !childName[0]) continue;
        fprintf(f,
            "%s{\"off\":\"0x%04X\",\"ptr\":\"0x%08X\","
            "\"vt\":\"0x%08X\",\"name\":\"%s\"}",
            namedWritten ? "," : "", off, (unsigned)child,
            (unsigned)childVt, childName);
        ++namedWritten;
    }
    fputs("]\n}\n", f);
    fclose(f);
    free(cmcBytes);

    lstrcpynA(g_partyAiLastFile, path, sizeof(g_partyAiLastFile));
    sprintf_s(g_partyAiStatus, sizeof(g_partyAiStatus),
        "AI bridge %03d: %d live objects", g_partyAiSeq, written);
    logFile << "PartyRecon: pawn AI bridge " << g_partyAiSeq
            << " candidates=" << g_nPawnAi << " written=" << written
            << " cmcInfo=0x" << std::hex << cmcInfo << std::dec
            << " file=" << g_partyAiLastFile << std::endl;
}

static void PartyTraceStop()
{
    if (!g_partyTrace) return;
    fputs("# stopped\n", g_partyTrace);
    fclose(g_partyTrace);
    g_partyTrace = nullptr;
    sprintf_s(g_partyStatus, sizeof(g_partyStatus),
        "live trace stopped: ddda_party_live_%03d.csv", g_partyTraceSeq);
    logFile << "PartyRecon: live trace stopped file=" << g_partyTraceFile << std::endl;
}

static void PartyTraceStart()
{
    PartyTraceStop();
    if (!PartyCandidatesStillValid()
        || !PartyRoleBody("Arisen") || !PartyRoleBody("Main Pawn")) {
        lstrcpynA(g_partyStatus, "find both bodies before live trace", sizeof(g_partyStatus));
        return;
    }

    ++g_partyTraceSeq;
    char fileName[64];
    sprintf_s(fileName, sizeof(fileName), "ddda_party_live_%03d.csv", g_partyTraceSeq);
    const char* path = ModPaths::File(fileName, 5);
    lstrcpynA(g_partyTraceFile, path ? path : fileName, sizeof(g_partyTraceFile));
    if (fopen_s(&g_partyTrace, g_partyTraceFile, "w") != 0 || !g_partyTrace) {
        sprintf_s(g_partyStatus, sizeof(g_partyStatus), "cannot write %s", fileName);
        return;
    }

    fprintf(g_partyTrace, "# build,%s\n", MOD_BUILD_TAG);
    fprintf(g_partyTrace, "# pBase windows,player/pawn record +0x960 through +0x9DC (raw dwords)\n");
    for (int i = 0; i < g_nPartyRuntime; ++i)
        fprintf(g_partyTrace, "# runtimeProbe,%d,%s,0x%08X,0x%08X\n",
            i, g_partyRuntime[i].name,
            (unsigned)g_partyRuntime[i].ptr, (unsigned)g_partyRuntime[i].vt);

    fputs("ms,arisenAction,arisenActionPtr,arisen_2DD4,arisen_4AE8,arisen_32D8,arisen_1C94,arisen_4B14,"
          "pawnAction,pawnActionPtr,pawn_2DD4,pawn_4AE8,pawn_32D8,pawn_1C94,pawn_4B14", g_partyTrace);
    for (int r = 0; r < 32; ++r) fprintf(g_partyTrace, ",playerRec_%03X", 0x960 + r * 4);
    for (int r = 0; r < 32; ++r) fprintf(g_partyTrace, ",pawnRec_%03X", 0x960 + r * 4);
    for (int p = 0; p < g_nPartyRuntime; ++p)
        for (int r = 0; r < kPartyRuntimeProbeBytes / 4; ++r)
            fprintf(g_partyTrace, ",probe%d_%02X", p, r * 4);
    fputc('\n', g_partyTrace);
    fflush(g_partyTrace);

    g_partyTraceStartMs = MsNow();
    sprintf_s(g_partyStatus, sizeof(g_partyStatus),
        "LIVE TRACE running: %s ('-' stops)", fileName);
    logFile << "PartyRecon: live trace started file=" << g_partyTraceFile
            << " runtimeProbes=" << g_nPartyRuntime << std::endl;
}

static void PartyTraceBody(FILE* f, PartyBodyDump* P)
{
    static const uint32_t offs[] = { 0x2DD4, 0x4AE8, 0x32D8, 0x1C94, 0x4B14 };
    char actName[48] = "?";
    uintptr_t act = 0;
    if (P && RdPtr((void*)(P->ptr + 0x2DC8), &act) && act)
        NameOfLiveObject(act, actName, sizeof(actName));
    fprintf(f, ",%s,0x%08X", actName[0] ? actName : "?", (unsigned)act);
    for (int i = 0; i < (int)(sizeof(offs) / sizeof(offs[0])); ++i) {
        uint32_t v = 0xFFFFFFFFu;
        if (P) Rd((void*)(P->ptr + offs[i]), &v, 4);
        fprintf(f, ",%u", v);
    }
}

static void PartyTraceRecord(FILE* f, uintptr_t record)
{
    uint32_t raw[32];
    memset(raw, 0xFF, sizeof(raw));
    if (record) Rd((void*)(record + 0x960), raw, sizeof(raw));
    for (int i = 0; i < 32; ++i) fprintf(f, ",0x%08X", raw[i]);
}

static void PartyTraceTick()
{
    if (!g_partyTrace) return;
    static DWORD last = 0;
    DWORD now = MsNow();
    if (last && now - last < 100) return;
    last = now;

    PartyBodyDump* arisen = PartyRoleBody("Arisen");
    PartyBodyDump* pawn = PartyRoleBody("Main Pawn");
    fprintf(g_partyTrace, "%u", now - g_partyTraceStartMs);
    PartyTraceBody(g_partyTrace, arisen);
    PartyTraceBody(g_partyTrace, pawn);

    uintptr_t playerRecord = (pBase && *pBase) ? (uintptr_t)(*pBase + 0xA7000) : 0;
    PartyTraceRecord(g_partyTrace, playerRecord);
    PartyTraceRecord(g_partyTrace, playerRecord ? playerRecord + 0x7F0 : 0);

    for (int p = 0; p < g_nPartyRuntime; ++p) {
        uint32_t raw[kPartyRuntimeProbeBytes / 4];
        memset(raw, 0xFF, sizeof(raw));
        Rd((void*)g_partyRuntime[p].ptr, raw, sizeof(raw));
        for (int r = 0; r < kPartyRuntimeProbeBytes / 4; ++r)
            fprintf(g_partyTrace, ",0x%08X", raw[r]);
    }
    fputc('\n', g_partyTrace);
    fflush(g_partyTrace);
}

static int PartyCollectIntentLinks(uintptr_t planner, uint32_t code,
    uintptr_t* links, int cap)
{
    if (!planner || code >= 91u || !links || cap <= 0) return 0;
    BYTE plan[0x110] = {};
    if (!Rd((void*)(planner + 0x190u + code * 0x110u), plan, sizeof(plan))) return 0;
    static const uint32_t descs[] = { 0x04u, 0x38u, 0x4Cu, 0x68u };
    int countOut = 0;
    for (int d = 0; d < (int)(sizeof(descs) / sizeof(descs[0])); ++d) {
        const uint32_t desc = descs[d];
        const uint32_t count = *(uint32_t*)(plan + desc + 0x04u);
        const uint32_t capacity = *(uint32_t*)(plan + desc + 0x08u);
        const uintptr_t arrayPtr = *(uint32_t*)(plan + desc + 0x10u);
        if (!count || count > 16u || capacity > 16u || count > capacity
            || !LooksHeap(arrayPtr))
            continue;
        uintptr_t nodes[16] = {};
        if (!Rd((void*)arrayPtr, nodes, count * sizeof(uintptr_t))) continue;
        for (uint32_t n = 0; n < count && countOut < cap; ++n) {
            char name[64] = {};
            if (!NameOfLiveObject(nodes[n], name, sizeof(name))
                || strcmp(name, "cAIGoalPlanning::cGoalPlanningNode"))
                continue;
            uintptr_t link = 0;
            if (!RdPtr((void*)(nodes[n] + 0x04), &link)) continue;
            bool duplicate = false;
            for (int i = 0; i < countOut; ++i)
                if (links[i] == link) { duplicate = true; break; }
            if (!duplicate) links[countOut++] = link;
        }
    }
    return countOut;
}

static void PartyIntentTraceStop(const char* reason)
{
    if (!g_intentTrace) return;
    fprintf(g_intentTrace, "# stopped,%s\n", reason ? reason : "manual");
    fclose(g_intentTrace);
    g_intentTrace = nullptr;
    g_intentLiveValid = false;
    logFile << "PartyRecon: intent trace stopped reason="
            << (reason ? reason : "manual") << std::endl;
}

static void PartyIntentTraceStart()
{
    PartyIntentTraceStop("restart");
    ++g_intentTraceSeq;
    char fileName[72] = {};
    sprintf_s(fileName, sizeof(fileName),
        "ddda_pawn_intent_trace_%03d.csv", g_intentTraceSeq);
    const char* path = ModPaths::File(fileName, 3);
    if (fopen_s(&g_intentTrace, path, "w") != 0 || !g_intentTrace) return;
    lstrcpynA(g_intentTraceFile, path, sizeof(g_intentTraceFile));
    g_intentTraceStartMs = MsNow();
    g_intentTraceLastMs = 0;
    g_intentTraceLastCode = 0xFFFFFFFEu;
    g_intentTraceLastActionVt = 0;
    g_intentTraceLastPacked = 0xFFFFFFFFu;
    g_intentTraceLastTarget = 0;
    g_intentLiveValid = false;
    fprintf(g_intentTrace, "# build,%s\n", MOD_BUILD_TAG);
    fputs("ms,priorityCode,intentName,intentMapped,actionName,actionPtr,packedCode,targetPtr,targetName,targetMode,nodeLinks\n",
        g_intentTrace);
    fflush(g_intentTrace);
    logFile << "PartyRecon: intent trace started file=" << path << std::endl;
}

static void PartyIntentTraceTick()
{
    if (!g_intentTrace) return;
    PartyBodyDump* pawn = PartyRoleBody("Main Pawn");
    if (!pawn || !pawn->ptr) return;
    uintptr_t bodyVt = 0;
    if (!RdPtr((void*)pawn->ptr, &bodyVt) || bodyVt != pawn->vt) {
        PartyIntentTraceStop("pawn lost");
        return;
    }

    uintptr_t action = 0;
    uintptr_t actionVt = 0;
    uint32_t packed = 0xFFFFFFFFu;
    uintptr_t target = 0;
    uintptr_t aiCtrl = 0;
    uintptr_t planner = 0;
    uint32_t code = 0xFFFFFFFFu;
    RdPtr((void*)(pawn->ptr + 0x2DC8), &action);
    if (action) RdPtr((void*)action, &actionVt);
    Rd((void*)(pawn->ptr + 0x2DD4), &packed, 4);
    RdPtr((void*)(pawn->ptr + 0x2EB8), &target);
    RdPtr((void*)(pawn->ptr + 0x2E64), &aiCtrl);
    if (aiCtrl) RdPtr((void*)(aiCtrl + 0x68), &planner);
    if (planner) Rd((void*)(planner + 0x17C), &code, 4);

    DWORD now = MsNow();
    const bool changed = code != g_intentTraceLastCode
        || actionVt != g_intentTraceLastActionVt
        || packed != g_intentTraceLastPacked
        || target != g_intentTraceLastTarget;
    if (!changed && g_intentTraceLastMs && now - g_intentTraceLastMs < 1000u)
        return;

    char actionName[64] = {};
    char targetName[64] = {};
    if (action) NameOfLiveObject(action, actionName, sizeof(actionName));
    if (target) NameOfLiveObject(target, targetName, sizeof(targetName));
    g_intentLiveValid = true;
    g_intentLiveCode = code;
    lstrcpynA(g_intentLiveAction, actionName, sizeof(g_intentLiveAction));
    g_intentLiveTarget = target;
    lstrcpynA(g_intentLiveTargetName, targetName, sizeof(g_intentLiveTargetName));
    uintptr_t links[64] = {};
    const int nLinks = PartyCollectIntentLinks(planner, code, links, 64);

    fprintf(g_intentTrace,
        "%u,%u,%s,%u,%s,0x%08X,0x%08X,0x%08X,%s,%s,",
        now - g_intentTraceStartMs, code, PawnPriorityIntentName(code),
        PawnPriorityIntentMapped(code) ? 1u : 0u, actionName,
        (unsigned)action, packed, (unsigned)target, targetName,
        !target ? "none" : (code == 0xFFFFFFFFu
            ? "retained_no_priority" : "planner_target"));
    for (int i = 0; i < nLinks; ++i)
        fprintf(g_intentTrace, "%s0x%08X", i ? ";" : "", (unsigned)links[i]);
    fputc('\n', g_intentTrace);
    fflush(g_intentTrace);
    g_intentTraceLastMs = now;
    g_intentTraceLastCode = code;
    g_intentTraceLastActionVt = actionVt;
    g_intentTraceLastPacked = packed;
    g_intentTraceLastTarget = target;
}

// ============ Build 61 — прицельная охота за code 4 и code 66 ============
// Эти два кода статически «unmatched» (PlanCtrl пуст без активации). Ловим
// их в рантайме: при selected code == 4 или 66 снимаем exact action, target,
// packed и PlanCtrl links → InterfaceID → имя GOAP-ресурса.


// Zip 36 — probe the decision/motion objects hanging off the actor.
// Confirmed live offsets (dumps 14.08): +0x2DC0 cActBank, +0x2DC8 current Act,
// +0x2E64 cAICtrl (704B). We snapshot their headers so weights and rate fields
// can be located by diffing calm vs. combat.
struct SidecarDump {
    uint32_t    off;
    uintptr_t   ptr;
    char        name[40];
    BYTE        hex[384];
    bool        ok;
};
static SidecarDump g_side[8];
static int         g_nSide = 0;

static void ProbeSidecars(uintptr_t body)
{
    g_nSide = 0;
    static const uint32_t kOffs[] = { 0x2DC0, 0x2DC8, 0x2E64, 0x29AC, 0x322C, 0x2710, 0 };
    for (int i = 0; kOffs[i] && g_nSide < 8; ++i) {
        uintptr_t p = 0;
        if (!RdPtr((void*)(body + kOffs[i]), &p) || !LooksHeap(p)) continue;
        SidecarDump& S = g_side[g_nSide];
        memset(&S, 0, sizeof(S));
        S.off = kOffs[i];
        S.ptr = p;
        NameOfLiveObject(p, S.name, sizeof(S.name));
        S.ok = Rd((void*)p, S.hex, sizeof(S.hex));
        g_nSide++;
    }
}

static void DumpActors()
{
    uintptr_t seed[32];
    int ns = 0;
    for (int i = 0; i < g_nLives && ns < 32; ++i)
        seed[ns++] = g_lives[i].ptr;
    DumpActorsFrom(seed, ns);
    PublishWorldFromActors();
}

// --- доступ для модулей поведения -------------------------------------------
// Оффсеты объектов внутри тела ПЛАВАЮТ (слот +0x2B98: uPlayer в calm,
// uCmc в aggro). Поэтому наружу отдаём тело и имя-резолвер, а не оффсеты:
// потребитель обязан проверять имя класса, а не доверять смещению.

uintptr_t DevTools::FirstEnemyBody()
{
    for (int i = 0; i < g_nAct; ++i) {
        if (!g_act[i].ptr) continue;
        const char* k = g_act[i].kind;
        if (!k) continue;
        // uEm* — враг. uNpc/uPl — не он.
        if (KindIsCreature(k)) return g_act[i].ptr;
    }
    return 0;
}

// Трупы отдельно — для диагностики и для будущего "поле боя после драки".
int DevTools::DeadCount()
{
    int n = 0;
    for (int i = 0; i < g_nAct; ++i) {
        if (!g_act[i].ptr || !g_act[i].isDead) continue;
        // Труп зайца — тоже труп: считаем всех существ, иначе цифра
        // не сойдётся с тем, что игрок видит на земле.
        if (KindIsCreature(g_act[i].kind)) ++n;
    }
    return n;
}

// Состояние существа по индексу в общем списке (включая мёртвых).
// Нужно диагностике: показать, что труп опознан, а не потерян.
const char* DevTools::EnemyActAt(int idx, bool* deadOut)
{
    if (idx < 0 || idx >= g_nAct) return nullptr;
    if (deadOut) *deadOut = g_act[idx].isDead;
    return g_act[idx].liveAct[0] ? g_act[idx].liveAct : nullptr;
}

// ============ Build 57.1 — динамический Guardian-фикс ============
// Снимает доказанный штраф -3 с code 54 (WpnDaggerAtk) rule 0, транзакционно.
// Кортеж подтверждён дампом Build 57 (GuardianAudit):
//   code=54 tuple{sensor=1, category=0, objectId=0, extra=1} personality=5
//     rule[0] AddS32=-3 AddF32=0 break=0 checks=1


static void DumpFactoryHeads()
{
    g_nFact = 0;
    static const char* kFail[] = {
        "uEm0100", "uEm0101", "uEm0200", "uEm0500", "uEm0900", "uPlayer", "uNpc",
        "uHumanEnemy", "sEnemyManager", "uCharacterBase", "uPawnIntel", nullptr
    };
    for (int i = 0; kFail[i] && g_nFact < 16; ++i) {
        const TypeAtlas::Info* t = TypeAtlas::FindByName(kFail[i]);
        if (!t || !g_base) continue;
        FactDump& d = g_fact[g_nFact];
        memset(&d, 0, sizeof(d));
        d.name = kFail[i];
        d.va = g_base + t->factoryVtRVA;
        d.ok = Rd((void*)d.va, d.hex, 32);
        if (d.ok) d.slot0 = *(uint32_t*)d.hex;
        RescueFactory(kFail[i], &d);
        g_nFact++;
    }
}

static void HopHeapMgrs()
{
    for (int i = 0; i < g_nMgrs; ++i) {
        BYTE buf[256];
        if (!Rd((void*)g_mgrs[i].ptr, buf, sizeof(buf))) continue;
        for (uint32_t o = 4; o < sizeof(buf); o += 4) {
            uintptr_t v = *(uint32_t*)(buf + o);
            if (!LooksHeap(v)) continue;
            Named n = NameOf(v);
            if (n.name) {
                AddDPtr(g_mgrs[i].ptr, o, v, g_mgrs[i].name, n);
                AddLive(v, n);
            }
            uintptr_t vt = 0;
            if (RdPtr((void*)v, &vt) && InImage(vt))
                TryGidScout(v, vt, LooksLikeVtable(vt));
        }
    }
}

// Walk MEM_PRIVATE. 8-byte aligned. Census every LooksLikeVtable.
// Named hunt uses factory vt AND derived instance vt. Managers capped at 3/type
// so sUnitSearchManager cannot hide everything else.
static void HeapHunt()
{
    g_huntMs = 0;
    g_huntRegions = 0;
    g_huntBytes = 0;
    g_nMgrs = 0;
    g_nCen = 0;
    if (!g_base) return;
    if (!g_nExec) InitSections();
    BuildHuntTable();

    DWORD t0 = MsNow();
    uintptr_t addr = 0x00010000;
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    const uintptr_t img0 = g_base;
    const uintptr_t img1 = ImageEnd();

    while (addr < 0x7FFF0000u) {
        SIZE_T got = VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi));
        if (!got) break;
        uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (next <= addr) break;

        DWORD prot = mbi.Protect & 0xFF;
        bool readable = prot == PAGE_READONLY || prot == PAGE_READWRITE
                     || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ
                     || prot == PAGE_EXECUTE_READWRITE;
        bool skip = mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE
                 || !readable || (mbi.Protect & PAGE_GUARD);
        if (skip) { addr = next; continue; }

        uintptr_t start = (uintptr_t)mbi.BaseAddress;
        uintptr_t end = next;
        if (start < img1 && end > img0) { addr = next; continue; }

        uint32_t bytes = (uint32_t)(end - start);
        g_huntRegions++;
        g_huntBytes += bytes;

        __try {
            uint32_t* p32 = (uint32_t*)start;
            uint32_t n = bytes / 4;
            for (uint32_t i = 0; i < n; ++i) {
                uintptr_t obj = start + (uintptr_t)i * 4;
                if (obj & 7) continue; // 8-byte align (16 was too strict)
                uint32_t val = p32[i];
                if ((uintptr_t)val < img0 || (uintptr_t)val >= img1) continue;
                const bool llv = LooksLikeVtable((uintptr_t)val);
                if (!llv) continue;
                TryGidScout(obj, (uintptr_t)val, true);
                CensusAdd((uintptr_t)val, obj);

                const HuntKey* hk = HuntLookup((uintptr_t)val);
                if (!hk) continue;
                BYTE probe[0x30];
                if (!Rd((void*)obj, probe, sizeof(probe))) continue;
                if (IsRepeatTag(*(uint32_t*)probe)) continue;

                Named nm{};
                nm.name = hk->name;
                nm.gid = hk->gid;
                nm.kind = hk->kind;
                nm.rva = (uint32_t)(hk->va - g_base);
                if (hk->kind == WK_CHAR) {
                    AddLive(obj, nm);
                } else if (CountMgrName(hk->name) < 3) {
                    AddHeapMgr(hk->name, obj);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        addr = next;
    }
    g_huntMs = MsNow() - t0;
}

static void NoteHolder(const char* where, uintptr_t base, uint32_t off, uintptr_t live, const char* name)
{
    if (g_nHold >= 48) return;
    uintptr_t slot = base + off;
    for (int i = 0; i < g_nHold; ++i)
        if (g_hold[i].slot == slot && g_hold[i].live == live) return;
    g_hold[g_nHold].where = where;
    g_hold[g_nHold].name = name ? name : "?";
    g_hold[g_nHold].slot = slot;
    g_hold[g_nHold].live = live;
    g_hold[g_nHold].off = off;
    g_nHold++;
}

static void ScanForLive(uintptr_t base, uint32_t bytes, const char* where)
{
    if (!base || bytes < 4 || !g_nLives) return;
    if (bytes > 0x200000) bytes = 0x200000;
    __try {
        uint32_t* p = (uint32_t*)base;
        uint32_t n = bytes / 4;
        for (uint32_t i = 0; i < n && g_nHold < 48; ++i) {
            uintptr_t v = p[i];
            if (!v) continue;
            for (int L = 0; L < g_nLives; ++L) {
                if (g_lives[L].ptr == v)
                    NoteHolder(where, base, i * 4, v, g_lives[L].name);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void FindHolders()
{
    g_nHold = 0;
    if (!g_nLives) return;
    if (g_sUnit && g_sUnitInImg)
        ScanForLive(g_sUnit, g_sUnitInImg, "sUnit");
    if (g_sSet && g_sSetInImg)
        ScanForLive(g_sSet, g_sSetInImg, "sSet");
    if (g_pWorldObj)
        ScanForLive(g_pWorldObj, 512, "pWorld");
    if (pBase && *pBase)
        ScanForLive((uintptr_t)*pBase, 0xC0000, "pBase");
    for (int k = 0; k < g_nScans; ++k) {
        if (!g_scans[k].t || strcmp(g_scans[k].t->name, "sPlayerManager")) continue;
        for (int i = 0; i < g_scans[k].nDataObj; ++i)
            ScanForLive(g_scans[k].dataObj[i], 2488, "sPlayerManager");
    }
    // writable image sections — who in .data holds a live ptr
    auto dos = (IMAGE_DOS_HEADER*)g_base;
    auto nt  = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    const int nsec = nt->FileHeader.NumberOfSections;
    __try {
        for (int si = 0; si < nsec && g_nHold < 48; ++si) {
            if (!(sec[si].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            uintptr_t s0 = g_base + sec[si].VirtualAddress;
            uint32_t sz = sec[si].Misc.VirtualSize;
            if (!sz) continue;
            if (sec[si].VirtualAddress + sz > g_imageSize)
                sz = g_imageSize - sec[si].VirtualAddress;
            ScanForLive(s0, sz, ".data");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void DumpWindow(uint32_t off)
{
    if (g_nWin >= 4 || !g_sUnit) return;
    WinDump& w = g_win[g_nWin];
    memset(&w, 0, sizeof(w));
    w.off = off;
    w.ok = Rd((void*)(g_sUnit + off), w.hex, sizeof(w.hex));
    g_nWin++;
}

static void WriteDumpJson()
{
    FILE* f = nullptr;
    if (fopen_s(&f, "ddda_sunit_dump.json", "w") != 0 || !f) return;

    fprintf(f,
        "{\n  \"moduleBase\":\"0x%08X\",\n  \"imageEnd\":\"0x%08X\",\n  \"dumpMs\":%u,\n"
        "  \"note\":\"factory vt != instance vt for u*. derived[] from create_*. census[] = all LooksLikeVtable on heap. FactoryPointer slots are always empty.\",\n"
        "  \"pWorld\":{\"slot\":\"0x%08X\",\"obj\":\"0x%08X\",\"name\":\"%s\"},\n"
        "  \"sUnit\":{\"addr\":\"0x%08X\",\"claimed\":1700720,\"bytesInImage\":%u,\"fitsInImage\":%s,\"identify\":\"%s\"},\n"
        "  \"sSetManager\":{\"addr\":\"0x%08X\",\"claimed\":112976,\"bytesInImage\":%u},\n"
        "  \"counts\":{\"moveLine\":%d,\"unitGroup\":%d,\"layout\":%d,\"live\":%d,\"headerPtrs\":%d,\"hunts\":%d,\"heapMgrs\":%d,\"holders\":%d,\"derived\":%d,\"census\":%d,\"gids\":%d,\"facts\":%d,\"dti\":%d,\"links\":%d,\"leads\":%d,\"tree\":%d,\"near\":%d,\"writes\":%d,\"nodes\":%d,\"ctors\":%d,\"actors\":%d},\n"
        "  \"embed\":{\"bytes\":%u,\"moveLine\":%d,\"unitGroup\":%d,\"layout\":%d},\n"
        "  \"hunt\":{\"ms\":%u,\"regions\":%d,\"bytes\":%u,\"keys\":%d},\n",
        (unsigned)g_base, (unsigned)ImageEnd(), g_dumpMs,
        (unsigned)(pWorld ? (uintptr_t)pWorld : 0),
        (unsigned)g_pWorldObj, g_pWorldName ? g_pWorldName : "",
        (unsigned)g_sUnit, g_sUnitInImg, g_sUnitFits ? "true" : "false",
        g_sUnitId ? g_sUnitId : "",
        (unsigned)g_sSet, g_sSetInImg,
        g_nMoveLine, g_nUnitGroup, g_nLayout, g_nLives, g_nDptrs, g_nHunts, g_nMgrs, g_nHold, g_nDer, g_nCen, g_nGid, g_nFact, g_nDti, g_nDlink, g_nLead, g_nTree, g_nNear, g_nWr, g_nNode, g_nCtor, g_nAct,
        g_embedBytes, g_nMoveLine, g_nUnitGroup, g_nLayout,
        g_huntMs, g_huntRegions, g_huntBytes, g_nHvt);

    {
        WorldReport wr = CombatBus::Instance().LastWorld();
        fprintf(f, "  \"world\":{\"count\":%d,\"goblins\":%d,\"cat\":%d,\"ms\":%u},\n",
            wr.count, wr.goblinCount, wr.dominantCategory, wr.timestampMs);
    }

    fputs("  \"headerHex\":\"", f);
    if (g_hdrOk) {
        for (int i = 0; i < (int)sizeof(g_hdr); ++i)
            fprintf(f, "%02X%s", g_hdr[i], (i + 1) == (int)sizeof(g_hdr) ? "" : " ");
    }
    fputs("\",\n  \"pWorldHex\":\"", f);
    if (g_worldHdrOk) {
        for (int i = 0; i < (int)sizeof(g_worldHdr); ++i)
            fprintf(f, "%02X%s", g_worldHdr[i], (i + 1) == (int)sizeof(g_worldHdr) ? "" : " ");
    }
    fputs("\",\n  \"setHeaderHex\":\"", f);
    if (g_setHdrOk) {
        for (int i = 0; i < (int)sizeof(g_setHdr); ++i)
            fprintf(f, "%02X%s", g_setHdr[i], (i + 1) == (int)sizeof(g_setHdr) ? "" : " ");
    }
    fputs("\",\n  \"windows\":[\n", f);
    for (int i = 0; i < g_nWin; ++i) {
        fprintf(f, "    %s{\"off\":\"0x%X\",\"hex\":\"", i ? "," : " ", g_win[i].off);
        if (g_win[i].ok) {
            for (int b = 0; b < 64; ++b)
                fprintf(f, "%02X%s", g_win[i].hex[b], b == 63 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"histogram\":[", f);
    for (int i = 0; i < g_nHist; ++i)
        fprintf(f, "%s{\"name\":\"%s\",\"n\":%d}", i ? "," : "", g_hist[i].name, g_hist[i].n);
    fputs("],\n  \"ptrs\":[\n", f);
    for (int i = 0; i < g_nDptrs; ++i) {
        fprintf(f,
            "    %s{\"from\":\"%s\",\"off\":\"0x%X\",\"ptr\":\"0x%08X\",\"name\":\"%s\"}\n",
            i ? "," : " ", g_dptrs[i].fromName, g_dptrs[i].off,
            (unsigned)g_dptrs[i].ptr, g_dptrs[i].name);
    }
    fputs("  ],\n  \"hunts\":[", f);
    for (int i = 0; i < g_nHunts; ++i)
        fprintf(f, "%s{\"name\":\"%s\",\"slot\":\"0x%08X\",\"instance\":\"0x%08X\"}",
            i ? "," : "", g_hunts[i].name, (unsigned)g_hunts[i].slot, (unsigned)g_hunts[i].instance);
    fputs("],\n  \"heapMgrs\":[\n", f);
    for (int i = 0; i < g_nMgrs; ++i) {
        fprintf(f, "    %s{\"name\":\"%s\",\"ptr\":\"0x%08X\",\"head\":\"",
            i ? "," : " ", g_mgrs[i].name, (unsigned)g_mgrs[i].ptr);
        if (g_mgrs[i].headOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_mgrs[i].head[b], b == 31 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"holders\":[\n", f);
    for (int i = 0; i < g_nHold; ++i)
        fprintf(f, "    %s{\"where\":\"%s\",\"off\":\"0x%X\",\"slot\":\"0x%08X\",\"live\":\"0x%08X\",\"name\":\"%s\"}\n",
            i ? "," : " ", g_hold[i].where, g_hold[i].off,
            (unsigned)g_hold[i].slot, (unsigned)g_hold[i].live, g_hold[i].name);
    fputs("  ],\n  \"derived\":[\n", f);
    for (int i = 0; i < g_nDer; ++i) {
        fprintf(f, "    %s{\"name\":\"%s\",\"factory\":\"0x%08X\",\"create\":\"0x%08X\",\"inst\":\"0x%08X\",\"same\":%s,\"shared\":%s}\n",
            i ? "," : " ", g_der[i].name,
            (unsigned)g_der[i].factory, (unsigned)g_der[i].create, (unsigned)g_der[i].inst,
            (g_der[i].inst && g_der[i].inst == g_der[i].factory) ? "true" : "false",
            g_der[i].shared ? "true" : "false");
    }
    fputs("  ],\n  \"census\":[\n", f);
    // emit highest counts first (selection sort of indices, small N)
    int order[96];
    int nc = g_nCen < 96 ? g_nCen : 96;
    for (int i = 0; i < nc; ++i) order[i] = i;
    for (int i = 0; i < nc; ++i) {
        int best = i;
        for (int j = i + 1; j < nc; ++j)
            if (g_cen[order[j]].n > g_cen[order[best]].n) best = j;
        int tmp = order[i]; order[i] = order[best]; order[best] = tmp;
    }
    for (int i = 0; i < nc; ++i) {
        const Census& c = g_cen[order[i]];
        const char* nm = NameVt(c.vt);
        fprintf(f, "    %s{\"vt\":\"0x%08X\",\"rva\":\"0x%X\",\"n\":%d,\"name\":\"%s\",\"s0\":\"0x%08X\"}\n",
            i ? "," : " ", (unsigned)c.vt,
            g_base ? (unsigned)(c.vt - g_base) : 0,
            c.n, nm ? nm : "", (unsigned)c.sample[0]);
    }
    fputs("  ],\n  \"facts\":[\n", f);
    for (int i = 0; i < g_nFact; ++i) {
        fprintf(f,
            "    %s{\"name\":\"%s\",\"got\":\"%s\",\"va\":\"0x%08X\",\"slot0\":\"0x%08X\","
            "\"caller\":\"0x%08X\",\"rescued\":\"0x%08X\","
            "\"ecx\":\"0x%08X\",\"ecx0\":\"0x%08X\","
            "\"namePtr\":\"0x%08X\",\"pushVt\":\"0x%08X\","
            "\"sizeImm\":\"0x%08X\",\"c7dest\":\"0x%08X\",\"c7imm\":\"0x%08X\",\"hex\":\"",
            i ? "," : " ", g_fact[i].name ? g_fact[i].name : "",
            g_fact[i].gotName, (unsigned)g_fact[i].va, (unsigned)g_fact[i].slot0,
            (unsigned)g_fact[i].caller, (unsigned)g_fact[i].rescued,
            (unsigned)g_fact[i].ecx, (unsigned)g_fact[i].ecx0,
            (unsigned)g_fact[i].namePtr, (unsigned)g_fact[i].pushVt,
            (unsigned)g_fact[i].sizeImm, (unsigned)g_fact[i].c7dest, (unsigned)g_fact[i].c7imm);
        if (g_fact[i].ok) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_fact[i].hex[b], b == 31 ? "" : " ");
        }
        fputs("\",\"callHex\":\"", f);
        if (g_fact[i].callOk) {
            for (int b = 0; b < 48; ++b)
                fprintf(f, "%02X%s", g_fact[i].callHex[b], b == 47 ? "" : " ");
        }
        fputs("\",\"afterHex\":\"", f);
        if (g_fact[i].afterOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_fact[i].afterHex[b], b == 31 ? "" : " ");
        }
        fputs("\",\"ecxHex\":\"", f);
        if (g_fact[i].ecxOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_fact[i].ecxHex[b], b == 31 ? "" : " ");
        }
        fputs("\",\"nameHex\":\"", f);
        if (g_fact[i].nameOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_fact[i].nameHex[b], b == 31 ? "" : " ");
        }
        fputs("\",\"pushHex\":\"", f);
        if (g_fact[i].pushOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_fact[i].pushHex[b], b == 31 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"dti\":[\n", f);
    for (int i = 0; i < g_nDti; ++i) {
        fprintf(f, "    %s{\"name\":\"%s\",\"dti\":\"0x%08X\",\"vt\":\"0x%08X\",\"namePtr\":\"0x%08X\",\"str\":\"0x%08X\",\"size\":%u,\"sizeRaw\":\"0x%08X\",\"typeId\":%u,\"create0\":\"0x%08X\",\"meth4\":\"0x%08X\",\"meth8\":\"0x%08X\",\"foundCreate\":\"0x%08X\",\"foundInst\":\"0x%08X\",\"foundSlot\":%d,\"foundFact\":\"0x%08X\",\"foundOff\":\"0x%X\",\"head\":\"",
            i ? "," : " ", g_dti[i].want ? g_dti[i].want : "",
            (unsigned)g_dti[i].dti, (unsigned)g_dti[i].vt,
            (unsigned)g_dti[i].namePtr, (unsigned)g_dti[i].strVa,
            g_dti[i].size, (unsigned)g_dti[i].sizeRaw, g_dti[i].typeId,
            (unsigned)g_dti[i].create0, (unsigned)g_dti[i].meth4, (unsigned)g_dti[i].meth8,
            (unsigned)g_dti[i].foundCreate, (unsigned)g_dti[i].foundInst, g_dti[i].foundSlot,
            (unsigned)g_dti[i].foundFact, g_dti[i].foundOff);
        if (g_dti[i].headOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_dti[i].head[b], b == 31 ? "" : " ");
        }
        fputs("\",\"body\":\"", f);
        if (g_dti[i].bodyOk) {
            for (int b = 0; b < 128; ++b)
                fprintf(f, "%02X%s", g_dti[i].body[b], b == 127 ? "" : " ");
        }
        fputs("\",\"vtHex\":\"", f);
        if (g_dti[i].vtOk) {
            for (int b = 0; b < 64; ++b)
                fprintf(f, "%02X%s", g_dti[i].vtHex[b], b == 63 ? "" : " ");
        }
        fputs("\",\"nCands\":", f);
        fprintf(f, "%d,\"cands\":[", g_dti[i].nCands);
        for (int c = 0; c < g_dti[i].nCands; ++c)
            fprintf(f, "%s{\"slot\":%d,\"fn\":\"0x%08X\",\"inst\":\"0x%08X\",\"callee\":%d}",
                c ? "," : "", g_dti[i].cands[c].slot,
                (unsigned)g_dti[i].cands[c].func, (unsigned)g_dti[i].cands[c].inst,
                g_dti[i].cands[c].callee);
        fputs("]}\n", f);
    }
    fputs("  ],\n  \"links\":[\n", f);
    for (int i = 0; i < g_nDlink; ++i) {
        fprintf(f, "    %s{\"owner\":\"%s\",\"off\":\"0x%X\",\"ptr\":\"0x%08X\",\"name\":\"%s\",\"head\":\"",
            i ? "," : " ", g_dlink[i].owner ? g_dlink[i].owner : "",
            g_dlink[i].off, (unsigned)g_dlink[i].ptr,
            g_dlink[i].name[0] ? g_dlink[i].name : "");
        if (g_dlink[i].headOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_dlink[i].head[b], b == 31 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"leads\":[\n", f);
    for (int i = 0; i < g_nLead; ++i) {
        fprintf(f, "    %s{\"from\":\"%s\",\"off\":\"0x%X\",\"src\":\"0x%08X\",\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\",\"name\":\"%s\",\"gid\":\"0x%02X\",\"fat\":%s,\"head\":\"",
            i ? "," : " ", g_lead[i].fromName ? g_lead[i].fromName : "",
            g_lead[i].off, (unsigned)g_lead[i].from, (unsigned)g_lead[i].ptr,
            (unsigned)g_lead[i].vt, g_lead[i].name ? g_lead[i].name : "",
            g_lead[i].gid, g_lead[i].fat ? "true" : "false");
        if (g_lead[i].headOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_lead[i].head[b], b == 31 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"tree\":[\n", f);
    for (int i = 0; i < g_nTree; ++i) {
        fprintf(f, "    %s{\"name\":\"%s\",\"dti\":\"0x%08X\",\"parent\":\"0x%08X\",\"child\":\"0x%08X\",\"next\":\"0x%08X\",\"size\":%u,\"typeId\":%u}\n",
            i ? "," : " ", g_tree[i].name[0] ? g_tree[i].name : "",
            (unsigned)g_tree[i].dti, (unsigned)g_tree[i].parent,
            (unsigned)g_tree[i].child, (unsigned)g_tree[i].next,
            g_tree[i].size, g_tree[i].typeId);
    }
    fputs("  ],\n  \"near\":[\n", f);
    for (int i = 0; i < g_nNear; ++i)
        fprintf(f, "    %s{\"owner\":\"%s\",\"off\":%d,\"at\":\"0x%08X\",\"val\":\"0x%08X\"}\n",
            i ? "," : " ", g_near[i].owner ? g_near[i].owner : "",
            g_near[i].off, (unsigned)g_near[i].at, (unsigned)g_near[i].val);
    fputs("  ],\n  \"writes\":[\n", f);
    for (int i = 0; i < g_nWr; ++i)
        fprintf(f, "    %s{\"vt\":\"0x%08X\",\"site\":\"0x%08X\",\"n\":%d}\n",
            i ? "," : " ", (unsigned)g_wr[i].vt, (unsigned)g_wr[i].site, g_wr[i].n);
    fputs("  ],\n  \"nodes\":[\n", f);
    for (int i = 0; i < g_nNode; ++i) {
        fprintf(f, "    %s{\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\",\"body\":\"",
            i ? "," : " ", (unsigned)g_node[i].ptr, (unsigned)g_node[i].vt);
        if (g_node[i].ok) {
            for (int b = 0; b < 256; ++b)
                fprintf(f, "%02X%s", g_node[i].body[b], b == 255 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"ctors\":[\n", f);
    for (int i = 0; i < g_nCtor; ++i) {
        fprintf(f, "    %s{\"tag\":\"%s\",\"va\":\"0x%08X\",\"hex\":\"",
            i ? "," : " ", g_ctor[i].tag ? g_ctor[i].tag : "", (unsigned)g_ctor[i].va);
        if (g_ctor[i].ok) {
            for (int b = 0; b < 256; ++b)
                fprintf(f, "%02X%s", g_ctor[i].hex[b], b == 255 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"actors\":[\n", f);
    for (int i = 0; i < g_nAct; ++i) {
        fprintf(f, "    %s{\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\",\"kind\":\"%s\",\"gid\":\"0x%02X\",\"st14\":\"0x%02X\",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"next\":\"0x%08X\",\"prev\":\"0x%08X\",\"subVt\":\"0x%08X\",\"fat29\":%s,\"actOff\":\"0x%04X\",\"actPtr\":\"0x%08X\",\"actVt\":\"0x%07X\",\"act\":\"%s\",\"actCat\":\"%s\",\"actHits\":%d,\"actOff2\":\"0x%04X\",\"act2\":\"%s\",\"win5b\":\"",
            i ? "," : " ", (unsigned)g_act[i].ptr, (unsigned)g_act[i].vt,
            g_act[i].kind ? g_act[i].kind : "?",
            g_act[i].gid, g_act[i].st14, g_act[i].x, g_act[i].y, g_act[i].z,
            (unsigned)g_act[i].next, (unsigned)g_act[i].prev,
            (unsigned)g_act[i].subVt, g_act[i].fat29 ? "true" : "false",
            g_act[i].actOff, (unsigned)g_act[i].actPtr, g_act[i].actVtRva,
            g_act[i].actName ? g_act[i].actName : "-",
            g_act[i].actCat  ? g_act[i].actCat  : "-",
            g_act[i].actHits,
            g_act[i].actOff2, g_act[i].actName2 ? g_act[i].actName2 : "-");
        if (g_act[i].win5bOk) {
            for (int b = 0; b < 16; ++b)
                fprintf(f, "%02X%s", g_act[i].win5b[b], b == 15 ? "" : " ");
        }
        fputs("\",\"win60\":\"", f);
        if (g_act[i].win60Ok) {
            for (int b = 0; b < 64; ++b)
                fprintf(f, "%02X%s", g_act[i].win60[b], b == 63 ? "" : " ");
        }
        fputs("\",\"raw\":[", f);
        for (int r = 0; r < g_act[i].nRaw; ++r)
            fprintf(f, "%s{\"off\":\"0x%04X\",\"ptr\":\"0x%08X\",\"vt\":\"0x%08X\",\"name\":\"%s\"}",
                r ? "," : "", g_act[i].rawOff[r], g_act[i].rawPtr[r],
                g_act[i].rawVt[r], g_act[i].rawName[r]);
        fputs("]}\n", f);
    }
    fputs("  ],\n  \"sidecars\":[\n", f);
    for (int i = 0; i < g_nSide; ++i) {
        fprintf(f, "    %s{\"off\":\"0x%04X\",\"ptr\":\"0x%08X\",\"name\":\"%s\",\"hex\":\"",
            i ? "," : " ", g_side[i].off, (unsigned)g_side[i].ptr, g_side[i].name);
        if (g_side[i].ok) {
            for (int b = 0; b < 384; ++b)
                fprintf(f, "%02X%s", g_side[i].hex[b], b == 383 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"gids\":[\n", f);


    for (int i = 0; i < g_nGid; ++i) {
        fprintf(f, "    %s{\"gid\":\"0x%02X\",\"name\":\"%s\",\"vt\":\"0x%08X\",\"ptr\":\"0x%08X\",\"want\":%s,\"head\":\"",
            i ? "," : " ", g_gid[i].gid, g_gid[i].name ? g_gid[i].name : "",
            (unsigned)g_gid[i].vt, (unsigned)g_gid[i].ptr,
            g_gid[i].want ? "true" : "false");
        if (g_gid[i].headOk) {
            for (int b = 0; b < 32; ++b)
                fprintf(f, "%02X%s", g_gid[i].head[b], b == 31 ? "" : " ");
        }
        fputs("\"}\n", f);
    }
    fputs("  ],\n  \"live\":[\n", f);
    for (int i = 0; i < g_nLives; ++i)
        fprintf(f, "    %s{\"name\":\"%s\",\"gid\":\"0x%02X\",\"ptr\":\"0x%08X\"}\n",
            i ? "," : " ", g_lives[i].name, g_lives[i].gid, (unsigned)g_lives[i].ptr);
    fputs("  ]\n}\n", f);
    fclose(f);
    logFile << "DevTools: wrote ddda_sunit_dump.json  live=" << g_nLives
            << " moveLine=" << g_nMoveLine << " unitGroup=" << g_nUnitGroup
            << " layout=" << g_nLayout
            << " hunts=" << g_nHunts
            << " heapMgrs=" << g_nMgrs
            << " holders=" << g_nHold
            << " huntMs=" << g_huntMs << std::endl;
}

// Dump7 live[] was 7x uPawnIntel sitting 8 bytes inside sSetManager heap objects.
static void FilterFakeLives()
{
    int w = 0;
    for (int i = 0; i < g_nLives; ++i) {
        bool drop = false;
        for (int m = 0; m < g_nMgrs; ++m) {
            uintptr_t a = g_lives[i].ptr;
            uintptr_t b = g_mgrs[m].ptr;
            uintptr_t d = (a > b) ? (a - b) : (b - a);
            if (d && d < 0x100u) { drop = true; break; }
        }
        if (!drop) {
            const TypeAtlas::Info* t = TypeAtlas::FindByName(g_lives[i].name);
            if (t && t->size && t->size < 256u) drop = true;
        }
        // dump11: 3x uNpc packed at 0x3005ECxx, 0x80 apart, not 29KB actors
        if (!drop) {
            for (int j = 0; j < g_nLives; ++j) {
                if (j == i || !g_lives[j].name || !g_lives[i].name) continue;
                if (strcmp(g_lives[j].name, g_lives[i].name) != 0) continue;
                uintptr_t a = g_lives[i].ptr, b = g_lives[j].ptr;
                uintptr_t d = (a > b) ? (a - b) : (b - a);
                if (d && d < 0x100u) { drop = true; break; }
            }
        }
        if (!drop) {
            BYTE probe = 0;
            if (!Rd((void*)(g_lives[i].ptr + 0xFFF), &probe, 1)) drop = true;
        }
        if (drop) continue;
        if (w != i) g_lives[w] = g_lives[i];
        w++;
    }
    g_nLives = w;
}

static void HuntLive()
{
    if (!g_base) return;
    if (!InWorld()) {
        logFile << "DevTools: HUNT refused — not in an active save" << std::endl;
        return;
    }
    BuildWatch(); // every hunt — leftover WatchAdd from last press chained inst vts
    InitSections();
    DeriveInstanceVts();
    DumpFactoryHeads();
    ScanDti();
    EnrichDti();
    ApplyDtiToFacts();
    ScanDtiLinks();
    WalkDtiTree();
    ScanNearFactory();
    ScanTextWrites();
    DumpGoldCtors();
    for (int i = 0; i < g_nFact; ++i) {
        if (!g_fact[i].rescued) continue;
        if (g_fact[i].gotName[0] && strcmp(g_fact[i].gotName, g_fact[i].name) != 0) continue;
        if (IsForeignFactory(g_fact[i].rescued, g_fact[i].name)) continue;
        if (IsBannedInst(g_fact[i].rescued)) continue;
        int di = -1;
        for (int j = 0; j < g_nDer; ++j)
            if (g_der[j].name && !strcmp(g_der[j].name, g_fact[i].name)) { di = j; break; }
        if (di < 0) continue;
        // Keep a good factory-derived inst even if shared (uPlayer gold).
        if (g_der[di].create && g_der[di].create != kSharedCreate
            && InExec(g_der[di].create)
            && g_der[di].inst && g_der[di].inst != kSharedInst
            && !IsBannedInst(g_der[di].inst)
            && LooksLikeVtable(g_der[di].inst)
            && !NearAnyDtiVt(g_der[di].inst))
            continue;
        const DtiHit* dt = FindDti(g_fact[i].name);
        if (dt && dt->foundInst && dt->foundInst == g_fact[i].rescued
            && !IsBannedInst(dt->foundInst) && !NearAnyDtiVt(dt->foundInst)) {
            g_der[di].inst = dt->foundInst;
            g_der[di].create = dt->foundCreate;
            g_der[di].shared = false;
            uint8_t kind = WK_CHAR, gid = 0;
            for (int k = 0; k < g_nWatch; ++k)
                if (g_watch[k].name && !strcmp(g_watch[k].name, g_fact[i].name)) {
                    kind = g_watch[k].kind; gid = g_watch[k].gid; break;
                }
            if (kind == WK_CHAR)
                WatchAdd((uint32_t)(dt->foundInst - g_base), g_fact[i].name, gid, kind);
            continue;
        }
        uintptr_t create = 0, nearVt = 0;
        uintptr_t inst = DeriveOne(g_fact[i].rescued, &create, &nearVt);
        if (create == kSharedCreate || inst == kSharedInst || IsBannedInst(inst)) {
            g_der[di].factory = g_fact[i].rescued;
            g_der[di].create = create;
            g_der[di].inst = inst;
            g_der[di].shared = true;
            continue;
        }
        g_der[di].factory = g_fact[i].rescued;
        g_der[di].create = create;
        if (inst) g_der[di].inst = inst;
        if (inst && inst != g_fact[i].rescued && !g_der[di].shared && !IsBannedInst(inst)) {
            uint8_t kind = WK_CHAR, gid = 0;
            for (int k = 0; k < g_nWatch; ++k)
                if (g_watch[k].name && !strcmp(g_watch[k].name, g_fact[i].name)) {
                    kind = g_watch[k].kind; gid = g_watch[k].gid; break;
                }
            if (kind == WK_CHAR) {
                WatchAdd((uint32_t)(inst - g_base), g_fact[i].name, gid, kind);
                if (nearVt && nearVt != inst && !IsBannedInst(nearVt))
                    WatchAdd((uint32_t)(nearVt - g_base), g_fact[i].name, gid, kind);
            }
        }
    }
    // uPlayer inst is shared with some uEm* — still hunt the known gold.
    // Do NOT hunt uEm0100 here unless derived inst is unique and not banned.
    for (int i = 0; i < g_nDer; ++i) {
        if (!g_der[i].inst || g_der[i].shared) continue;
        if (IsBannedInst(g_der[i].inst) || g_der[i].create == kSharedCreate) continue;
        if (NearAnyDtiVt(g_der[i].inst)) continue;
        if (strcmp(g_der[i].name, "uPlayer") && strcmp(g_der[i].name, "uCmc")
            && strcmp(g_der[i].name, "uEm0100")) continue;
        if (!strcmp(g_der[i].name, "uEm0100") && g_der[i].inst == kParentVt) continue;
        uint32_t rva = (uint32_t)(g_der[i].inst - g_base);
        uint8_t kind = WK_CHAR, gid = 0;
        for (int k = 0; k < g_nWatch; ++k)
            if (g_watch[k].name && !strcmp(g_watch[k].name, g_der[i].name)) {
                kind = g_watch[k].kind; gid = g_watch[k].gid; break;
            }
        WatchAdd(rva, g_der[i].name, gid, kind);
    }
    // dump17: DTI::create allocates 0x73C0 and writes this vt onto [0]
    WatchAdd((uint32_t)(kGoblinInst - g_base), "uEm0100", 0, WK_CHAR);
    WatchAdd((uint32_t)(kNpcInst - g_base), "uNpc", 0, WK_CHAR);
    WatchAdd((uint32_t)(kEm8000Inst - g_base), "uEm8000", 0x61, WK_CHAR);
    WatchAdd((uint32_t)(kHareInst - g_base), "uEm8600", 0x6B, WK_CHAR);
    if (!g_sUnit) g_sUnit = ScanObjOf("sUnit");
    if (!g_sSet)  g_sSet  = ScanObjOf("sSetManager");
    if (g_sUnit && !g_sUnitInImg) g_sUnitInImg = BytesInImage(g_sUnit, 1700720);
    if (g_sSet && !g_sSetInImg)   g_sSetInImg  = BytesInImage(g_sSet, 112976);

    DWORD t0 = MsNow();
    g_nLives = 0;
    g_nMgrs = 0;
    g_nHold = 0;
    g_nCen = 0;
    g_nGid = 0;
    HeapHunt();
    HopHeapMgrs();
    CollectLeads();
    DumpGidNodes();
    FilterFakeLives();
    g_actFullScan = true;    // Zip 32: HUNT does the wide search
    DumpActors();
    g_actFullScan = false;
    // Zip 36: snapshot decision objects of the first enemy that is not a hare.
    for (int i = 0; i < g_nAct; ++i) {
        if (g_act[i].kind && strcmp(g_act[i].kind, "uEm8600")
            && strcmp(g_act[i].kind, "uEm8000") && g_act[i].ptr) {
            ProbeSidecars(g_act[i].ptr);
            break;
        }
    }
    for (int i = 0; i < g_nLives && g_nLead < 32; ++i) {
        BYTE probe = 0;
        if (Rd((void*)(g_lives[i].ptr + 0x73BF), &probe, 1))
            AddLead("fat29", g_lives[i].ptr, 0x73C0, g_lives[i].ptr);
    }
    FindHolders();
    g_huntMs = MsNow() - t0;
    g_dumped = true;

    logFile << "DevTools: hunt " << g_huntMs << " ms live=" << g_nLives
            << " census=" << g_nCen << " derived=" << g_nDer
            << " mgrs=" << g_nMgrs << " gids=" << g_nGid
            << " leads=" << g_nLead << std::endl;
    WriteDumpJson();
}

static void DumpAnatomy()
{
    g_nLives = 0;
    g_nDptrs = 0;
    g_nHist = 0;
    g_nHunts = 0;
    g_nMoveLine = 0;
    g_nUnitGroup = 0;
    g_nLayout = 0;
    g_embedBytes = 0;
    g_nSeen = 0;
    g_dumped = false;
    g_pWorldObj = 0;
    g_pWorldName = nullptr;
    g_sUnitId = nullptr;
    g_hdrOk = false;
    g_worldHdrOk = false;
    g_setHdrOk = false;
    g_nWin = 0;
    g_nMgrs = 0;
    g_nHold = 0;
    g_huntMs = 0;
    g_huntRegions = 0;
    g_huntBytes = 0;
    g_nDer = 0;
    g_nCen = 0;
    g_nGid = 0;
    g_nFact = 0;
    g_nDti = 0;
    g_nDlink = 0;
    g_nLead = 0;
    g_nTree = 0;
    g_nNear = 0;
    g_nWr = 0;
    g_nNode = 0;
    g_nCtor = 0;
    // Keep g_nAct. DUMP is sUnit anatomy. Zeroing it killed WorldScan until the next HUNT.
    memset(g_hdr, 0, sizeof(g_hdr));
    memset(g_worldHdr, 0, sizeof(g_worldHdr));
    memset(g_setHdr, 0, sizeof(g_setHdr));

    if (!g_base) return;
    if (!g_nWatch) BuildWatch();

    DWORD t0 = MsNow();

    if (pWorld && RdPtr(pWorld, &g_pWorldObj) && g_pWorldObj) {
        Named nw = NameOf(g_pWorldObj);
        g_pWorldName = nw.name;
        HistAdd(nw.name ? nw.name : "pWorld");
        g_worldHdrOk = Rd((void*)g_pWorldObj, g_worldHdr, sizeof(g_worldHdr));
        if (LooksHeap(g_pWorldObj) || InImage(g_pWorldObj)) {
            Consider((uintptr_t)pWorld, 0, g_pWorldObj, "pWorld", 0);
            ScanRegionPtrs(g_pWorldObj, 512, "pWorld", 512);
        }
    }

    g_sUnit = ScanObjOf("sUnit");
    g_sSet  = ScanObjOf("sSetManager");
    g_sUnitInImg = BytesInImage(g_sUnit, 1700720);
    g_sSetInImg  = BytesInImage(g_sSet, 112976);
    g_sUnitFits  = (g_sUnitInImg >= 1700720);

    if (g_sUnit) {
        DumpHeader(g_sUnit);
        Named ns = NameOf(g_sUnit);
        g_sUnitId = ns.name;
        HistAdd(ns.name ? ns.name : "sUnit");
        // header pointers + FULL in-image body for MoveLine/UnitGroup
        ScanRegionPtrs(g_sUnit, 512, "sUnit", 512);
        g_embedBytes = g_sUnitInImg;
        ScanEmbedded(g_sUnit, g_sUnitInImg, "sUnit");
        DumpWindow(0xD0C);   // MoveLine+8 target (sUnit-relative)
        DumpWindow(0x1954);  // sUnit+0x14 target
    }
    if (g_sSet) {
        Named nset = NameOf(g_sSet);
        HistAdd(nset.name ? nset.name : "sSetManager");
        g_setHdrOk = Rd((void*)g_sSet, g_setHdr, sizeof(g_setHdr));
        ScanRegionPtrs(g_sSet, 512, "sSetManager", 512);
        ScanEmbedded(g_sSet, g_sSetInImg, "sSetManager");
        ScanRegionPtrs(g_sSet, g_sSetInImg, "sSetManager", 112976);
    }

    // sPlayerManager candidates: header only, pick the ones whose [0] is the vt
    for (int k = 0; k < g_nScans; ++k) {
        if (!g_scans[k].t || strcmp(g_scans[k].t->name, "sPlayerManager")) continue;
        for (int i = 0; i < g_scans[k].nDataObj; ++i) {
            Named np = NameOf(g_scans[k].dataObj[i]);
            HistAdd(np.name ? np.name : "sPlayerManager?");
            ScanRegionPtrs(g_scans[k].dataObj[i], 256, "sPlayerManager", 256);
        }
    }

    HuntHeapSingletons();

    g_dumpMs = MsNow() - t0;
    g_dumped = true;

    logFile << "DevTools: dump " << g_dumpMs << " ms"
            << " sUnit=0x" << std::hex << g_sUnit
            << " inImg=0x" << g_sUnitInImg << std::dec
            << " fits=" << (g_sUnitFits ? 1 : 0)
            << " moveLine=" << g_nMoveLine
            << " unitGroup=" << g_nUnitGroup
            << " live=" << g_nLives
            << " hunts=" << g_nHunts
            << " heapMgrs=" << g_nMgrs
            << " holders=" << g_nHold
            << " huntMs=" << g_huntMs << std::endl;
    for (int i = 0; i < g_nHist; ++i)
        logFile << "  hist " << g_hist[i].name << " x" << g_hist[i].n << std::endl;
    for (int i = 0; i < g_nLives; ++i)
        logFile << "  live " << g_lives[i].name << " gid=0x" << std::hex
                << (int)g_lives[i].gid << " @ 0x" << g_lives[i].ptr << std::dec << std::endl;

    WriteDumpJson();
}

// ─── UI ──────────────────────────────────────────────────────
static char g_filter[64] = "sEnemy";
static char g_inspectBuf[16] = "";
static char g_inspOffBuf[12] = "0";
static uintptr_t g_inspect = 0;
static uint32_t  g_inspOff = 0;
static int g_sel = -1;

static void HexDump(const void* ptr, uint32_t bytes)
{
    if (bytes > 256) bytes = 256;
    BYTE buf[256];
    if (!Rd(ptr, buf, bytes)) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "unreadable");
        return;
    }
    for (uint32_t row = 0; row < bytes; row += 16) {
        ImGui::Text("%04X:", row);
        ImGui::SameLine(50);
        char line[80]; int n = 0;
        for (uint32_t col = 0; col < 16 && row + col < bytes; ++col)
            n += sprintf_s(line + n, sizeof(line) - n, "%02X ", buf[row + col]);
        ImGui::TextUnformatted(line);
    }
}

static void SetInspect(uintptr_t a)
{
    g_inspect = a;
    sprintf_s(g_inspectBuf, "%08X", (unsigned)a);
}

static void RenderDevToolsUI()
{
    // Покадровый сэмплер масштаба.
    // ВАЖНО: вызывать ДО early-return по CollapsingHeader — иначе замер
    // прекращается, стоит свернуть панель, а мерить надо как раз тогда,
    // когда игрок дерётся с гоблином и не смотрит в UI.
    EnemyTuner::SampleTick();
    AnimProbe::Tick();   // покадровый сэмпл поиска часов анимации
    GoapProbe::Tick();   // слежение за кодом приоритета пешки

    if (!ImGui::CollapsingHeader("DevTools - Type Atlas")) return;
    ImGui::PushID("DT");

    // Версия сборки на виду: чтобы «а какая DLL сейчас стоит» не съедало итерацию.
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1), "build %s  (%s %s)",
                       MOD_BUILD_TAG, __DATE__, __TIME__);
    ImGui::Text("exe base 0x%08X   image end 0x%08X", (unsigned)g_base, (unsigned)ImageEnd());

    // Build 69.2: цена продуктового тика. Оптимизируем по цифрам, а не по
    // ощущениям. Тик идёт раз в 150 мс, поэтому «мс на тик» — это и есть
    // почти вся стоимость скана.
    {
        const Runtime::ScanStats st = Runtime::ScanGetStats();
        const Runtime::Mem::NameCacheStats nc = Runtime::Mem::NameCacheGetStats();
        const uint32_t total = nc.hits + nc.misses;
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1),
            "WorldScan: last %u us | avg %u us | max %u us | ticks %u | actors %d | poll %u KB",
            st.lastUs, st.avgUs, st.maxUs, st.ticks, st.actors, st.pollKb);
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1),
            "Name cache: %u%% hit (%u hit / %u miss), %u vtables known",
            total ? (unsigned)((nc.hits * 100ull) / total) : 0u,
            nc.hits, nc.misses, nc.entries);
        ImGui::SameLine();
        if (ImGui::Button("Reset scan stats")) Runtime::ScanResetStats();
        const uint32_t bad = Runtime::CrtInvalidParamCount();
        if (bad) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
                "CRT invalid parameter x%u - a formatted string overflowed its "
                "buffer somewhere (logged output may be truncated)", bad);
        }
    }

    // --- разведка планировщика пешки ---------------------------------------
    //
    // Панель СВЁРНУТА и разложена по двум строкам. Причина простая: восемь
    // кнопок в ряд растягивали окно на весь экран, и нужную приходилось
    // искать глазами. Плюс пять проб отработали свой вопрос и удалены
    // вместе с кодом (см. GoapProbe.h).
    if (ImGui::CollapsingHeader("GOAP probe (pawn dash track)")) {
        // ЦЕЛЬ ПРОБЫ. Все дампы читали свою пешку; с полной партией это
        // молча давало ответ не про того, на кого смотрит тестер.
        {
            int target = GoapProbe::GetProbePawn();
            ImGui::Text("probe target:");
            for (int i = 0; i <= 2; ++i) {
                int voc = 0, lvl = 0;
                uintptr_t body = 0;
                if (!Runtime::PartyRecordInfo(i, &voc, &lvl, &body)) continue;
                char lbl[48];
                sprintf_s(lbl, "%s##pt%d", i == 0 ? "own" : (i == 1 ? "hired 1" : "hired 2"), i);
                ImGui::SameLine();
                if (ImGui::RadioButton(lbl, target == i)) GoapProbe::SetProbePawn(i);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Vocation %d, level %d, live body %s", voc, lvl,
                                      body ? "found" : "not found");
            }
        }
        // Ряд 1 — измерение. Именно этим снимаются числа трека.
        const bool w = GoapProbe::CodeWatchActive();
        if (w) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1));
        if (ImGui::Button(w ? "code watch: ON" : "code watch: off"))
            GoapProbe::ToggleCodeWatch();
        if (w) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Logs every change of the pawn's selected priority "
                              "code with its current act, and feeds the histogram. "
                              "Turning it ON resets the histogram, so every "
                              "measurement starts clean.");
        ImGui::SameLine();
        if (ImGui::Button("Histogram")) GoapProbe::DumpCodeHistogram();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Frames spent in each priority code, split by combat. "
                              "Enable 'code watch' first.");
        ImGui::SameLine();
        if (ImGui::Button("Plan interfaces")) GoapProbe::DumpPlanInterfaces();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Which cCmc* motor commands are wired into the live "
                              "plan blocks. Every dash so far happened under code 1 "
                              "(Follow), so the jog/dash switch lives inside that "
                              "goal. Press once while the pawn jogs and once while "
                              "it dashes.");

        // Ряд 2 — карты и разборы. Нажимаются редко, обычно один раз.
        if (ImGui::Button("Goal codes")) GoapProbe::DumpPlannerGoals();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("All loaded goals with their priority codes "
                              "(code = (slot - 8) / 4). Re-run it on a pawn of a "
                              "different vocation: the goal set is not the same.");
        ImGui::SameLine();
        if (ImGui::Button("Priority rows")) GoapProbe::DumpPriorityRows();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("All 85 cPrioParam rows by bucket, with the goal name "
                              "next to each code. Says which codes the priority "
                              "layer can nominate at all - dash codes 84/85 have no "
                              "row.");
        ImGui::SameLine();
        if (ImGui::Button("PlanCtrl A/B")) GoapProbe::DumpPlanCtrlAB();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dumps and diffs PlanCtrl(1) against PlanCtrl(84). "
                              "Same array, same type, so equal offsets mean equal "
                              "fields. Read-only.");
        ImGui::SameLine();
        if (ImGui::Button(GoapProbe::SlotSweepActive()
                          ? "Bucket sweep: RUNNING (click to stop)"
                          : "Bucket sweep (code 54)"))
            GoapProbe::ToggleSlotSweep();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Walks the dagger row (code 54) across the 48 "
                              "buckets and counts how much of each fight the "
                              "pawn spends in melee at every position. Answers "
                              "the one open question: which bucket wins between "
                              "PL_Party (bow) and Etc (daggers). WRITES to the "
                              "rule - uncheck the Guardian fix first. Steps "
                              "advance on COMBAT frames only; just keep fighting. "
                              "The report prints itself.");
        ImGui::SameLine();
        if (ImGui::Button("Bucket map")) GoapProbe::DumpBucketMap();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Where each priority row sits in this pawn's 48 "
                              "buckets. The rules are shared by the whole party, "
                              "but the LAYOUT is per pawn and is computed from her "
                              "inclinations - so this is the applied personality. "
                              "Press on one pawn, switch the target, press again: "
                              "the diff is what the inclination actually did.");
        ImGui::SameLine();
        if (ImGui::Button("Inclination rules")) GoapProbe::DumpInclinationRules();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Every personality rule of all 85 rows, expanded: "
                              "code, AddS32, and the inclination + rank it is "
                              "conditioned on. Player reports about Guardian "
                              "(sticks to you, only fights back, drops casts to "
                              "reposition) describe position-over-action, not the "
                              "dagger penalty we fixed - this map shows which "
                              "codes each inclination actually moves.");
        ImGui::SameLine();
        if (ImGui::Button("Party roster")) GoapProbe::DumpPartyRoster();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Every pawn body in the party (a hired pawn is also "
                              "uCmc) with its AI roots. Answers the question that "
                              "decides the scope of every weight edit we make: is "
                              "the priority resource shared by all pawns or one per "
                              "pawn? Hire two pawns before pressing.");

        // Трек идлов: гистограмма ДЕЙСТВИЙ, а не целей.
        const bool aw = GoapProbe::ActWatchActive();
        if (aw) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1));
        if (ImGui::Button(aw ? "act watch: ON" : "act watch: off"))
            GoapProbe::ToggleActWatch();
        if (aw) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Idle track: counts the pawn's ACT classes, not its "
                              "goals. A goal named Wait is one thing; how many "
                              "different poses play under it is another, and the "
                              "idle question is entirely about the second.");
        ImGui::SameLine();
        if (ImGui::Button("Act histogram")) GoapProbe::DumpActHistogram();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Frames and entries per act, split by combat, with the "
                              "pawn's inclinations printed alongside. Stand around "
                              "five minutes, dump, switch the primary inclination, "
                              "dump again - the difference is the answer.");

        ImGui::TextWrapped("%s", GoapProbe::Status());
    }

    // --- охота за множителем темпа анимации --------------------------------
    //
    // Панель ведёт тестера по шагам и не даёт нажать то, что сейчас не имеет
    // смысла. Прошлая версия этого не делала — и первый же тест кончился
    // записью в нулевое смещение и вылетом игры.
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Anim probe (attack tempo hunt)");

        // ---------- ШАГ 1: замер --------------------------------------------
        if (!AnimProbe::Active()) {
            ImGui::TextWrapped("STEP 1. Stand next to the goblin and press Start. "
                               "The status line must show a small distance - that is "
                               "how you know it locked on the one you are fighting.");
            if (ImGui::Button("1. Start on goblin")) AnimProbe::Start("uEm0100");
            ImGui::SameLine();
            if (ImGui::Button("Start on any enemy")) AnimProbe::Start(nullptr);
            ImGui::SameLine();
            if (ImGui::Button("List child objects")) AnimProbe::ScanChildren();

            // Дополнительно можно следить за подобъектом: часов в теле нет,
            // поэтому кандидаты второго уровня всё ещё в игре.
            const int nch = AnimProbe::CandidateCount();
            if (nch) {
                ImGui::Text("also watch:");
                for (int i = 0; i < nch; ++i) {
                    char btn[64];
                    sprintf_s(btn, "%s##wc%d", AnimProbe::CandidateName(i), i);
                    const bool on = AnimProbe::WatchedOffset() == AnimProbe::CandidateOffset(i)
                                 && AnimProbe::WatchedOffset() != 0;
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1));
                    if (ImGui::SmallButton(btn))
                        AnimProbe::WatchChild(AnimProbe::CandidateOffset(i));
                    if (on) ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("body +0x%04X", AnimProbe::CandidateOffset(i));
                    if ((i % 3) != 2 && i + 1 < nch) ImGui::SameLine();
                }
                if (ImGui::SmallButton("body only")) AnimProbe::WatchChild(0);
            }
        } else {
            ImGui::TextWrapped("STEP 2. Let it swing at you three or four times, "
                               "then press Stop. Two attacks minimum, otherwise the "
                               "durations mean nothing.");
            if (ImGui::Button("2. Stop and analyse")) AnimProbe::Stop();
        }

        ImGui::TextWrapped("%s", AnimProbe::Status());
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1), "%s", AnimProbe::Verdict());

        // ---------- ГЛАВНЫЙ ПОДОЗРЕВАЕМЫЙ ------------------------------------
        //
        // Диф по торпору дал два поля тела, стоявшие ровно на 0.5 и ставшие
        // ровно 0.25. Это в точности похоже на шаг проигрывания анимации:
        // игра анимирует 30 к/с при рендере 60, то есть 0.5 кадра за кадр.
        // Проверяется одной кнопкой, без всякого перебора.
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1), "PLAYBACK RATE (confirmed)");
        ImGui::TextWrapped("body +0x0EE4 .. +0x0EF4 - five playback rates in a row, "
                           "1.0 each. Writing 0.5 into any of them halves the monster; "
                           "this was confirmed by hand. 0.5 = half speed, 1.5 = faster. "
                           "The product version of this knob is in the Enemy AI panel "
                           "(attack tempo variation).");
        static float suspectVal = 1.50f;
        ImGui::PushItemWidth(110);
        ImGui::InputFloat("value##susp", &suspectVal, 0.05f, 0.25f, 3);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (!AnimProbe::Active()) {
            ImGui::TextDisabled("press '1. Start on goblin' first");
        } else {
            if (ImGui::Button("WRITE to both suspects")) AnimProbe::SuspectsApply(suspectVal);
            ImGui::SameLine();
            if (ImGui::Button("undo##susp")) AnimProbe::TestRevert();
        }

        // ---------- ШАГ 3: деление пополам ----------------------------------
        const int nc = AnimProbe::ConstCount();
        static float testVal = 1.5f;

        ImGui::Separator();
        if (!nc) {
            ImGui::TextDisabled("No candidates yet - run one capture (Start / Stop) "
                                "or an A/B diff first.");
        } else {
            ImGui::TextWrapped("STEP 3 (fast path). %d candidates. Do NOT test them "
                               "one by one - write into ALL of them at once. If the "
                               "attacks change speed, the multiplier is in the list; "
                               "the tool then halves the set every round. Five fights "
                               "instead of %d.", nc, nc);

            ImGui::PushItemWidth(110);
            ImGui::InputFloat("multiplier", &testVal, 0.1f, 0.5f, 2);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextDisabled("1.5 = look for shorter attacks; 0.5 = longer");

            if (!AnimProbe::BisectActive()) {
                if (!AnimProbe::Active())
                    ImGui::TextDisabled("press '1. Start on goblin' first, then start the bisect");
                else if (ImGui::Button("3. Write to ALL candidates (start bisect)"))
                    AnimProbe::BisectBegin(testVal);
            } else {
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", AnimProbe::BisectStatus());
                ImGui::TextWrapped("Fight, press Stop, read the verdict line above, "
                                   "then answer here. The next set is written "
                                   "automatically when you press Start again.");
                if (AnimProbe::Active()) {
                    ImGui::TextDisabled("stop the capture before answering");
                } else {
                    if (ImGui::Button("FASTER - keep this half")) AnimProbe::BisectReport(true);
                    ImGui::SameLine();
                    if (ImGui::Button("no change - drop this half")) AnimProbe::BisectReport(false);
                }
                ImGui::SameLine();
                if (ImGui::Button("cancel bisect")) AnimProbe::BisectReset();
            }

            // ---------- ручной выбор одного поля ----------------------------
            ImGui::TextDisabled("or pick one field by hand:");
            static int sel = -1;
            if (sel >= nc) sel = -1;
            for (int i = 0; i < nc; ++i) {
                char btn[80];
                sprintf_s(btn, "%s +0x%04X = %.2f##cc%d",
                          AnimProbe::ConstZone(i), AnimProbe::ConstOffset(i),
                          AnimProbe::ConstValue(i), i);
                const bool on = (sel == i);
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1));
                if (ImGui::SmallButton(btn)) sel = i;
                if (on) ImGui::PopStyleColor();
                if ((i % 2) == 0 && i + 1 < nc) ImGui::SameLine();
            }
            const bool canApply = (sel >= 0) && AnimProbe::Active();
            if (!canApply)
                ImGui::TextDisabled(sel < 0 ? "pick a candidate above"
                                            : "press Start first, then Apply");
            else if (ImGui::Button("Apply to selected"))
                AnimProbe::TestApplyIndex(sel, testVal);
            ImGui::SameLine();
            if (ImGui::Button("revert writes")) AnimProbe::TestRevert();
            ImGui::TextColored(AnimProbe::TestActive() ? ImVec4(1, 0.8f, 0.3f, 1)
                                                       : ImVec4(0.6f, 0.6f, 0.6f, 1),
                               "%s", AnimProbe::TestStatus());
        }

        // ---------- ШАГ 4: A/B по торпору -----------------------------------
        //
        // Порядок кнопок = порядок действий. Первый живой прогон показал,
        // что без контрольного слепка отчёт забивается позой существа,
        // поэтому A2 стоит в середине и подписан как обязательный.
        ImGui::Separator();
        ImGui::TextWrapped("STEP 4 (effect diff). Find the knob the engine itself "
                           "turns. Order: A -> A2 -> apply torpor -> B -> Compare. "
                           "Keep the target in the SAME state for A and A2: same "
                           "pose, same act, no walking. Each capture takes ~1 s.");

        // Состояние слепков видно всегда - иначе непонятно, что уже снято.
        ImGui::Text("captures:");
        ImGui::SameLine();
        for (int w = 0; w < 3; ++w) {
            const char* nm = (w == 0) ? "A" : (w == 1 ? "B" : "A2");
            const bool have = AnimProbe::AbHave(w);
            ImGui::TextColored(have ? ImVec4(0.4f, 1.0f, 0.4f, 1)
                                    : ImVec4(0.6f, 0.6f, 0.6f, 1),
                               have ? "%s yes" : "%s no", nm);
            if (w < 2) ImGui::SameLine();
        }

        if (AnimProbe::AbBusy()) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "capturing... hold still");
        } else {
            if (ImGui::Button("1. A: normal")) AnimProbe::AbCapture(0);
            ImGui::SameLine();
            if (ImGui::Button("2. A2: normal again (noise control)")) AnimProbe::AbCapture(2);
            ImGui::SameLine();
            if (ImGui::Button("3. B: under the effect")) AnimProbe::AbCapture(1);

            if (AnimProbe::AbHave(0) && AnimProbe::AbHave(1)) {
                if (ImGui::Button("4. Compare")) AnimProbe::AbCompare();
                if (!AnimProbe::AbHave(2)) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                                       "no A2: expect pose noise");
                }
            } else {
                ImGui::TextDisabled("Compare needs A and B");
            }

            if (ImGui::Button("Dump named char params")) AnimProbe::DumpCharParams();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("cCharParamEnemy = em0100_cmn.prp loaded into the "
                                  "body, twice. 72 named fields incl. res_TORPOR and "
                                  "RESTRAINT_SLOW_LV1/LV2/EX. Goes to the log.");
        }
        ImGui::TextWrapped("%s", AnimProbe::AbStatus());
    }

    ImGui::Spacing();

    // ================= Player + Main Pawn recon (temporary) ===============
    // Only the two controlled subjects are shown here. Detailed bytes go to
    // JSON for offline analysis; the tester sees only body + current action.
    if (ImGui::CollapsingHeader("Player + Main Pawn recon", "partyrecon", true, true)) {
        ImGui::TextWrapped(
            "General priority sidecar: profiles contain 0..48 exact rule entries. "
            "'-' switches vanilla/research_pair45_46; custom profiles are selected in "
            "DDDA_AI_Overhaul\\ddda_pawn_ai_profiles.ini. '=' captures a snapshot.");
        if (ImGui::Button("Find both + capture baseline", ImVec2(280, 28)))
            PartyCapture(true);
        ImGui::SameLine();
        if (ImGui::Button("Switch profile", ImVec2(110, 28)))
            PartyPriorityProfileToggle();
        ImGui::SameLine();
        ImGui::TextDisabled("- profile  = snapshot");

        // Build 56.5: исследовательские дампы OFF по умолчанию.
        if (ImGui::Checkbox("Write research dumps (json/csv)", &g_researchDump)) {
            config.setBool("devtools", "researchDump", g_researchDump);
            if (!g_researchDump && g_intentTrace) PartyIntentTraceStop("dump disabled");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When ON, capture writes ddda_party_recon / ddda_pawn_ai_bridge / ddda_pawn_intent_trace. Off by default - not needed for Guardian doctrine.");

        ImVec4 pcol = g_nParty >= 2
            ? ImVec4(0.35f, 1.0f, 0.35f, 1.0f)
            : ImVec4(1.0f, 0.65f, 0.25f, 1.0f);
        ImGui::TextColored(pcol, "%s", g_partyStatus);
        for (int i = 0; i < g_nParty; ++i) {
            PartyBodyDump& P = g_party[i];
            ImGui::Text("%-11s  %-7s  body 0x%08X", P.role, P.dti, (unsigned)P.ptr);
            ImGui::SameLine();
            if (P.actName[0])
                ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.55f, 1.0f),
                    "%s  @+0x%04X", P.actName, P.actOff);
            else
                ImGui::TextDisabled("action not found yet");
        }
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", g_partyAiStatus);
        ImVec4 profileColor = strcmp(g_priorityProfileActive, "vanilla")
            ? ImVec4(1.0f, 0.65f, 0.20f, 1.0f)
            : ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
        ImGui::TextColored(profileColor, "%s", g_priorityProfileStatus);
        if (g_partyAiLastFile[0])
            ImGui::TextDisabled("AI snapshot: ddda_pawn_ai_bridge_%03d.json", g_partyAiSeq);
        if (g_intentTrace)
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.75f, 1.0f),
                "Intent trace recording: ddda_pawn_intent_trace_%03d.csv",
                g_intentTraceSeq);
        if (g_intentLiveValid) {
            ImGui::Text("Intent [%u]: %s%s", g_intentLiveCode,
                PawnPriorityIntentName(g_intentLiveCode),
                PawnPriorityIntentMapped(g_intentLiveCode) ? "" : " [family only]");
            ImGui::Text("Exact action: %s", g_intentLiveAction[0]
                ? g_intentLiveAction : "none");
            ImGui::Text("Current target: %s (0x%08X)%s", g_intentLiveTargetName[0]
                ? g_intentLiveTargetName : "none", (unsigned)g_intentLiveTarget,
                g_intentLiveTarget && g_intentLiveCode == 0xFFFFFFFFu
                    ? " [retained; planner inactive]" : "");
        }
        ImGui::TextDisabled("DTI AI candidates in census: %d", g_nPawnAi);
    }

    ImGui::Spacing();

    // ================= Мутации сущностей (EnemyTuner) =====================
    //
    // СОСТОЯНИЕ ВСЕГДА СВЕРХУ. Урок теста 11: пользователь нажал FORCE,
    // тело встало под удержание, и ini «перестал работать». Индикатор был,
    // но рисовался ПОД кнопками и в глаза не бросался. Теперь статус —
    // первое, что видно в разделе.
    // ImGui 1.48: флагов ImGuiTreeNodeFlags_* ещё нет, а "открыт по
    // умолчанию" задаётся 4-м аргументом CollapsingHeader(label, id,
    // display_frame, default_open). Не переносить сюда синтаксис новых версий.
    if (ImGui::CollapsingHeader("Entity mutations", "entmut", true, true)) {
        if (EnemyTuner::HeldBody()) {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.15f, 1),
                "HOLD 0x%08X = %.3f  -- ini is NOT applied to this body",
                (unsigned)EnemyTuner::HeldBody(), EnemyTuner::HeldValue());
            if (ImGui::Button("RELEASE - give control back to ini", ImVec2(300, 26)))
                EnemyTuner::ReleaseHold();
        } else {
            ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1),
                "Scale is driven by ddda_entities.ini");
        }

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s",
                           EnemyTuner::StatusLine());
        ImGui::Text("alive: %d   dead: %d   writes: %d",
                    EnemyTuner::TrackedCount(), DevTools::DeadCount(),
                    EnemyTuner::WriteCount());
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1),
                           "dead = ActDie/ActDeadBody, excluded from mutations");

        ImGui::Spacing();

        // Осмотр — то, чем пользуемся постоянно.
        if (ImGui::Button("List enemies + scale", ImVec2(300, 26)))
            EnemyTuner::ListEnemies();

        // Ручная проверка: временно перебивает ini.
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1),
                           "Manual check (sets HOLD):");
        if (ImGui::Button("Shrink 0.60", ImVec2(146, 26)))
            EnemyTuner::ForceScale(0.60f);
        ImGui::SameLine();
        if (ImGui::Button("Reset 1.00", ImVec2(146, 26)))
            EnemyTuner::ForceScale(1.00f);

        // Инструменты разведки: нужны редко, свёрнуты по умолчанию.
        // Не удаляем — на них будем искать зрение, скорость и поводок.
        if (ImGui::TreeNode("Recon tools")) {
            if (ImGui::Button("Dump body head 0x00..0x100", ImVec2(290, 24)))
                EnemyTuner::DumpHead();
            if (ImGui::Button("Read charParam (leash)", ImVec2(290, 24)))
                EnemyTuner::ReadCharParam();
            if (ImGui::Button("Scan vision params", ImVec2(290, 24)))
                EnemyTuner::ScanVisionParams();
            if (ImGui::Button("Dump sensor window +0x5900", ImVec2(290, 24)))
                EnemyTuner::DumpSensorWindow();

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1),
                               "Per-frame search for changing fields:");
            if (ImGui::Button("FieldScan start", ImVec2(142, 24)))
                EnemyTuner::StartFieldScan();
            ImGui::SameLine();
            if (ImGui::Button("FieldScan report", ImVec2(142, 24)))
                EnemyTuner::StopFieldScan();
            if (ImGui::Button("Check HOLD", ImVec2(290, 24)))
                EnemyTuner::CheckHold();
            ImGui::TreePop();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("SCAN exe for manager objects", ImVec2(300, 26))) {
        ScanImage();
        WriteScanJson();
    }
    if (g_scanned) {
        ImGui::SameLine();
        ImGui::TextDisabled("done in %u ms", g_scanMs);
    }

    if (g_scanned && ImGui::TreeNode("Scan results")) {
        for (int k = 0; k < g_nScans; ++k) {
            TypeScan& s = g_scans[k];
            if (s.skipped) {
                ImGui::TextDisabled("%-24s  shared stub vtable - skip", s.t->name);
                continue;
            }
            const bool ok = s.nDataObj > 0;
            ImVec4 col = ok ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.55f, 0.25f, 1);
            ImGui::TextColored(col, "%-24s  dataObj %d   codeRefs %d",
                s.t->name, s.nDataObj, s.nCode);
            for (int i = 0; i < s.nDataObj; ++i) {
                ImGui::SameLine(0, 8);
                char lab[32];
                sprintf_s(lab, "obj %08X##o%d%d", (unsigned)s.dataObj[i], k, i);
                if (ImGui::SmallButton(lab)) SetInspect(s.dataObj[i]);
            }
        }
        ImGui::TextDisabled("Green = vtable dword in the exe. Orange = not in the image.");
        ImGui::TreePop();
    }

    ImGui::Spacing();
    if (ImGui::Button("DUMP anatomy", ImVec2(180, 28)))
        DumpAnatomy();
    ImGui::SameLine();
    if (ImGui::Button("HUNT live units", ImVec2(180, 28)))
        HuntLive();
    if (!InWorld()) {
        ImGui::SameLine();
        ImGui::TextDisabled("HUNT waits until a save is loaded");
    }
    if (g_dumped) {
        ImGui::SameLine();
        ImGui::TextDisabled("%u ms (hunt %u)  json", g_dumpMs, g_huntMs);

        ImGui::Text("pWorld obj 0x%08X  %s",
            (unsigned)g_pWorldObj, g_pWorldName ? g_pWorldName : "(no vtable name)");
        ImGui::Text("sUnit  0x%08X  in-image %u / 1700720  fits=%s  id=%s",
            (unsigned)g_sUnit, g_sUnitInImg, g_sUnitFits ? "YES" : "NO",
            g_sUnitId ? g_sUnitId : "?");
        ImGui::Text("sSet   0x%08X  in-image %u / 112976",
            (unsigned)g_sSet, g_sSetInImg);
        ImGui::Text("embed %u B  MoveLine %d  UnitGroup %d  layout %d",
            g_embedBytes, g_nMoveLine, g_nUnitGroup, g_nLayout);
        ImGui::Text("hunt %u ms  %d regions  %.1f MB  keys %d  live %d  mgrs %d  census %d  derived %d",
            g_huntMs, g_huntRegions, g_huntBytes / 1048576.0f, g_nHvt,
            g_nLives, g_nMgrs, g_nCen, g_nDer);
        ImGui::TextWrapped(
            "Green names below are heap objects whose first dword is a known vtable.");

        if (ImGui::TreeNode("Histogram (what the pointers actually are)")) {
            if (!g_nHist) ImGui::TextDisabled("nothing identified - paste the json anyway");
            for (int i = 0; i < g_nHist; ++i)
                ImGui::Text("%4d  %s", g_hist[i].n, g_hist[i].name);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Followed pointers")) {
            for (int i = 0; i < g_nDptrs; ++i) {
                char lab[96];
                sprintf_s(lab, "%s +0x%X -> %-20s %08X##dp%d",
                    g_dptrs[i].fromName, g_dptrs[i].off,
                    g_dptrs[i].name, (unsigned)g_dptrs[i].ptr, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_dptrs[i].ptr);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Heap singleton hunts")) {
            if (!g_nHunts) ImGui::TextDisabled("none - managers live behind another pointer");
            for (int i = 0; i < g_nHunts; ++i) {
                char lab[80];
                sprintf_s(lab, "%-22s inst %08X##h%d",
                    g_hunts[i].name, (unsigned)g_hunts[i].instance, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_hunts[i].instance);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Heap managers (vtable on MEM_PRIVATE)")) {
            if (!g_nMgrs) ImGui::TextDisabled("none - sEnemyManager etc. not on the private heap");
            for (int i = 0; i < g_nMgrs; ++i) {
                char lab[80];
                sprintf_s(lab, "%-22s %08X##hm%d",
                    g_mgrs[i].name, (unsigned)g_mgrs[i].ptr, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_mgrs[i].ptr);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Holders (who points at a live unit)")) {
            if (!g_nHold) ImGui::TextDisabled("no live units, or nobody in sUnit/sSet/.data holds them");
            for (int i = 0; i < g_nHold; ++i) {
                char lab[112];
                sprintf_s(lab, "%s +0x%X  -> %-14s %08X##hd%d",
                    g_hold[i].where, g_hold[i].off,
                    g_hold[i].name, (unsigned)g_hold[i].live, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_hold[i].slot);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Derived instance vtables (from create_*)")) {
            if (!g_nDer) ImGui::TextDisabled("none derived");
            int shown = 0;
            for (int i = 0; i < g_nDer && shown < 24; ++i) {
                bool same = g_der[i].inst && g_der[i].inst == g_der[i].factory;
                if (same && shown > 8) continue; // skip boring same=true after a few
                const char* tag = !g_der[i].inst ? "fail" : g_der[i].shared ? "SHARED" : same ? "same" : "DIFF";
                ImGui::Text("%-20s fact %08X  inst %08X  %s",
                    g_der[i].name, (unsigned)g_der[i].factory, (unsigned)g_der[i].inst, tag);
                shown++;
            }
            ImGui::TextDisabled("SHARED = several types wrote the same vt (base). Not a species. DIFF = hunt key.");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Heap census (LooksLikeVtable, sorted by count)")) {
            if (!g_nCen) ImGui::TextDisabled("no vtable-like objects on MEM_PRIVATE");
            int order[96];
            int nc = g_nCen < 96 ? g_nCen : 96;
            for (int i = 0; i < nc; ++i) order[i] = i;
            for (int i = 0; i < nc && i < 24; ++i) {
                int best = i;
                for (int j = i + 1; j < nc; ++j)
                    if (g_cen[order[j]].n > g_cen[order[best]].n) best = j;
                int tmp = order[i]; order[i] = order[best]; order[best] = tmp;
                const Census& c = g_cen[order[i]];
                const char* nm = NameVt(c.vt);
                char lab[112];
                sprintf_s(lab, "%4d  vt %08X  %-20s##cen%d",
                    c.n, (unsigned)c.vt, nm ? nm : "(unknown)", i);
                if (ImGui::SmallButton(lab)) SetInspect(c.sample[0]);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Factory heads (create=0 suspects)")) {
            if (!g_nFact) ImGui::TextDisabled("none - HUNT first");
            for (int i = 0; i < g_nFact; ++i) {
                char lab[96];
                sprintf_s(lab, "%-16s [0] %08X  ecx %08X  rescue %08X##fh%d",
                    g_fact[i].name ? g_fact[i].name : "?",
                    (unsigned)g_fact[i].slot0, (unsigned)g_fact[i].ecx,
                    (unsigned)g_fact[i].rescued, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_fact[i].ecx ? g_fact[i].ecx : g_fact[i].va);
            }
            ImGui::TextDisabled("Click = factory object (ecx). got= name the stub really registered.");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("DTI objects (name at +4 in .data)")) {
            if (!g_nDti) ImGui::TextDisabled("none - HUNT first. Looking for exact type-name strings.");
            for (int i = 0; i < g_nDti; ++i) {
                char lab[112];
                sprintf_s(lab, "%-16s inst %08X  n=%d  slot %d##dti%d",
                    g_dti[i].want ? g_dti[i].want : "?",
                    (unsigned)g_dti[i].foundInst, g_dti[i].nCands,
                    g_dti[i].foundSlot, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_dti[i].foundInst ? g_dti[i].foundInst : g_dti[i].dti);
            }
            ImGui::TextDisabled("inst = UNIQUE vtable across types. Shared parent is left 0.");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("DTI links (+08/+0C/+10/+14)")) {
            if (!g_nDlink) ImGui::TextDisabled("none - HUNT first");
            for (int i = 0; i < g_nDlink; ++i) {
                char lab[112];
                sprintf_s(lab, "%s +0x%X  %-16s %08X##lk%d",
                    g_dlink[i].owner ? g_dlink[i].owner : "?", g_dlink[i].off,
                    g_dlink[i].name[0] ? g_dlink[i].name : "?", (unsigned)g_dlink[i].ptr, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_dlink[i].ptr);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Leads (ptrs out of gid-0x05 / cLinkUnitEnemy)")) {
            if (!g_nLead) ImGui::TextDisabled("none - HUNT first");
            for (int i = 0; i < g_nLead; ++i) {
                char lab[128];
                sprintf_s(lab, "%s%s +0x%X -> %-12s %08X gid=%02X##ld%d",
                    g_lead[i].fat ? "*" : " ",
                    g_lead[i].fromName ? g_lead[i].fromName : "?",
                    g_lead[i].off, g_lead[i].name ? g_lead[i].name : "?",
                    (unsigned)g_lead[i].ptr, g_lead[i].gid, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_lead[i].ptr);
            }
            ImGui::TextDisabled("* = readable 8KB. Click to inspect.");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Factory neighborhood / text writes")) {
            ImGui::Text("near %d   writes %d   nodes %d", g_nNear, g_nWr, g_nNode);
            for (int i = 0; i < g_nNear && i < 16; ++i) {
                char lab[96];
                sprintf_s(lab, "%s %+d  %08X##nf%d",
                    g_near[i].owner ? g_near[i].owner : "?", g_near[i].off,
                    (unsigned)g_near[i].val, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_near[i].at);
            }
            ImGui::TextDisabled("Gold check: uEm0900 +D8 == 015B5A80. writes = rare C7 vt");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("DTI tree (uEnemy children)")) {
            if (!g_nTree) ImGui::TextDisabled("none - HUNT first");
            for (int i = 0; i < g_nTree; ++i) {
                char lab[112];
                sprintf_s(lab, "%-20s sz %u  id %u  %08X##tr%d",
                    g_tree[i].name[0] ? g_tree[i].name : "?",
                    g_tree[i].size, g_tree[i].typeId, (unsigned)g_tree[i].dti, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_tree[i].dti);
            }
            ImGui::TextDisabled("+08=next sibling  +0C=first child  +10=parent");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Gid scouts (+0x2D, filtered)")) {
            if (!g_nGid) ImGui::TextDisabled("none - no filtered heap object with a bestiary typeId at +0x2D");
            for (int i = 0; i < g_nGid; ++i) {
                char lab[112];
                sprintf_s(lab, "%s gid 0x%02X  %-14s vt %08X  %08X##gs%d",
                    g_gid[i].want ? "*" : " ",
                    g_gid[i].gid, g_gid[i].name ? g_gid[i].name : "?",
                    (unsigned)g_gid[i].vt, (unsigned)g_gid[i].ptr, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_gid[i].ptr);
            }
            ImGui::TextDisabled("* = gid 0x05 goblin (reserved). Others = filtered samples.");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Live characters")) {
            WorldReport wr = CombatBus::Instance().LastWorld();
            DWORD now = MsNow();
            DWORD age = (wr.timestampMs && now >= wr.timestampMs) ? (now - wr.timestampMs) : 0;
            if (g_nAct) {
                ImGui::Text("world %d  goblins %d  cat %d  age %u ms",
                    wr.count, wr.goblinCount, wr.dominantCategory, age);
                ImGui::TextDisabled("json = photo on HUNT. Rows tick. Empty list polls heap, no Hunt.");
                for (int i = 0; i < g_nAct; ++i) {
                    char lab[112];
                    sprintf_s(lab, "%-8s gid=%02X st=%02X  %08X  %.0f %.0f %.0f##ac%d",
                        g_act[i].kind ? g_act[i].kind : "?",
                        g_act[i].gid, g_act[i].st14, (unsigned)g_act[i].ptr,
                        g_act[i].x, g_act[i].y, g_act[i].z, i);
                    if (ImGui::SmallButton(lab)) SetInspect(g_act[i].ptr);
                    // Zip 32 — live action state
                    if (g_act[i].actName) {
                        bool dead = g_act[i].actCat && !strcmp(g_act[i].actCat, "death");
                        bool tnt  = g_act[i].actCat && !strcmp(g_act[i].actCat, "taunt");
                        ImGui::SameLine();
                        ImGui::TextColored(
                            dead ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                                 : tnt ? ImVec4(1.0f, 0.85f, 0.30f, 1.0f)
                                       : ImVec4(0.55f, 0.95f, 0.55f, 1.0f),
                            "%s (%s) @+%04X x%d",
                            g_act[i].actName, g_act[i].actCat ? g_act[i].actCat : "?",
                            g_act[i].actOff, g_act[i].actHits);
                        if (g_act[i].actName2) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("| 2nd %s @+%04X",
                                g_act[i].actName2, g_act[i].actOff2);
                        }
                    } else {
                        // Zip 34: no atlas match, but the object names itself.
                        const char* dtiName = 0;
                        for (int r = 0; r < g_act[i].nRaw; ++r)
                            if (g_act[i].rawOff[r] == kActSlot && g_act[i].rawName[r][0]) {
                                dtiName = g_act[i].rawName[r]; break;
                            }
                        ImGui::SameLine();
                        if (dtiName) {
                            bool dead = strstr(dtiName, "Die") || strstr(dtiName, "Dead");
                            bool tnt  = strstr(dtiName, "Howl") || strstr(dtiName, "Threat")
                                     || strstr(dtiName, "Dance");
                            ImGui::TextColored(
                                dead ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                                     : tnt ? ImVec4(1.0f, 0.85f, 0.30f, 1.0f)
                                           : ImVec4(0.55f, 0.95f, 0.55f, 1.0f),
                                "%s @+%04X", dtiName, kActSlot);
                        } else if (g_act[i].nRaw) {
                            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                                "%d raw vt (see json)", g_act[i].nRaw);
                        } else {
                            ImGui::TextDisabled("act=? (press HUNT)");
                        }
                    }
                }
                ImGui::Text("act slot: 0x%04X %s", g_actSlotOff,
                    g_actSlotOff ? "(locked, cheap re-read each tick)" : "(unknown — press HUNT)");
                ImGui::SameLine();
                if (ImGui::SmallButton("re-search act slot")) { g_actSlotOff = 0; g_actFullScan = true; RewalkActors(); g_actFullScan = false; }
            } else {
                ImGui::TextDisabled("list empty - polling hot heap 0x10000000-0x18000000, 8MB/tick.");
            }
            if (g_nLives) {
                ImGui::TextDisabled("hunt snapshot (frozen until next HUNT)");
                for (int i = 0; i < g_nLives; ++i) {
                    char lab[80];
                    sprintf_s(lab, "%-16s gid=0x%02X  %08X##lu%d",
                        g_lives[i].name, g_lives[i].gid, (unsigned)g_lives[i].ptr, i);
                    if (ImGui::SmallButton(lab)) SetInspect(g_lives[i].ptr);
                }
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::TreeNode("Factory slots (DEAD experiment, always empty)")) {
        ImGui::TextDisabled("TSV FactoryPointer column. Empty since day 1. Not a progress bar.");
        for (int i = 0; kManagers[i]; ++i) {
            const TypeAtlas::Info* t = TypeAtlas::FindByName(kManagers[i]);
            if (!t) continue;
            Probe p = ProbeType(*t);
            ImGui::TextDisabled("%-24s  %s  slot=0x%08X  raw=0x%08X",
                t->name, KindName(p.kind), (unsigned)p.slot, (unsigned)p.raw);
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Inspect address");
    ImGui::SameLine();
    ImGui::PushItemWidth(120);
    if (ImGui::InputText("##insp", g_inspectBuf, sizeof(g_inspectBuf),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        g_inspect = (uintptr_t)strtoul(g_inspectBuf, nullptr, 16);
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextUnformatted("+");
    ImGui::SameLine();
    ImGui::PushItemWidth(70);
    if (ImGui::InputText("##off", g_inspOffBuf, sizeof(g_inspOffBuf),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        g_inspOff = (uint32_t)strtoul(g_inspOffBuf, nullptr, 16);
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::SmallButton("head")) { g_inspOff = 0; memcpy(g_inspOffBuf, "0", 2); }
    ImGui::SameLine();
    if (ImGui::SmallButton("+5BD0")) { g_inspOff = 0x5BD0; memcpy(g_inspOffBuf, "5BD0", 5); }
    ImGui::SameLine();
    if (ImGui::SmallButton("+6000")) { g_inspOff = 0x6000; memcpy(g_inspOffBuf, "6000", 5); }
    ImGui::SameLine();
    if (ImGui::SmallButton("+6150")) { g_inspOff = 0x6150; memcpy(g_inspOffBuf, "6150", 5); }
    if (g_inspect) {
        Named hit = NameOf(g_inspect);
        if (hit.name)
            ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Identify: %s  kind=%u  watchGid=0x%02X  vtRVA=0x%X",
                hit.name, hit.kind, hit.gid, hit.rva);
        else
            ImGui::TextDisabled("Identify: vtable not in atlas (or unreadable)");
        BYTE b2d = 0, st14 = 0;
        float f4c = 0.f;
        Rd((void*)(g_inspect + 0x2D), &b2d, 1);
        Rd((void*)(g_inspect + 0x14), &st14, 1);
        Rd((void*)(g_inspect + 0x4C), &f4c, 4);
        ImGui::Text("body +14=%02X  +2D=%02X  +4C=%.3f   showing +0x%X",
            st14, b2d, f4c, g_inspOff);
        ImGui::TextDisabled("watchGid is the atlas/watch table, not +2D. First 256B = transform.");
        HexDump((void*)(g_inspect + g_inspOff), 256);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Type catalog - 4405 names. Not live objects.");
    ImGui::PushItemWidth(220);
    ImGui::InputText("filter", g_filter, sizeof(g_filter));
    ImGui::PopItemWidth();

    ImGui::BeginChild("atlasList", ImVec2(0, 160), true);
    int shown = 0;
    for (int i = 0; i < TypeAtlas::kCount && shown < 80; ++i) {
        const TypeAtlas::Info& t = TypeAtlas::kTypes[i];
        if (g_filter[0] && !strstr(t.name, g_filter)) continue;
        if (ImGui::Selectable(t.name, g_sel == i)) g_sel = i;
        ++shown;
    }
    if (shown == 80) ImGui::TextDisabled("... narrow the filter");
    ImGui::EndChild();

    if (g_sel >= 0 && g_sel < TypeAtlas::kCount) {
        const TypeAtlas::Info& t = TypeAtlas::kTypes[g_sel];
        ImGui::Text("%s   size %u   typeId 0x%02X", t.name, t.size, t.typeId);
        ImGui::Text("factory slot VA 0x%08X   vtable VA 0x%08X",
            (unsigned)(g_base + t.factoryRVA), (unsigned)(g_base + t.factoryVtRVA));
    }

    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Build 69: реализация Runtime::ResearchHooks. Продукт вызывает эти функции
// в фиксированных точках и не знает, что за ними стоит.
// ---------------------------------------------------------------------------
static void ResearchOnSnapshotEarly()
{
    if (g_researchDump) PartyWriteJson();
}

static void ResearchOnSnapshotFull()
{
    if (g_researchDump) PartyWriteAiBridgeJson();
    if (g_researchDump && !g_intentTrace) PartyIntentTraceStart();
}

static void ResearchOnTick()
{
    PartyTraceTick();
    PartyIntentTraceTick();
}

static void ResearchOnWorldUnload(const char* reason)
{
    PartyIntentTraceStop(reason);
}

void Hooks::DevTools()
{
    // Дефолт OFF: игроку DevTools не нужен, а WorldScan_Tick стоит 150 мс-обхода.
    // Для разработки включается в ddda_ai_overhaul.ini: [devtools] enabled = on
    g_enabled = config.getBool("devtools", "enabled", false);
    // Исследовательские дампы (JSON/CSV) — отдельно, дефолт OFF.
    g_researchDump = config.getBool("devtools", "researchDump", false);
    // Build 69: базу образа и секции поднимает Runtime::Init() ДО этого места
    // и независимо от [devtools] enabled. Здесь только проверка.
    if (!g_base) logFile << "DevTools: image base not resolved by runtime" << std::endl;

    // Build 69: DevTools подписывает свой research-слой на продуктовые хуки.
    // Пока панель выключена, указатели нулевые и продукт в research не заходит.
    Runtime::ResearchHooks hooks = {};
    hooks.onSnapshotEarly = ResearchOnSnapshotEarly;
    hooks.onSnapshotFull  = ResearchOnSnapshotFull;
    hooks.onTick          = ResearchOnTick;
    hooks.onWorldUnload   = ResearchOnWorldUnload;
    Runtime::SetResearchHooks(hooks);

    if (!g_enabled) {
        logFile << "DevTools: disabled (product layer runs independently)" << std::endl;
        return;
    }

    // Build 69: priority-профили поднимает Runtime::Init() — они нужны
    // продукту, а не панели.
    BuildWatch();

    logFile << "DevTools: TypeAtlas " << TypeAtlas::kCount
            << "  watch=" << g_nWatch
            << "  base=0x" << std::hex << g_base
            << "  imageEnd=0x" << (g_base + g_imageSize) << std::dec
            << "  (in-world: DUMP anatomy, then HUNT)"
            << std::endl;

    InGameUIAdd(RenderDevToolsUI);
}

void Hooks::DevTools_Shutdown()
{
    // Build 69: здесь остаётся только research. Откат priority-правил —
    // забота продукта, он делается в Runtime::Shutdown().
    PartyIntentTraceStop("DLL detach");
}

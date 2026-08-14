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
#include "TypeAtlas.Generated.h"
#include "EnemyTypes.Generated.h"
#include "ActMap.Generated.h"
#include "devtools/DevTools.h"
#include "devtools/TypeCallers.Generated.h"
#include "CombatBus.h"
#include <stdio.h>

extern BYTE *codeBase, *codeEnd, *dataBase, *dataEnd;

static bool g_enabled = false;
static uintptr_t g_base = 0;
static uint32_t g_imageSize = 0;

static bool Rd(const void* p, void* out, size_t n)
{
    if (!p || IsBadReadPtr(p, (UINT_PTR)n)) return false;
    __try { memcpy(out, p, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool RdPtr(const void* p, uintptr_t* out) { return Rd(p, out, sizeof(uintptr_t)); }

static uintptr_t ImageEnd() { return g_base + g_imageSize; }
static bool InImage(uintptr_t a)
{
    return g_base && a >= g_base && a < ImageEnd();
}
static bool LooksHeap(uintptr_t a)
{
    // 4-byte aligned user pointers only. Odd values are flags/packed ints.
    // Low 256MB page-round numbers (0x08000000, 0x08040000) were false hits.
    if (a < 0x01000000 || a >= 0x80000000 || (a & 3) || InImage(a)) return false;
    if (a < 0x10000000 && (a & 0xFFFF) == 0) return false;
    return true;
}

// Save-layer check. Same idea as CombatIntel::IsInActiveGameplay.
// HUNT must not run on the title screen or mid-load.
static bool InWorld()
{
    if (!pBase || !*pBase) return false;
    BYTE* pl = *pBase + 0xA7000;
    UINT16 level = 0;
    float maxHp = 0.f;
    if (!Rd(pl + 0xDD0, &level, 2) || !level) return false;
    if (!Rd(pl + 0x96C + 4, &maxHp, 4)) return false;
    if (maxHp <= 0.f || maxHp > 200000.f) return false;
    return true;
}

static bool IsRepeatTag(uint32_t v)
{
    if (!v || v == 0xFFFFFFFFu) return false;
    uint8_t b = (uint8_t)v;
    return v == (uint32_t)b * 0x01010101u;
}

static const char* TagName(uint32_t v)
{
    switch (v) {
    case 0x50505050: return "tag/AABB";
    case 0xD0D0D0D0: return "tag/D0";
    case 0x0F0F0F0F: return "tag/0F";
    case 0x78787878: return "tag/78";
    case 0xF0F0F0F0: return "tag/F0";
    case 0x5A5A5A5A: return "tag/5A";
    case 0x4B4B4B4B: return "tag/4B";
    case 0x3C3C3C3C: return "tag/3C";
    default: return IsRepeatTag(v) ? "tag/xx" : nullptr;
    }
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

    DWORD t0 = GetTickCount();

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

    g_scanMs = GetTickCount() - t0;
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

struct SecRange { uintptr_t lo, hi; };
static SecRange  g_exec[8];
static int       g_nExec = 0;
static SecRange  g_rdata[8];
static int       g_nRdata = 0;

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
static const uintptr_t kGoblinInst   = 0x015852A8u; // meth4 writes this onto new uEm0100
static const uintptr_t kNpcInst      = 0x015D2618u; // dump19: uNpc DTI vt - 0x344
static const uintptr_t kEm8000Inst   = 0x015BB278u; // dump19 list, gid 0x61
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

struct ActorDump {
    uintptr_t   ptr, vt, next, prev, subVt;
    uint8_t     gid, st14;
    float       x, y, z;
    bool        fat29, subOk, win5bOk, win60Ok;
    const char* kind;
    BYTE        win5b[16];
    BYTE        win60[64];
    // Zip 32 — ActScan: current-action pointer inside the 29KB body.
    uint32_t    actOff;      // offset where the Act* was found
    uintptr_t   actPtr;      // the action object
    uint32_t    actVtRva;    // its vtable RVA
    const char* actName;     // "ThreatHowl" / "Die" / ...
    const char* actCat;      // "taunt" / "death" / ...
    int         actHits;     // how many ActMap-matching ptrs in the body
    uint32_t    actOff2;     // second candidate (previous/queued action)
    const char* actName2;
    // Zip 33 — raw mode: vtable-bearing objects in the body, NO ActMap filter.
    // ActMap holds factory vtables; live objects carry instance vtables.
    // These are the real ones, harvested so we can build the bridge.
    uint32_t    rawOff[40];
    uint32_t    rawVt[40];
    uint32_t    rawPtr[40];   // Zip 35: object address — embedded vs heap
    char        rawName[40][40];   // Zip 34: real class name read from DTI
    int         nRaw;
};
static ActorDump g_act[32];
static int       g_nAct = 0;
static uintptr_t g_pollAddr = 0x10000000u;
static uintptr_t g_lastBand = 0x10000000u;
static int       g_emptyPoll = 0;
static const uintptr_t kUnk84Inst = 0x015D1D30u; // dump22 list, gid 0x84
static const uintptr_t kHareInst  = 0x015BD9D0u; // meth4_uEm8600 dump23
static const uintptr_t kHotLo     = 0x10000000u;
static const uintptr_t kHotHi     = 0x18000000u; // all dump18-23 actors live here

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

static void InitSections()
{
    g_nExec = 0;
    g_nRdata = 0;
    if (!g_base) return;
    auto dos = (IMAGE_DOS_HEADER*)g_base;
    auto nt  = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    const int nsec = nt->FileHeader.NumberOfSections;
    for (int i = 0; i < nsec; ++i) {
        uintptr_t lo = g_base + sec[i].VirtualAddress;
        uint32_t sz = sec[i].Misc.VirtualSize;
        if (!sz || sec[i].VirtualAddress >= g_imageSize) continue;
        if (sec[i].VirtualAddress + sz > g_imageSize)
            sz = g_imageSize - sec[i].VirtualAddress;
        if (!sz) continue;
        uintptr_t hi = lo + sz;
        const bool exec = (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (exec && g_nExec < 8) {
            g_exec[g_nExec].lo = lo; g_exec[g_nExec].hi = hi; g_nExec++;
        } else if (!exec && g_nRdata < 8) {
            g_rdata[g_nRdata].lo = lo; g_rdata[g_nRdata].hi = hi; g_nRdata++;
        }
    }
}

static bool InExec(uintptr_t a)
{
    for (int i = 0; i < g_nExec; ++i)
        if (a >= g_exec[i].lo && a < g_exec[i].hi) return true;
    return false;
}

static bool InRdata(uintptr_t a)
{
    for (int i = 0; i < g_nRdata; ++i)
        if (a >= g_rdata[i].lo && a < g_rdata[i].hi) return true;
    return false;
}

// Vtable lives in rdata; first slots are methods in .text.
// InImage alone is too weak: 0x400000 (MZ) and patterned dwords leaked into census.
static bool LooksLikeVtable(uintptr_t vt)
{
    if (!vt || (vt & 3) || InExec(vt)) return false;
    if (g_base && vt < g_base + 0x1000) return false; // DOS header
    if (!InRdata(vt)) return false;
    uintptr_t m0 = 0, m1 = 0;
    if (!RdPtr((void*)vt, &m0) || !RdPtr((void*)(vt + 4), &m1)) return false;
    return InExec(m0) && InExec(m1);
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

static int KindCategory(const char* kind)
{
    // Tactical category from LIVE kind, not gid. 0x61 must never become boss.
    if (!kind) return -1;
    if (!strcmp(kind, "uEm0100") || !strcmp(kind, "uEm0101")) return 0;
    return -1;
}

static void PublishWorldFromActors()
{
    if (g_nAct && g_act[0].ptr)
        g_lastBand = g_act[0].ptr & ~0xFFFFFu;
    WorldReport w{};
    w.timestampMs = GetTickCount();
    w.dominantCategory = -1;
    int best = -1;
    for (int i = 0; i < g_nAct && w.count < 32; ++i) {
        if (!g_act[i].ptr) continue;
        WorldPresence& p = w.units[w.count];
        p.ptr = g_act[i].ptr;
        p.vt = (uint32_t)g_act[i].vt;
        p.gid = g_act[i].gid;
        p.kind = g_act[i].kind ? g_act[i].kind : "?";
        p.x = g_act[i].x;
        p.y = g_act[i].y;
        p.z = g_act[i].z;
        p.fromScan = true;
        if (g_act[i].kind && (!strcmp(g_act[i].kind, "uEm0100")
            || !strcmp(g_act[i].kind, "uEm0101")))
            w.goblinCount++;
        int cat = KindCategory(g_act[i].kind);
        if (cat > best) best = cat;
        w.count++;
    }
    w.dominantCategory = best;
    CombatBus::Instance().PublishWorld(w);
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
static const uint32_t kActSlot = 0x2DC8;
static uint32_t g_actSlotOff  = 0;      // learned offset, sticky across ticks
static bool     g_actFullScan = false;  // set by HUNT, cleared after use

static const ActMap::Act* ActAt(uintptr_t body, uint32_t off, uintptr_t* outPtr, uint32_t* outRva)
{
    uintptr_t cand = 0;
    if (!RdPtr((void*)(body + off), &cand)) return nullptr;
    if (!LooksHeap(cand)) return nullptr;
    uintptr_t vt = 0;
    if (!RdPtr((void*)cand, &vt)) return nullptr;
    if (!InImage(vt)) return nullptr;
    uint32_t rva = (uint32_t)(vt - g_base);
    const ActMap::Act* a = ActMap::FindByVt(rva);
    if (!a) return nullptr;
    if (outPtr) *outPtr = cand;
    if (outRva) *outRva = rva;
    return a;
}

// Zip 34 — resolve a live object's class name without the atlas.
// MT Framework: every MtObject's vtable has GetDTI() early; the DTI card in
// .data holds a char* name at +4 (Zip 14 note). So: object -> vtable -> scan
// the first slots for a function that returns a .data pointer whose +4 is an
// ASCII class name. Cheaper and exact vs. extrapolating the 0x48 lattice.
static bool ReadCStr(uintptr_t va, char* out, int cap)
{
    if (!va || !InImage(va)) return false;
    for (int i = 0; i < cap - 1; ++i) {
        BYTE c = 0;
        if (!Rd((void*)(va + i), &c, 1)) return false;
        if (c == 0) { out[i] = 0; return i > 2; }
        if (c < 0x20 || c > 0x7E) return false;
        out[i] = (char)c;
    }
    out[cap - 1] = 0;
    return true;
}

// A DTI card: [0] = MtDTI vtable (in .rdata), [4] = char* name (in image).
static bool NameFromDti(uintptr_t dti, char* out, int cap)
{
    if (!dti || !InImage(dti)) return false;
    uintptr_t np = 0;
    if (!RdPtr((void*)(dti + 4), &np)) return false;
    return ReadCStr(np, out, cap);
}

// Find the DTI for a live object by scanning its vtable for `mov eax, imm32;
// ret` (B8 imm32 C3) — that is GetDTI in this build.
static uintptr_t DtiOfObject(uintptr_t obj)
{
    uintptr_t vt = 0;
    if (!RdPtr((void*)obj, &vt) || !InImage(vt)) return 0;
    for (int slot = 0; slot < 12; ++slot) {
        uintptr_t fn = 0;
        if (!RdPtr((void*)(vt + slot * 4), &fn)) break;
        if (!fn || !InExec(fn)) break;
        BYTE code[8];
        if (!Rd((void*)fn, code, sizeof(code))) continue;
        int at = -1;
        if (code[0] == 0xB8 && code[5] == 0xC3) at = 1;          // mov eax,imm; ret
        else if (code[0] == 0xB8 && code[5] == 0xC2) at = 1;     // ret n
        if (at < 0) continue;
        uintptr_t imm = *(uint32_t*)(code + at);
        char probe[40];
        if (NameFromDti(imm, probe, sizeof(probe))) return imm;
    }
    return 0;
}

static bool NameOfLiveObject(uintptr_t obj, char* out, int cap)
{
    uintptr_t dti = DtiOfObject(obj);
    if (!dti) { out[0] = 0; return false; }
    return NameFromDti(dti, out, cap);
}

static void ScanActSlot(ActorDump& A)
{
    A.actOff = 0; A.actPtr = 0; A.actVtRva = 0;
    A.actName = 0; A.actCat = 0; A.actHits = 0;
    A.actOff2 = 0; A.actName2 = 0;
    A.nRaw = 0;
    if (!g_base) return;

    // Fast path: we already know where it lives.
    if (!g_actFullScan && g_actSlotOff) {
        uintptr_t ptr = 0; uint32_t rva = 0;
        if (const ActMap::Act* a = ActAt(A.ptr, g_actSlotOff, &ptr, &rva)) {
            A.actOff = g_actSlotOff; A.actPtr = ptr; A.actVtRva = rva;
            A.actName = a->name; A.actCat = a->category; A.actHits = 1;
        }
        return;
    }

    // Full search. Copy the body first: 8 guarded reads instead of thousands.
    static BYTE  buf[0x7400];
    static bool  ok[0x7400 / 0x1000 + 1];
    const uint32_t kEnd = 0x7400, kChunk = 0x1000;
    for (uint32_t c = 0, off = 0; off < kEnd; ++c, off += kChunk) {
        uint32_t n = (off + kChunk <= kEnd) ? kChunk : (kEnd - off);
        ok[c] = Rd((void*)(A.ptr + off), buf + off, n);
    }

    for (uint32_t off = 0x100; off + 4 <= kEnd; off += 4) {
        if (!ok[off / kChunk]) continue;
        uintptr_t cand = *(uintptr_t*)(buf + off);
        if (!LooksHeap(cand)) continue;
        uintptr_t vt = 0;
        if (!RdPtr((void*)cand, &vt) || !InImage(vt)) continue;
        uint32_t rva = (uint32_t)(vt - g_base);

        // Zip 33: harvest every real vtable-bearing object, unfiltered.
        // LooksLikeVtable = lives in .rdata and its first two slots point
        // into .text — that is a genuine C++ object, Act or not.
        if (A.nRaw < 40 && LooksLikeVtable(vt)) {
            int dup = 0;
            for (int r = 0; r < A.nRaw; ++r)
                if (A.rawVt[r] == (uint32_t)vt) { dup = 1; break; }
            if (!dup) {
                A.rawOff[A.nRaw] = off;
                A.rawVt[A.nRaw]  = (uint32_t)vt;
                A.rawPtr[A.nRaw] = (uint32_t)cand;
                // Zip 34: ask the object its own name. Atlas not involved.
                if (!NameOfLiveObject(cand, A.rawName[A.nRaw], 40))
                    A.rawName[A.nRaw][0] = 0;
                A.nRaw++;
            }
        }

        const ActMap::Act* a = ActMap::FindByVt(rva);
        if (!a) continue;

        A.actHits++;
        if (!A.actPtr) {
            A.actOff = off; A.actPtr = cand; A.actVtRva = rva;
            A.actName = a->name; A.actCat = a->category;
        } else if (!A.actOff2) {
            A.actOff2 = off; A.actName2 = a->name;
        }
    }
    if (A.actOff) g_actSlotOff = A.actOff;   // remember for the cheap path
}

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

static void DumpActorsFrom(uintptr_t* seed, int ns)
{
    g_nAct = 0;
    if (!seed || ns <= 0) return;
    if (ns > 32) ns = 32;
    for (int s = 0; s < ns && g_nAct < 32; ++s) {
        uintptr_t p = seed[s];
        if (!p || !LooksHeap(p)) continue;
        int have = 0;
        for (int k = 0; k < g_nAct; ++k) if (g_act[k].ptr == p) { have = 1; break; }
        if (have) continue;
        uintptr_t vt = 0;
        if (!RdPtr((void*)p, &vt) || !LooksLikeVtable(vt)) continue;
        ActorDump& A = g_act[g_nAct];
        memset(&A, 0, sizeof(A));
        A.ptr = p;
        A.vt = vt;
        BYTE gidb = 0;
        if (Rd((void*)(p + 0x2D), &gidb, 1)) A.gid = gidb;
        Rd((void*)(p + 0x40), &A.x, 4);
        Rd((void*)(p + 0x44), &A.y, 4);
        Rd((void*)(p + 0x48), &A.z, 4);
        RdPtr((void*)(p + 0x0C), &A.next);
        RdPtr((void*)(p + 0x10), &A.prev);
        if (A.vt == kGoblinInst)
            A.subOk = RdPtr((void*)(p + 0x6150), &A.subVt);
        BYTE probe = 0;
        A.fat29 = Rd((void*)(p + 0x73BF), &probe, 1);
        { BYTE st = 0; if (Rd((void*)(p + 0x14), &st, 1)) A.st14 = st; }
        A.win5bOk = Rd((void*)(p + 0x5BD0), A.win5b, 16);
        A.win60Ok = Rd((void*)(p + 0x6000), A.win60, 64);
        ScanActSlot(A);
        if (A.vt == kGoblinInst) A.kind = "uEm0100";
        else if (A.vt == kNpcInst) A.kind = "uNpc";
        else if (A.vt == kEm8000Inst) A.kind = "uEm8000";
        else if (A.vt == kUnk84Inst) A.kind = "u?84";
        else if (A.vt == kHareInst) A.kind = "uEm8600";
        else A.kind = "?";
        g_nAct++;
        if (A.next && LooksHeap(A.next) && ns < 32) {
            int d = 0;
            for (int k = 0; k < ns; ++k) if (seed[k] == A.next) { d = 1; break; }
            if (!d) seed[ns++] = A.next;
        }
        if (A.prev && LooksHeap(A.prev) && ns < 32) {
            int d = 0;
            for (int k = 0; k < ns; ++k) if (seed[k] == A.prev) { d = 1; break; }
            if (!d) seed[ns++] = A.prev;
        }
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

static void RewalkActors()
{
    uintptr_t seed[32];
    int ns = 0;
    for (int i = 0; i < g_nAct && ns < 32; ++i)
        if (g_act[i].ptr) seed[ns++] = g_act[i].ptr;
    if (!ns) return;
    DumpActorsFrom(seed, ns);
    PublishWorldFromActors();
}

static int IsSeedVt(uint32_t val)
{
    return val == (uint32_t)kGoblinInst || val == (uint32_t)kEm8000Inst
        || val == (uint32_t)kNpcInst || val == (uint32_t)kUnk84Inst
        || val == (uint32_t)kHareInst;
}

static uintptr_t PollSeedSlice(uint32_t budget)
{
    // Hot ring only. dump18-23 actors are 0x10DD..0x114F. Walking to 0x40000000
    // skipped the classic band for ~30s (dump23 pack of 3).
    if (!g_nExec) InitSections();
    if (!budget) budget = 0x800000u;
    const uint32_t kBudget = budget;
    uint32_t used = 0;
    int steps = 0;
    if (g_pollAddr < kHotLo || g_pollAddr >= kHotHi)
        g_pollAddr = kHotLo;
    while (used < kBudget && steps < 64) {
        steps++;
        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof(mbi));
        SIZE_T got = VirtualQuery((LPCVOID)g_pollAddr, &mbi, sizeof(mbi));
        if (!got) { g_pollAddr = kHotLo; break; }
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        uintptr_t next = base + mbi.RegionSize;
        if (next <= g_pollAddr) { g_pollAddr = kHotLo; break; }
        DWORD prot = mbi.Protect & 0xFF;
        bool readable = prot == PAGE_READONLY || prot == PAGE_READWRITE
                     || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ
                     || prot == PAGE_EXECUTE_READWRITE;
        bool skip = mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE
                 || !readable || (mbi.Protect & PAGE_GUARD)
                 || next <= kHotLo || base >= kHotHi
                 || (g_base && base < ImageEnd() && next > g_base);
        if (skip) { g_pollAddr = (next >= kHotHi) ? kHotLo : next; continue; }
        uintptr_t lo = g_pollAddr > base ? g_pollAddr : base;
        if (lo < kHotLo) lo = kHotLo;
        uintptr_t hi = next;
        if (hi > kHotHi) hi = kHotHi;
        if (hi <= lo) { g_pollAddr = (next >= kHotHi) ? kHotLo : next; continue; }
        uint32_t span = (uint32_t)(hi - lo);
        if (span > kBudget - used) span = kBudget - used;
        hi = lo + span;
        __try {
            uint32_t* p32 = (uint32_t*)lo;
            uint32_t n = span / 4;
            for (uint32_t i = 0; i < n; ++i) {
                uintptr_t obj = lo + (uintptr_t)i * 4;
                if (obj & 7) continue;
                uint32_t val = p32[i];
                if (!IsSeedVt(val)) continue;
                if (!LooksLikeVtable((uintptr_t)val)) continue;
                BYTE probe = 0;
                if (!Rd((void*)(obj + 0x2D), &probe, 1)) continue;
                g_pollAddr = obj + 8;
                return obj;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        used += span;
        g_pollAddr = hi;
        if (g_pollAddr >= kHotHi) { g_pollAddr = kHotLo; break; }
        if (hi < next) break;
    }
    return 0;
}

void DevTools::WorldScan_Tick()
{
    // Presence only. Engine keeps uEm* on the list and on screen far
    // past the spawn sphere. World=0 means the 29KB body is gone.
    // Do not invent a distance despawn.
    if (!g_enabled) return;
    if (!InWorld()) return;
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (last && now - last < 150) return;
    last = now;
    if (g_nAct)
        RewalkActors();
    // Always poll the hot ring. Empty: 8MB/tick. Have list: 4MB, merge new camps.
    uintptr_t s = PollSeedSlice(g_nAct ? 0x400000u : 0x800000u);
    if (!s) return;
    int have = 0;
    for (int i = 0; i < g_nAct; ++i)
        if (g_act[i].ptr == s) { have = 1; break; }
    if (have) return;
    uintptr_t seed[32];
    int ns = 0;
    seed[ns++] = s;
    for (int i = 0; i < g_nAct && ns < 32; ++i)
        if (g_act[i].ptr) seed[ns++] = g_act[i].ptr;
    DumpActorsFrom(seed, ns);
    PublishWorldFromActors();
}

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

    DWORD t0 = GetTickCount();
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
    g_huntMs = GetTickCount() - t0;
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
        const WorldReport& wr = CombatBus::Instance().LastWorld();
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

    DWORD t0 = GetTickCount();
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
    g_huntMs = GetTickCount() - t0;
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

    DWORD t0 = GetTickCount();

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

    g_dumpMs = GetTickCount() - t0;
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
    if (!ImGui::CollapsingHeader("DevTools — Type Atlas")) return;
    ImGui::PushID("DT");

    ImGui::Text("exe base 0x%08X   image end 0x%08X", (unsigned)g_base, (unsigned)ImageEnd());
    ImGui::TextWrapped(
        "Factory slots = dead TSV column, always empty. "
        "DUMP is safe anatomy. HUNT (in-world only) derives instance vts and censuses the heap.");

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
                ImGui::TextDisabled("%-24s  shared stub vtable — skip", s.t->name);
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
            if (!g_nHist) ImGui::TextDisabled("nothing identified — paste the json anyway");
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
            if (!g_nHunts) ImGui::TextDisabled("none — managers live behind another pointer");
            for (int i = 0; i < g_nHunts; ++i) {
                char lab[80];
                sprintf_s(lab, "%-22s inst %08X##h%d",
                    g_hunts[i].name, (unsigned)g_hunts[i].instance, i);
                if (ImGui::SmallButton(lab)) SetInspect(g_hunts[i].instance);
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Heap managers (vtable on MEM_PRIVATE)")) {
            if (!g_nMgrs) ImGui::TextDisabled("none — sEnemyManager etc. not on the private heap");
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
            const WorldReport& wr = CombatBus::Instance().LastWorld();
            DWORD now = GetTickCount();
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
                ImGui::TextDisabled("list empty — polling hot heap 0x10000000-0x18000000, 8MB/tick.");
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
    ImGui::TextDisabled("Type catalog — 4405 names. Not live objects.");
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

void Hooks::DevTools()
{
    // Дефолт OFF: игроку DevTools не нужен, а WorldScan_Tick стоит 150 мс-обхода.
    // Для разработки включается в ddda_ai_overhaul.ini: [devtools] enabled = on
    g_enabled = config.getBool("devtools", "enabled", false);
    g_base = (uintptr_t)GetModuleHandle(nullptr);

    if (g_base) {
        auto dos = (IMAGE_DOS_HEADER*)g_base;
        auto nt  = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
        g_imageSize = nt->OptionalHeader.SizeOfImage;
    }

    if (!g_enabled) {
        logFile << "DevTools: disabled" << std::endl;
        return;
    }

    BuildWatch();

    logFile << "DevTools: TypeAtlas " << TypeAtlas::kCount
            << "  watch=" << g_nWatch
            << "  base=0x" << std::hex << g_base
            << "  imageEnd=0x" << (g_base + g_imageSize) << std::dec
            << "  (in-world: DUMP anatomy, then HUNT)"
            << std::endl;

    InGameUIAdd(RenderDevToolsUI);
}

// Runtime::Mem — реализация. См. MemProbe.h.

#include "stdafx.h"
#include "MemProbe.h"

namespace Runtime {
namespace Mem {

uintptr_t g_base = 0;
uint32_t g_imageSize = 0;
SecRange  g_exec[8];
int       g_nExec = 0;
SecRange  g_rdata[8];
int       g_nRdata = 0;

// ВНИМАНИЕ: здесь БОЛЬШЕ НЕТ IsBadReadPtr, и это принципиально.
//
// Вылет 19.08 на глубоком обходе объектов пешки объяснился именно им.
// IsBadReadPtr ЧИТАЕТ проверяемую страницу, а среди адресов, которые
// проходят наш фильтр LooksHeap, попадаются страницы-сторожа стеков
// чужих потоков (PAGE_GUARD). Обращение к такой странице «съедает»
// сторожа: исключение перехватывается, но страница остаётся без флага,
// и поток, чей это был стек, падает позже — например, на загрузке
// сохранения. Ровно эта картина и наблюдалась.
//
// MSDN прямо называет функцию устаревшей и указывает на ту же проблему.
// SEH ловит недоступную память сам, без предварительного чтения.
bool Rd(const void* p, void* out, size_t n)
{
    if (!p || !out || !n) return false;
    __try { memcpy(out, p, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Можно ли вообще трогать этот участок.
//
// VirtualQuery смотрит на таблицы страниц и САМУ ПАМЯТЬ НЕ ЧИТАЕТ,
// поэтому сторожевые страницы остаются целы. Для точечного чтения
// достаточно SEH, но для СПЛОШНОГО обхода мегабайтов проверка
// обязательна: иначе мы гарантированно наступим на чей-нибудь стек.
bool RegionOk(uintptr_t addr, size_t bytes)
{
    if (!addr || !bytes) return false;
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;

    const DWORD bad = PAGE_NOACCESS | PAGE_GUARD;
    if (mbi.Protect & bad) return false;
    const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                   | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                   | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & ok)) return false;

    // Участок должен целиком лежать в одном регионе: иначе следующая
    // страница может оказаться сторожевой.
    const uintptr_t regEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    return (addr + bytes) <= regEnd;
}

bool RdPtr(const void* p, uintptr_t* out)
{ return Rd(p, out, sizeof(uintptr_t)); }

bool WrSafe(void* p, const void* value, size_t n)
{
    if (!p || !value || !n) return false;
    __try { memcpy(p, value, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t ImageEnd()
{ return g_base + g_imageSize; }

bool InImage(uintptr_t a)
{
    return g_base && a >= g_base && a < ImageEnd();
}

bool LooksHeap(uintptr_t a)
{
    // 4-byte aligned user pointers only. Odd values are flags/packed ints.
    // Low 256MB page-round numbers (0x08000000, 0x08040000) were false hits.
    if (a < 0x01000000 || a >= 0x80000000 || (a & 3) || InImage(a)) return false;
    if (a < 0x10000000 && (a & 0xFFFF) == 0) return false;
    return true;
}

// Save-layer check. Same idea as CombatIntel::IsInActiveGameplay.
// HUNT must not run on the title screen or mid-load.
bool InWorld()
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

void InitSections()
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

bool InExec(uintptr_t a)
{
    for (int i = 0; i < g_nExec; ++i)
        if (a >= g_exec[i].lo && a < g_exec[i].hi) return true;
    return false;
}

bool InRdata(uintptr_t a)
{
    for (int i = 0; i < g_nRdata; ++i)
        if (a >= g_rdata[i].lo && a < g_rdata[i].hi) return true;
    return false;
}

// Vtable lives in rdata; first slots are methods in .text.
// InImage alone is too weak: 0x400000 (MZ) and patterned dwords leaked into census.
bool LooksLikeVtable(uintptr_t vt)
{
    if (!vt || (vt & 3) || InExec(vt)) return false;
    if (g_base && vt < g_base + 0x1000) return false; // DOS header
    if (!InRdata(vt)) return false;
    uintptr_t m0 = 0, m1 = 0;
    if (!RdPtr((void*)vt, &m0) || !RdPtr((void*)(vt + 4), &m1)) return false;
    return InExec(m0) && InExec(m1);
}

// Zip 34 — resolve a live object's class name without the atlas.
// MT Framework: every MtObject's vtable has GetDTI() early; the DTI card in
// .data holds a char* name at +4 (Zip 14 note). So: object -> vtable -> scan
// the first slots for a function that returns a .data pointer whose +4 is an
// ASCII class name. Cheaper and exact vs. extrapolating the 0x48 lattice.
bool ReadCStr(uintptr_t va, char* out, int cap)
{
    if (!va || !InImage(va) || cap < 2) return false;

    // Build 69.2: быстрый путь — ОДНО защищённое чтение блоком.
    // Раньше имя читалось побайтно: на каждый байт свой IsBadReadPtr + SEH,
    // до 40 вызовов ради одной строки. На горячем пути (имя класса каждого
    // актёра каждый тик) это была основная цена скана.
    const int want = (cap - 1 < 64) ? cap - 1 : 64;
    BYTE buf[64];
    if (Rd((void*)va, buf, (size_t)want)) {
        for (int i = 0; i < want; ++i) {
            BYTE c = buf[i];
            if (c == 0) { out[i] = 0; return i > 2; }
            if (c < 0x20 || c > 0x7E) return false;
            out[i] = (char)c;
        }
        out[want] = 0;
        return want >= cap - 1;
    }

    // Медленный путь: блок упёрся в конец страницы. Дочитываем побайтно —
    // ровно как раньше, поэтому поведение на краю секции не изменилось.
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
bool NameFromDti(uintptr_t dti, char* out, int cap)
{
    if (!dti || !InImage(dti)) return false;
    uintptr_t np = 0;
    if (!RdPtr((void*)(dti + 4), &np)) return false;
    return ReadCStr(np, out, cap);
}

// Find the DTI for a live object by scanning its vtable for `mov eax, imm32;
// ret` (B8 imm32 C3) — that is GetDTI in this build.
uintptr_t DtiOfObject(uintptr_t obj)
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

// ---------------------------------------------------------------------------
// Кэш vtable -> имя класса.
//
// ЗАЧЕМ. NameOfLiveObject звался на каждого актёра каждый тик, а внутри —
// DtiOfObject: до 12 слотов vtable, на каждый защищённое чтение указателя
// и восьми байт кода, плюс разбор строки имени. При 32 актёрах это тысячи
// SEH-защищённых чтений в секунду ради данных, которые НЕ МЕНЯЮТСЯ:
// таблица виртуальных функций и имя класса неизменны на весь процесс.
//
// Кэш прямого отображения: коллизия просто вытесняет запись, корректность
// обеспечивается сравнением самого vt. Отрицательный результат тоже
// кэшируется (пустое имя) — иначе неопознанные объекты резолвились бы заново
// каждый тик, а это как раз худший случай (полный обход 12 слотов впустую).
// ---------------------------------------------------------------------------
struct NameCacheEntry { uintptr_t vt; char name[48]; };
// 512 слотов на 511 известных vtable давали толкотню: две горячие записи
// в одном слоте вытесняли друг друга, и попадания падали до 93%.
// Больше слотов + мультипликативный хеш (числа Кнута) вместо сдвига:
// младшие биты указателей на vtable распределены неравномерно.
static const int    kNameCacheSize = 4096;         // степень двойки
static NameCacheEntry g_nameCache[kNameCacheSize];
static uint32_t     g_nameCacheHits = 0;
static uint32_t     g_nameCacheMisses = 0;
static uint32_t     g_nameCacheEntries = 0;

NameCacheStats NameCacheGetStats()
{
    NameCacheStats s;
    s.hits = g_nameCacheHits;
    s.misses = g_nameCacheMisses;
    s.entries = g_nameCacheEntries;
    return s;
}

void NameCacheReset()
{
    memset(g_nameCache, 0, sizeof(g_nameCache));
    g_nameCacheHits = g_nameCacheMisses = g_nameCacheEntries = 0;
}

bool NameOfLiveObject(uintptr_t obj, char* out, int cap)
{
    if (!out || cap < 2) return false;
    out[0] = 0;

    uintptr_t vt = 0;
    if (!RdPtr((void*)obj, &vt) || !InImage(vt)) return false;

    const uint32_t h = (uint32_t)((vt * 2654435761u) >> 11);
    NameCacheEntry& e = g_nameCache[h & (kNameCacheSize - 1)];
    if (e.vt == vt) {
        ++g_nameCacheHits;
        if (!e.name[0]) return false;          // отрицательный результат
        lstrcpynA(out, e.name, cap);
        return true;
    }

    ++g_nameCacheMisses;
    char tmp[48] = {};
    uintptr_t dti = DtiOfObject(obj);
    const bool ok = dti && NameFromDti(dti, tmp, sizeof(tmp));

    if (!e.vt) ++g_nameCacheEntries;
    e.vt = vt;
    lstrcpynA(e.name, ok ? tmp : "", sizeof(e.name));
    if (!ok) return false;

    lstrcpynA(out, tmp, cap);
    return true;
}

const char* NameOfLiveObjectSafe(const void* obj, char* out, int cap)
{
    if (!obj || !out || cap < 2) return nullptr;
    out[0] = 0;
    if (!NameOfLiveObject((uintptr_t)obj, out, cap)) return nullptr;
    return out[0] ? out : nullptr;
}

// Build 69: базу образа снимает рантайм, а не DevTools. Раньше это
// делал Hooks::DevTools(), который при [devtools] enabled = off
// выходил раньше — и продукт оставался без g_base.
void Init()
{
    g_base = (uintptr_t)GetModuleHandle(nullptr);
    if (g_base) {
        auto dos = (IMAGE_DOS_HEADER*)g_base;
        auto nt  = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
        g_imageSize = nt->OptionalHeader.SizeOfImage;
    }
    InitSections();
}

} // namespace Mem
} // namespace Runtime

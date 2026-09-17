/**
 * AudioRedirect — музыкальный слой 0 (docs/AUDIO_MUSIC_RECON.md).
 *
 *   R1 (умолчание): log_requests — журнал обращений к *.sngw;
 *                   probe_stq — разовый поиск STRQ-образов в памяти
 *                   (статус полей ответит, «сырой ресурс или распарсен»).
 *   R2 (enabled=1):  подмена файла + транзакционный патч 4 полей записи
 *                   (size/samples/loopIn/loopOut) + readback + WATCH.
 *
 * Патчится ТОЛЬКО запись, валидированная магией STRQ и точным именем
 * (по FIX_RULES: неизвестное состояние → деградируем в ванилу).
 * Сейв, файлы игры и ARC не трогаются вообще.
 */

#include "stdafx.h"
#include "audio/AudioRedirect.h"
#include "ModPaths.h"
#include "MinHook/MinHook.h"
#include "runtime/LogMem.h"
#include "BuildTag.h"

#include <thread>
#include <cstdlib>
#include <unordered_map>
#include <random>
#include <ctime>
#include <emmintrin.h>
#include <intrin.h>

// ---------------------------------------------------------------- state

typedef HANDLE(WINAPI* CreateFileW_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileW_t oCreateFileW = nullptr;
static CreateFileA_t oCreateFileA = nullptr;

struct Pool { std::vector<string> files; };   // относительные пути от music\

static std::unordered_map<string, Pool> g_map; // key: "bgm\wave2\tittle_ddn" (lower)
struct LoopRange { unsigned loopIn = 0, loopOut = 0; bool set = false; };
static std::unordered_map<string, LoopRange> g_loops; // [music.loops] key = loopIn, loopOut (сэмплы)

// ========== R3: оффлайн-каталог якорей (из bgm.stq / Tittle_bgm.stq) ==========
// exact u32 полей записей, сняты инструментом оффлайн. 0xFFFFFFFF = без лупа
// (важно: треки без лупа совершенно реальны в игре — их семантика священна).
struct TrackMeta {
    const char* name;                                     // как в STRQ
    unsigned size, samples, ch, loopIn, loopOut;
};
static const TrackMeta kCatalog[] = {
    { "bgm\\wave2\\Tittle_DDN",     5327566,   4199510, 6,  2734966,  4078967 },
    { "bgm\\wave\\gamestart",        230915,    192320, 6, 0xFFFFFFFFu, 0xFFFFFFFFu },
    { "bgm\\wave\\ShuuzinCycrops",   8228472,   6366549, 6,   644896,  6042368 },
    { "bgm\\wave\\Gazer",            7945023,   6085580, 6,  1168490,  5869827 },
    { "bgm\\wave\\Demon_1",         13935922,  10728969, 6,  2611344, 10657008 },
    { "bgm\\wave\\Lich_DragonZonbi", 7705703,   5309970, 6,   153927,  5194014 },
    { "bgm\\wave\\Shinigami_Battle", 7011892,   5552172, 6,   610051,  5403039 },
    { "bgm\\wave\\Shitaiwaki",       7140759,   5736951, 6,  1329033,  5553161 },
    { "bgm\\wave\\Upper_ZakoDragon", 4674913,   4267340, 6,    81024,  4185600 },
    { "bgm\\wave\\DD_Kasou",         8438442,   6251520, 6,  1249096,  5953180 },
    { "bgm\\wave\\DD_Entrance",      3595978,   3058272, 6, 0xFFFFFFFFu, 0xFFFFFFFFu },
    { "bgm\\wave\\Zako_1",           7089027,   5280000, 6,   240000,  5136000 },
    { "bgm\\wave\\Zako_2",           7935719,   5280000, 6,   240000,  5136000 },
    { "bgm\\wave\\Zako_3",           7405703,   5280000, 6,   240000,  5136000 },
    { "bgm\\wave\\No_018",           2267747,   2649600, 6, 0xFFFFFFFFu, 0xFFFFFFFFu },
    { "bgm\\wave\\No_046",           8052810,   7085568, 6,   238454,  7014924 },
};
static const size_t kCatalogCount = sizeof(kCatalog) / sizeof(kCatalog[0]);

struct FoundRec { BYTE* table = nullptr; BYTE* entry = nullptr; const TrackMeta* meta = nullptr; };
static std::unordered_map<string, FoundRec> g_found;  // key = имя трека (lower)

// R3-2: то, что патчили этой сессией (для фоновых добивок)
struct ActiveTrack {
    string key;
    const TrackMeta* meta;
    unsigned newSize = 0, newSamples = 0, lin = 0, lout = 0;
};
static std::vector<ActiveTrack> g_tracks;
struct PatchRecT { BYTE* addr; unsigned oldV, newV; }; // (PatchRec используем общий ниже)
static volatile bool g_dying = false;  // 84.84: процесс умирает — хуки сквозные, потоки выходят
static std::mt19937 g_rng;               // 84.89: srand+rand давал коррелированный первый выбор
static bool  g_enabled     = false; // [music] enabled  (R2)
static bool  g_logRequests = true;  // [music] log_requests (R1 журнал)
static bool  g_probeStq    = true;  // [music] probe_stq (R1 пробник)
static bool  g_strikeLoops = true;  // [music] strike_loops: бить живые копии лупов (0 — только STRQ-патч)
static bool  g_strikeCoherence = true; // [music] strike_coherence: требовать соседний size/samples
static LONG  g_probeDone   = 0;
static DWORD g_probeLastEnd = 0; // минимум 5 c между проходами (пробник перезапускаем)     // one-shot флаг пробника

// ---------------------------------------------------------------- utils

static void ToLower(string& s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 32;
}
static string Trim(string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == string::npos ? string() : s.substr(a, b - a + 1);
}
static string WideToUtf8(LPCWSTR w) {
    if (!w) return string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return string();
    string r(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &r[0], n, nullptr, nullptr);
    return r;
}
static std::wstring Utf8ToWidePath(const string& u) { // wide-string для CreateFileW
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, &w[0], n);
    return w;
}

// Из абсолютного/относительного пути достаём ключ таблицы (нижний регистр,
// без расширения): "…\sound\stream\bgm\wave2\Tittle_DDN.sngw" → "bgm\wave2\tittle_ddn"
static bool ExtractBgmKey(const string& pathUtf8, string& keyOut)
{
    string p = pathUtf8;
    std::replace(p.begin(), p.end(), '/', '\\');
    string low = p; ToLower(low);
    if (low.find(".sngw") == string::npos) return false;
    size_t m = low.rfind("sound\\stream\\");
    size_t start;
    if (m != string::npos) start = m + lstrlenA("sound\\stream\\");
    else {
        m = low.rfind("bgm\\wave");
        if (m == string::npos) return false;
        start = m;
    }
    string tail = p.substr(start);
    size_t dot = tail.size() >= 5 ? tail.size() - 5 : 0;
    tail.resize(dot); // срезать ".sngw"
    ToLower(tail);
    keyOut = tail;
    return true;
}

// ---------------------------------------------------------------- music map

static void LoadMap()
{
    string path = string(ModPaths::Dir()) + "\\music\\ddda_music_map.ini";
    std::ifstream f(path.c_str());
    if (!f) {
        logFile << "AudioR: карта не найдена (" << path
                << ") — R1-режим умолчаний (log+probe)" << std::endl;
    }
    string line, section;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { section = Trim(line.substr(1, line.size() - 2)); ToLower(section); continue; }
        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = Trim(line.substr(0, eq));
        string val = Trim(line.substr(eq + 1));
        if (section == "music") {
            ToLower(key);
            if (key == "enabled")      g_enabled     = atoi(val.c_str()) != 0;
            if (key == "log_requests") g_logRequests = atoi(val.c_str()) != 0;
            if (key == "probe_stq")    g_probeStq    = atoi(val.c_str()) != 0;
            if (key == "strike_loops") g_strikeLoops = atoi(val.c_str()) != 0;
            if (key == "strike_coherence") g_strikeCoherence = atoi(val.c_str()) != 0;
        }
        else if (section == "music.loops") {
            ToLower(key);
            size_t comma = val.find(',');
            if (comma != string::npos) {
                LoopRange lr;
                lr.loopIn  = (unsigned)strtoul(Trim(val.substr(0, comma)).c_str(), nullptr, 0);
                lr.loopOut = (unsigned)strtoul(Trim(val.substr(comma + 1)).c_str(), nullptr, 0);
                lr.set = true;
                g_loops[key] = lr;
                logFile << "AudioR: loops " << key << " = " << lr.loopIn
                        << "," << lr.loopOut << std::endl;
            }
        }
        else if (section == "music.map") {
            ToLower(key);
            Pool pool;
            size_t s = 0;
            while (true) {
                size_t bar = val.find('|', s);
                string one = Trim(bar == string::npos ? val.substr(s) : val.substr(s, bar - s));
                if (!one.empty()) { std::replace(one.begin(), one.end(), '/', '\\'); pool.files.push_back(one); }
                if (bar == string::npos) break;
                s = bar + 1;
            }
            if (!pool.files.empty()) g_map[key] = pool;
        }
    }
    logFile << "AudioR: map keys=" << g_map.size()
            << " | enabled=" << g_enabled
            << " log=" << g_logRequests
            << " probe=" << g_probeStq << std::endl;
}

// ---------------------------------------------------------------- OGG probe

struct OggInfo {
    bool ok = false;
    unsigned rate = 0;
    int channels = 0;
    unsigned long long samples = 0;   // granule последней страницы
    unsigned long long fileSize = 0;
    long long loopStart = -2;         // 84.84: vorbis-теги; -2 = тега нет, -1 = без лупа
    long long loopEnd   = -2;
};

static bool ProbeOgg(const string& path, OggInfo& oi)
{
    CreateFileA_t cf = oCreateFileA ? oCreateFileA : ::CreateFileA;
    HANDLE h = cf(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        logFile << "AudioR: ogg не открывается: " << path << " err=" << GetLastError() << std::endl;
        return false;
    }
    DWORD sz = GetFileSize(h, nullptr);
    oi.fileSize = sz;
    if (sz == INVALID_FILE_SIZE || sz < 4096) { CloseHandle(h); return false; }

    // --- vorbis ident packet: \x01"vorbis" | u32 ver | u8 ch | u32 rate ---
    {
        std::vector<BYTE> head(sz < 65536 ? sz : 65536);
        DWORD rd = 0;
        ReadFile(h, &head[0], (DWORD)head.size(), &rd, nullptr);
        for (DWORD i = 6; i + 12 < rd; ++i)
            if (head[i] == 'v' && memcmp(&head[i], "vorbis", 6) == 0 && head[i - 1] == 1) {
                oi.channels = head[i + 10];
                oi.rate = *(DWORD*)&head[i + 11];
                break;
            }
        // 84.84: теги LOOPSTART/LOOPEND — луп-конфигурация, путешествующая в файле
        string headS((const char*)&head[0], rd);
        string low = headS;
        ToLower(low);
        size_t p;
        if ((p = low.find("loopstart=")) != string::npos)
            oi.loopStart = strtoll(headS.c_str() + p + 10, nullptr, 10);
        if ((p = low.find("loopend=")) != string::npos)
            oi.loopEnd = strtoll(headS.c_str() + p + 8, nullptr, 10);
        if (oi.loopStart != -2 || oi.loopEnd != -2)
            logFile << "AudioR: ogg теги LOOPSTART=" << oi.loopStart
                    << " LOOPEND=" << oi.loopEnd << std::endl;
    }
    // --- последняя страница: granule = всего сэмплов ---
    {
        DWORD back = sz < 65536 ? sz : 65536;
        std::vector<BYTE> tail(back);
        SetFilePointer(h, sz - back, nullptr, FILE_BEGIN);
        DWORD rd = 0;
        ReadFile(h, &tail[0], back, &rd, nullptr);
        for (int i = (int)rd - 26; i >= 0; --i)
            if (memcmp(&tail[i], "OggS", 4) == 0) {
                unsigned long long granule = *(unsigned long long*)&tail[i + 6];
                if (granule != 0xFFFFFFFFFFFFFFFFULL) { oi.samples = granule; break; }
            }
    }
    CloseHandle(h);
    oi.ok = oi.rate && oi.channels && oi.samples && oi.fileSize;
    return oi.ok;
}

// ---------------------------------------------------------------- STRQ memory probe (R1)
//
// Отчёт пробника пишется в отдельный файл (audio_probe.txt в папке мода),
// а не в общий logFile: строка "Tittle_DDN" встречается в памяти тысячами,
// 8-МиБ RAM-буфер общего лога был переполнен за один прогон (урок R1, 84.70).

struct StrqHit { BYTE* entry; BYTE* tableBase; int index; };
static std::vector<StrqHit> g_hits;      // все STRQ-образы с нашим якорем
static BYTE* g_titleEntry = nullptr;     // валидированная запись титла
static BYTE* g_titleBase  = nullptr;

static bool GuardRead32(const BYTE* p, unsigned& out) {
    __try { out = *(unsigned*)p; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool GuardStrEq(const BYTE* p, const char* s, size_t n) {
    __try { return memcmp(p, s, n) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// C2712: __try нельзя в функции с C++-деструкторами — выносим копию наверх
static bool GuardCopy(void* dst, const void* src, size_t len) {
    __try { memcpy(dst, src, len); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void ConsiderAnchoredAt(const BYTE* base, const BYTE* pos, std::ofstream& out)
{
    // якорь размера: поднимаемся до магии STRQ по сетке idx*24
    for (int idx = 0; idx < 128; ++idx) {
        const BYTE* entry = pos - 4;
        const BYTE* tb = entry - 0x3C - idx * 24;
        if (tb < base) break;
        unsigned magic;
        if (GuardRead32(tb, magic) && magic == 0x51525453u) { // "STRQ"
            unsigned namePtr = 0;
            GuardRead32(entry, namePtr);
            out << "STRQ@0x" << std::hex << (uintptr_t)tb << " entry#" << std::dec << idx
                << " @0x" << std::hex << (uintptr_t)entry << " namePtr=0x" << namePtr
                << std::dec << "\n";
            StrqHit h; h.entry = (BYTE*)entry; h.tableBase = (BYTE*)tb; h.index = idx;
            g_hits.push_back(h);
            if (namePtr && GuardStrEq(tb + namePtr, "bgm\\wave2\\Tittle_DDN", 21)) {
                g_titleEntry = (BYTE*)entry;
                g_titleBase = (BYTE*)tb;
                out << "TITLE entry CONFIRMED @0x" << std::hex << (uintptr_t)entry
                    << std::dec << " (raw STRQ in memory)" << "\n";
                logFile << "AudioProbe: TITLE entry CONFIRMED @" << (void*)entry << std::endl;
            }
            return;
        }
    }
}

static void ConsiderMagicAt(const BYTE* pos, std::ofstream& out)
{
    // R3: магия STRQ — распознаём ВСЕ записи каталога в этой таблице.
    unsigned cnt = 0;
    if (!GuardRead32(pos + 0x08, cnt) || cnt == 0 || cnt > 4096) return;
    if (g_found.size() >= kCatalogCount) return;
    const BYTE* tb = pos;
    unsigned additions = 0;
    for (unsigned i = 0; i < cnt && i < 300; ++i) {
        const BYTE* e = tb + 0x3C + i * 24;
        unsigned np = 0;
        if (!GuardRead32(e, np) || !np) continue;
        char nb[64] = { 0 };
        if (!GuardCopy(nb, tb + np, 63)) continue;
        nb[63] = 0;
        string low = nb;
        ToLower(low);
        for (size_t t = 0; t < kCatalogCount; ++t) {
            string kn = kCatalog[t].name;
            ToLower(kn);
            if (low != kn || g_found.count(low)) continue;
            FoundRec fr; fr.table = (BYTE*)tb; fr.entry = (BYTE*)e; fr.meta = &kCatalog[t];
            g_found[low] = fr;
            ++additions;
            out << "CATALOG " << nb << " @0x" << std::hex << (uintptr_t)e << std::dec << "\n";
            logFile << "AudioProbe: CATALOG " << nb << " @" << (void*)e << std::endl;
            if (low == "bgm\\wave2\\tittle_ddn") {
                g_titleEntry = (BYTE*)e; g_titleBase = (BYTE*)tb;
                logFile << "AudioProbe: TITLE entry CONFIRMED (via magic) @" << (void*)e << std::endl;
            }
            break;
        }
    }
    if (additions) {
        out << "table @0x" << std::hex << (uintptr_t)tb << " cnt=" << std::dec << cnt
            << " | найдено треков каталога: " << g_found.size() << "/" << kCatalogCount << "\n";
    }
    (void)cnt;
}

// 84.86: SSE2-скан - 16 байт за итерацию; счётчик строк Tittle_DDN выпилен
// (был лишним тормозом и давал 0 с трёх прогонов).
static void ScanRegion(const BYTE* base, size_t size, std::ofstream& out,
                       unsigned long long& pagesSkipped)
{
    static const unsigned ANCHOR = 5327566u; // байт-размер Tittle_DDN (R0)
    static const unsigned MAGIC  = 0x51525453u; // "STRQ"
    static unsigned char buf[4096 + 32]; // пробник однопоточен
    const __m128i va = _mm_set1_epi32((int)ANCHOR);
    const __m128i vm = _mm_set1_epi32((int)MAGIC);
    for (size_t off = 0; off < size && g_found.size() < kCatalogCount; off += 4096) {
        const BYTE* page = base + off;
        size_t span = size - off < 4096 ? size - off : 4096;
        if (!GuardCopy(buf, page, span)) { ++pagesSkipped; continue; }
        memset(buf + span, 0, 32);
        for (size_t i = 0; i < span && g_found.size() < kCatalogCount; i += 16) {
            __m128i blk = _mm_loadu_si128((const __m128i*)(buf + i));
            unsigned ma = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi32(blk, va));
            unsigned mm = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi32(blk, vm));
            while (ma && g_found.size() < kCatalogCount) {
                unsigned long idx; _BitScanForward(&idx, ma); ma &= ma - 1;
                ConsiderAnchoredAt(base, page + i + (idx >> 2) * 4, out);
            }
            while (mm && g_found.size() < kCatalogCount) {
                unsigned long idx; _BitScanForward(&idx, mm); mm &= mm - 1;
                ConsiderMagicAt(page + i + (idx >> 2) * 4, out);
            }
        }
    }
}

static void ProbeStqThread()
{
    Sleep(500); // дать title.arc доложиться
    if (g_dying) return;
    std::ofstream out((string(ModPaths::Dir()) + "\\audio_probe.txt").c_str());
    out << "AudioProbe R1 report (build " << MOD_BUILD_TAG << ")\n====\n";
    SYSTEM_INFO si; GetSystemInfo(&si);
    const BYTE* addr = (const BYTE*)si.lpMinimumApplicationAddress;
    const BYTE* max = (const BYTE*)si.lpMaximumApplicationAddress;
    unsigned long long scanned = 0, pagesSkipped = 0, skippedOther = 0, skippedGuard = 0, nextMark = 256ull << 20;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < max) {
        if (VirtualQuery(addr, &mbi, sizeof mbi) != sizeof mbi) break;
        addr += mbi.RegionSize;
        if (mbi.State != MEM_COMMIT) continue;
        // 84.94 CRITICAL: PAGE_GUARD нельзя даже читать — guard-страница стека
        // потока игры consumится ОДНИМ касанием; затем глубокая рекурсия
        // загрузчика сейва уходит в голое дно -> push-падение (окно 84.72-84.93).
        if (mbi.Protect & PAGE_GUARD) { skippedGuard += mbi.RegionSize; continue; }
        if (mbi.Type != MEM_PRIVATE) { skippedOther += mbi.RegionSize; continue; }
        DWORD pr = mbi.Protect & 0xFF;
        if (!(pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_WRITECOPY ||
              pr == PAGE_EXECUTE_READ || pr == PAGE_EXECUTE_READWRITE || pr == PAGE_EXECUTE_WRITECOPY))
            continue;
        if (g_found.size() >= kCatalogCount) break; // каталог собран — дальше незачем
        scanned += mbi.RegionSize;
        if (scanned >= nextMark) {
            out << "... progress " << (scanned >> 20) << " MB, hits=" << g_hits.size() << "\n";
            logFile << "AudioProbe: " << (scanned >> 20) << " MB..." << std::endl;
            nextMark += 256ull << 20;
        }
        ScanRegion((const BYTE*)mbi.BaseAddress, mbi.RegionSize, out, pagesSkipped);
        if (g_found.size() >= kCatalogCount) break; // каталог собран
    }
    out << "==== done. scanned=" << (scanned >> 20) << "MB"
        << " (private-only, otherSkip " << (skippedOther >> 20) << "MB, guardSkip " << (skippedGuard >> 10) << "KB)"
        << " hits=" << g_hits.size()
        << " titleEntry=0x" << std::hex << (uintptr_t)g_titleEntry << std::dec
        << " pagesSkipped=" << pagesSkipped
        << "\n";
    out << "catalog found: " << g_found.size() << "/" << kCatalogCount << "\n";
    out.close();
    g_probeLastEnd = GetTickCount();
    InterlockedExchange(&g_probeDone, 0); // перезапуск разрешён (таблицы движка догружаются позже)
    logFile << "AudioProbe: done. hits=" << g_hits.size()
            << " catalog=" << g_found.size() << "/" << kCatalogCount
            << " titleEntry=" << (void*)g_titleEntry
            << " (детали: audio_probe.txt)" << std::endl;
}

static void MaybeProbe()
{
    if (!g_probeStq) return;
    if (g_found.size() >= kCatalogCount) return;      // всё нашлось — больше не бегаем
    if (GetTickCount() - g_probeLastEnd < 5000) return; // не душим CPU на спам-открытиях
    if (InterlockedCompareExchange(&g_probeDone, 1, 0) == 0)
        std::thread(ProbeStqThread).detach();
}

// ---------------------------------------------------------------- patch txn (R2)

struct PatchRec { BYTE* addr; unsigned oldV, newV; };
static PatchRec g_patch[4];
static bool g_patched = false;                  // легаси-флаг для антиповтора WATCH (84.93: используется реестром)
static std::vector<PatchRec> g_tablePatch;      // сырые STRQ-поля (откат в Shutdown)
static std::vector<PatchRec> g_loopPatch;       // вторая очередь: живые копии
struct LoopVictim { BYTE* inA; BYTE* outA; };
static std::vector<LoopVictim> g_loopVictims;   // все найденные живые копии (для дедупа/наблюдателя)

// 84.93: разрешение луп-точек трека по старшинству:
//   1) ванильный "без лупа" (meta loopOut=0xFFFFFFFF) — священен;
//   2) [music.loops] — явный отладочный перебой;
//   3) теги файла LOOPSTART/LOOPEND (-1/-1 = без лупа);
//   4) умолчание — весь трек.
static void ResolveLoops(const TrackMeta& meta, const OggInfo& oi, const string& key,
                         unsigned& loopIn, unsigned& loopOut)
{
    if (meta.loopOut == 0xFFFFFFFFu) {
        loopIn = 0xFFFFFFFFu; loopOut = 0xFFFFFFFFu;
        return;
    }
    auto li = g_loops.find(key);
    if (li != g_loops.end() && li->second.set) {
        loopIn = li->second.loopIn; loopOut = li->second.loopOut;
        if (loopOut <= oi.samples && loopIn < loopOut) return;
        logFile << "AudioR: loops из ini вне диапазона (" << loopIn << ","
                << loopOut << " vs " << oi.samples << ") - дальше по старшинству" << std::endl;
    }
    if (oi.loopStart == -1 && oi.loopEnd == -1) {
        loopIn = 0xFFFFFFFFu; loopOut = 0xFFFFFFFFu;
        return;
    }
    if (oi.loopStart >= 0 && oi.loopEnd > oi.loopStart &&
        (unsigned long long)oi.loopEnd <= oi.samples) {
        loopIn = (unsigned)oi.loopStart; loopOut = (unsigned)oi.loopEnd;
        return;
    }
    if (oi.loopStart != -2 || oi.loopEnd != -2)
        logFile << "AudioR: LOOP-теги (" << oi.loopStart << "," << oi.loopEnd
                << ") в файле некорректны vs " << oi.samples
                << " smp - весь трек (защита от дурака)" << std::endl;
    loopIn = 0; loopOut = (unsigned)oi.samples;
}

static bool GuardWrite32(BYTE* p, unsigned v) {
    DWORD oldPr;
    if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &oldPr)) return false;
    __try { *(unsigned*)p = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) { VirtualProtect(p, 4, oldPr, &oldPr); return false; }
    VirtualProtect(p, 4, oldPr, &oldPr);
    unsigned chk;
    return GuardRead32(p, chk) && chk == v;
}

static void PatchWatch() {
    Sleep(2500);
    if (g_dying) return;
    int drift = 0;
    size_t n = g_tablePatch.size() < 200 ? g_tablePatch.size() : 200;
    for (size_t i = 0; i < n; ++i) {
        unsigned v = 0;
        GuardRead32(g_tablePatch[i].addr, v);
        if (v != g_tablePatch[i].newV) {
            ++drift;
            if (drift <= 4)
                logFile << "AudioR: WATCH drift @" << (void*)g_tablePatch[i].addr
                        << " want=" << g_tablePatch[i].newV << " got=" << v << std::endl;
        }
    }
    logFile << "AudioR: WATCH " << (drift == 0 ? "ok (таблицы держатся 2.5c)" : "DRIFT — движок переписал!")
            << " (" << n << " полей)" << std::endl;
}

// 84.78 loop-sync: pass0 выполняется СИНХРОННО в первом хуке, до
// создания голоса плеером (урок 84.77: голос копирует луп-точки при
// старте; добивка на +6c уже опаздывала, а одна живая структура
// успевала мутировать → тишина вместо лупа). Проходы 1..3 задним
// числом добивают поздние копии (идемпотентно, ревалидация защищает),
// наблюдатель снимает телеметрию каждые 2 с.

// 84.93: страйк по константам КОНКРЕТНОГО трека. Якорь-пара:
//  - лупящиеся треки: (vanilla loopIn, vanilla loopOut) -> наши разрешённые лупы;
//  - без-луп-треки  : (vanilla size, vanilla samples) -> (newSize, newSamples).
// Когерентность (по умолчанию ON, см. strike_coherence): в окне +-48 байт
// вокруг пары обязан лежать ванильный size или samples ЭТОГО трека — так
// мы не стреляем по случайным парам чисел (урок телеметрии 84.89).
static bool TableInRegion(const BYTE* rbase, size_t rsize)
{
    for (auto it = g_found.begin(); it != g_found.end(); ++it) {
        const BYTE* t = it->second.table;
        if (t && t >= rbase && t < rbase + rsize) return true;
    }
    return false;
}

static int LoopStrikePass(const ActiveTrack& tr, std::ofstream* pout,
                          std::vector<LoopVictim>& known)
{
    const TrackMeta& meta = *tr.meta;
    const bool looped = (meta.loopOut != 0xFFFFFFFFu);
    const unsigned A  = looped ? meta.loopIn  : meta.size;      // якорь In
    const unsigned B  = looped ? meta.loopOut : meta.samples;   // якорь Out
    const unsigned AP = looped ? tr.lin       : tr.newSize;     // замена In
    const unsigned BP = looped ? tr.lout      : tr.newSamples;  // замена Out
    SYSTEM_INFO si; GetSystemInfo(&si);
    const BYTE* addr = (const BYTE*)si.lpMinimumApplicationAddress;
    const BYTE* max  = (const BYTE*)si.lpMaximumApplicationAddress;
    static unsigned char buf[4096 + 128];
    MEMORY_BASIC_INFORMATION mbi;
    const __m128i va = _mm_set1_epi32((int)A);
    int struck = 0, cohSkipped = 0;
    while (addr < max) {
        if (VirtualQuery(addr, &mbi, sizeof mbi) != sizeof mbi) break;
        addr += mbi.RegionSize;
        if (mbi.State != MEM_COMMIT) continue;
        if (mbi.Protect & PAGE_GUARD) continue; // 84.94: не трогаем стековые guard
        if (mbi.Type != MEM_PRIVATE) continue;
        DWORD pr = mbi.Protect & 0xFF;
        if (!(pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_WRITECOPY ||
              pr == PAGE_EXECUTE_READ || pr == PAGE_EXECUTE_READWRITE || pr == PAGE_EXECUTE_WRITECOPY))
            continue;
        const BYTE* rbase = (const BYTE*)mbi.BaseAddress;
        if (TableInRegion(rbase, (size_t)mbi.RegionSize)) continue; // сырья не оплакиваем
        for (size_t off = 0; off < mbi.RegionSize; off += 4096) {
            const BYTE* page = rbase + off;
            size_t span = mbi.RegionSize - off < 4096 ? (size_t)mbi.RegionSize - off : 4096;
            if (!GuardCopy(buf, page, span)) continue;
            memset(buf + span, 0, 128);
            if (struck >= 32) return struck;
            for (size_t i = 0; i < span; i += 16) {
                __m128i blk = _mm_loadu_si128((const __m128i*)(buf + i));
                unsigned mv = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi32(blk, va));
                while (mv) {
                    unsigned long idx; _BitScanForward(&idx, mv); mv &= mv - 1;
                    size_t i2 = i + (idx >> 2) * 4;
                    for (size_t j = 0; j < 17; ++j) {
                        size_t k = i2 + j * 4;
                        BYTE* outA = nullptr;
                        if (k + 4 <= sizeof buf && *(const unsigned*)(buf + k) == B)
                            outA = (BYTE*)page + k;
                        else if (i2 >= j * 4 && *(const unsigned*)(buf + i2 - j * 4) == B)
                            outA = (BYTE*)page + (i2 - j * 4);
                        if (!outA) continue;
                        if (known.size() >= 512 || struck >= 32) break;
                        bool dup = false;
                        for (size_t vv = 0; vv < known.size(); ++vv)
                            if (known[vv].inA >= (BYTE*)page + (int)i2 - 16 &&
                                known[vv].inA <= (BYTE*)page + (int)i2 + 16) { dup = true; break; }
                        if (dup) break;
                        LoopVictim v; v.inA = (BYTE*)page + i2; v.outA = outA;
                        unsigned oIn = 0, oOut = 0;
                        if (!GuardRead32(v.inA, oIn) || oIn != A ||
                            !GuardRead32(v.outA, oOut) || oOut != B) {
                            known.push_back(v);
                            break;
                        }
                        // когерентность: в окне должен сидеть ванильный size/samples этого трека
                        if (g_strikeCoherence) {
                            bool okCoh = false;
                            for (int d = -48; d <= 48 && !okCoh; d += 4) {
                                unsigned w = 0;
                                if (GuardRead32(v.inA + d, w) &&
                                    (w == meta.size || w == meta.samples)) okCoh = true;
                            }
                            if (!okCoh) { ++cohSkipped; known.push_back(v); break; }
                        }
                        if (!GuardWrite32(v.inA, AP) || !GuardWrite32(v.outA, BP)) {
                            logFile << "AudioR: LOOPSTRIKE fail @" << (void*)v.inA << std::endl;
                            break;
                        }
                        unsigned cIn = 0, cOut = 0;
                        GuardRead32(v.inA, cIn); GuardRead32(v.outA, cOut);
                        if (cIn == AP && cOut == BP) {
                            ++struck;
                            PatchRec r1 = { v.inA,  oIn,  AP };
                            PatchRec r2 = { v.outA, oOut, BP };
                            g_loopPatch.push_back(r1); g_loopPatch.push_back(r2);
                            known.push_back(v);
                            if (pout) *pout << "LOOPSTRIKE " << tr.key << " @0x"
                                            << std::hex << (uintptr_t)v.inA << std::dec
                                            << " (" << oIn << "," << oOut << ") -> (" << AP << "," << BP << ") [ok]\n";
                            // full-record: соседние закэшированные size/samples
                            for (int d = -48; d <= 48; d += 4) {
                                BYTE* q = v.inA + d;
                                if (q == v.inA || q == v.outA) continue;
                                unsigned val = 0;
                                if (!GuardRead32(q, val)) continue;
                                unsigned repl = 0; const char* what = nullptr;
                                if (val == meta.samples && tr.newSamples) { repl = tr.newSamples; what = "samples"; }
                                else if (val == meta.size && tr.newSize)  { repl = tr.newSize;   what = "size"; }
                                if (!repl) continue;
                                unsigned re2 = 0;
                                if (!GuardRead32(q, re2) || re2 != val) continue;
                                if (!GuardWrite32(q, repl)) continue;
                                unsigned cb = 0; GuardRead32(q, cb);
                                if (cb == repl) {
                                    PatchRec r3 = { (BYTE*)q, val, repl };
                                    g_loopPatch.push_back(r3);
                                    if (pout) *pout << "  field " << what << " @0x"
                                                    << std::hex << (uintptr_t)q << std::dec
                                                    << " " << val << " -> " << repl << " [ok]\n";
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    if (cohSkipped && pout)
        *pout << "coherence-skip (пары без соседних size/samples): " << cohSkipped << "\n";
    return struck;
}

static void LoopWatchThread()
{
    Sleep(6000);
    if (g_dying) return;
    std::ofstream out((string(ModPaths::Dir()) + "\\loop_probe.txt").c_str());
    out << "LoopStrike/Watch report (build " << MOD_BUILD_TAG << ")\n====\n";
    for (int pass = 1; pass <= 3 && !g_dying; ++pass) {
        for (size_t ti = 0; ti < g_tracks.size() && !g_dying; ++ti) {
            int struck = LoopStrikePass(g_tracks[ti], &out, g_loopVictims);
            out << "--- pass#" << pass << " [" << g_tracks[ti].key << "] struck=" << struck << " ----\n";
            logFile << "AudioR: loop pass#" << pass << " [" << g_tracks[ti].key
                    << "] struck=" << struck << std::endl;
        }
        out.flush();
        if (pass < 3) Sleep(pass == 1 ? 14000 : 20000);
    }
    out << "==== watch done. victims=" << g_loopVictims.size() << "\n";
}

// 84.93: обобщённый транзакционный патч записи трека (size/samples/loopIn/loopOut).
static bool ApplyTrackPatch(const string& key, FoundRec& fr, const OggInfo& oi)
{
    const TrackMeta& meta = *fr.meta;
    BYTE* tb = fr.table;
    BYTE* entry = fr.entry;
    // повторная валидация имени (мир мог смениться между пробником и патчем)
    unsigned namePtr = 0;
    string expect = meta.name;
    if (!GuardRead32(entry, namePtr) || namePtr == 0 ||
        !GuardStrEq(tb + namePtr, expect.c_str(), expect.size() + 1)) {
        logFile << "AudioR: валидация записи " << key << " провалена — ванила" << std::endl;
        return false;
    }
    unsigned stqCh = 0;
    GuardRead32(entry + 0x0C, stqCh);
    if (oi.rate != 48000) {
        logFile << "AudioR: частота " << oi.rate << " != 48000 — отказ"
                << " (игра съест неверный темп/тон), ванила" << std::endl;
        return false;
    }
    if (stqCh != (unsigned)oi.channels) {
        logFile << "AudioR: каналы " << oi.channels << " != stq " << stqCh
                << " — отказ во имя краша, ванила" << std::endl;
        return false;
    }
    unsigned lin = 0, lout = 0;
    ResolveLoops(meta, oi, key, lin, lout);

    ActiveTrack tr; tr.key = key; tr.meta = fr.meta;
    tr.newSize = (unsigned)oi.fileSize; tr.newSamples = (unsigned)oi.samples;
    tr.lin = lin; tr.lout = lout;

    const int off[4] = { 0x04, 0x08, 0x10, 0x14 };  // size, samples, loopIn, loopOut
    const unsigned nv[4] = { tr.newSize, tr.newSamples, lin, lout };
    PatchRec rec[4];
    for (int i = 0; i < 4; ++i) {
        BYTE* a = entry + off[i];
        unsigned oldV = 0;
        GuardRead32(a, oldV);
        rec[i].addr = a; rec[i].oldV = oldV; rec[i].newV = nv[i];
        if (!GuardWrite32(a, nv[i])) {
            logFile << "AudioR: PATCH fail +" << std::hex << off[i] << std::dec
                    << " — откат, ванила" << std::endl;
            for (int j = 0; j < i; ++j) GuardWrite32(rec[j].addr, rec[j].oldV);
            return false;
        }
        logFile << "AudioR: PATCH [" << key << "] +" << std::hex << off[i] << std::dec
                << " " << oldV << " -> " << nv[i] << " [ok]" << std::endl;
    }
    for (int i = 0; i < 4; ++i) g_tablePatch.push_back(rec[i]);

    // реестр активных (для фоновых добивок) — по ключу обновляем, не плодим
    bool known = false;
    for (size_t i = 0; i < g_tracks.size(); ++i)
        if (g_tracks[i].key == key) { g_tracks[i] = tr; known = true; break; }
    if (!known) g_tracks.push_back(tr);

    // страйки: pass0 синхронно при первой подмене трека + фоновые добивки
    static std::unordered_map<string, bool> s_struck;
    if (g_strikeLoops && !s_struck[key]) {
        s_struck[key] = true;
        DWORD t0 = GetTickCount();
        int struck = LoopStrikePass(tr, nullptr, g_loopVictims);
        logFile << "AudioR: loop pass#0 [" << key << "] struck=" << struck
                << " (" << (GetTickCount() - t0) << " ms)" << std::endl;
        static bool s_watchStarted = false;
        if (!s_watchStarted) { s_watchStarted = true; std::thread(LoopWatchThread).detach(); }
    }
    std::thread(PatchWatch).detach();
    g_patched = true; // для сообщений о выкате в Shutdown
    return true;
}

static bool CanPatchKey(const string& key)
{
    auto it = g_found.find(key);
    return it != g_found.end() && it->second.meta != nullptr && it->second.entry != nullptr;
}

// ---------------------------------------------------------------- detours
//
// R1 (84.70): игра читает музыку через CreateFileA (ANSI), АБСОЛЮТНЫМИ
// путями, одним потоком; каждый трек открывается ~3 раза подряд.
// Подмена идемпотентна: в окне 5 c тот же ключ → тот же выбранный файл
// (иначе три открытия одного трека накидают разные позиции пула).

static std::unordered_map<string, int> g_notedUnknown;  // антиспам «запись неизвеста»

struct LastSub { string key, full; DWORD tick = 0; };
static LastSub g_last;

static bool TrySubstitute(const string& key, string& fullOut)
{
    if (!g_enabled) return false;
    auto it = g_map.find(key);
    if (it == g_map.end()) return false;
    if (!CanPatchKey(key)) {
        // Гонка (улов 84.72): трек открывается сразу пачкой, а пробник
        // идёт ~1-2 с в отдельном потоке. Удерживаем первый вызов до
        // валидации записи. R3: ворота теперь под любую запись каталога.
        if (g_probeStq) {
            MaybeProbe();
            DWORD t0 = GetTickCount();
            while (!g_found.count(key) && GetTickCount() - t0 < 12000) Sleep(100);
            logFile << "AudioR: probe wait " << (GetTickCount() - t0)
                    << " ms, " << key
                    << (g_found.count(key) ? " НАЙДЕН" : " НЕ НАЙДЕН") << std::endl;
        }
        if (!CanPatchKey(key)) {
            if (g_notedUnknown[key]++ == 0 && !g_probeStq)
                logFile << "AudioR: ключ " << key
                        << " смаплен, но STRQ-запись неизвестна — ванила (R2-MVP)" << std::endl;
            return false;
        }
    }
    if (g_last.key == key && !g_last.full.empty() &&
        GetTickCount() - g_last.tick < 5000) {
        fullOut = g_last.full; // то же открытие того же трека
        return true;
    }
    const Pool& pool = it->second;
    OggInfo oi;
    string rel, full;
    bool anyOk = false;
    size_t tries = pool.files.size() < 3 ? pool.files.size() : 3; // защита от дурака: до 3 попыток на живой файл
    for (size_t t = 0; t < tries; ++t) {
        std::uniform_int_distribution<size_t> pick(0, pool.files.size() - 1);
        rel  = pool.files[pick(g_rng)];
        full = string(ModPaths::Dir()) + "\\music\\" + rel;
        if (ProbeOgg(full, oi)) { anyOk = true; break; }
        logFile << "AudioR: ogg fail " << rel << " (попытка " << (t + 1)
                << "/" << tries << ") — кручу пул дальше" << std::endl;
    }
    if (!anyOk) {
        logFile << "AudioR: весь пул мёртв (" << pool.files.size() << ") — ванила" << std::endl;
        return false;
    }
    if (!ApplyTrackPatch(key, g_found[key], oi)) return false; // причины уже в логе
    logFile << "AudioR: SUB " << key << " -> " << rel
            << " (" << oi.fileSize << "B, " << oi.samples
            << " smp, " << oi.channels << "ch)" << std::endl;
    g_last.key = key; g_last.full = full; g_last.tick = GetTickCount();
    fullOut = full;
    return true;
}

static bool LooksSngwW(LPCWSTR w) {
    for (LPCWSTR p = w; *p; ++p)
        if (*p == L'.' && (p[1] == L's' || p[1] == L'S') &&
            (p[2] == L'n' || p[2] == L'N') && (p[3] == L'g' || p[3] == L'G') &&
            (p[4] == L'w' || p[4] == L'W') && p[5] == 0) return true;
    return false;
}

static HANDLE WINAPI Hook_CreateFileA(LPCSTR lpFileName, DWORD da, DWORD sm,
    LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fa, HANDLE ht)
{
    if (g_dying) return oCreateFileA(lpFileName, da, sm, sa, cd, fa, ht);
    if (lpFileName) {
        string key;
        if (ExtractBgmKey(lpFileName, key)) {
            MaybeProbe();
            if (g_logRequests)
                logFile << "AudioR[A]: " << lpFileName
                        << " tid=" << GetCurrentThreadId() << std::endl;
            string full;
            if (TrySubstitute(key, full))
                return oCreateFileA(full.c_str(), da, sm, sa, cd, fa, ht);
        }
    }
    return oCreateFileA(lpFileName, da, sm, sa, cd, fa, ht);
}

static HANDLE WINAPI Hook_CreateFileW(LPCWSTR lpFileName, DWORD da, DWORD sm,
    LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fa, HANDLE ht)
{
    if (g_dying) return oCreateFileW(lpFileName, da, sm, sa, cd, fa, ht);
    if (lpFileName && LooksSngwW(lpFileName)) {
        string key;
        if (ExtractBgmKey(WideToUtf8(lpFileName), key)) {
            MaybeProbe();
            if (g_logRequests)
                logFile << "AudioR[W]: " << WideToUtf8(lpFileName)
                        << " tid=" << GetCurrentThreadId() << std::endl;
            string full;
            if (TrySubstitute(key, full)) {
                std::wstring w = Utf8ToWidePath(full);
                return oCreateFileW(w.c_str(), da, sm, sa, cd, fa, ht);
            }
        }
    }
    return oCreateFileW(lpFileName, da, sm, sa, cd, fa, ht);
}

// ---------------------------------------------------------------- module

void Audio::Init()
{
    LoadMap();
    // 84.89: смешанный сид — первый rand() после srand(tick) коррелировал с сидом,
    // и запуски подряд давали один и тот же выбор пула (улов 5 из 5 одного файла).
    g_rng.seed(GetTickCount() ^ (GetCurrentProcessId() * 2654435761u) ^
               (unsigned)time(nullptr) ^ 0x9E3779B9u);
    (void)g_rng(); // прожечь первый выхлоп (C4834: mt19937::operator() [[nodiscard]])
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) { logFile << "AudioR: kernel32 не найден, модуль выключен" << std::endl; return; }
    void* pw = GetProcAddress(k32, "CreateFileW");
    void* pa = GetProcAddress(k32, "CreateFileA");
    Hooks::CreateHook("AudioR CreateFileW", pw, Hook_CreateFileW, (LPVOID*)&oCreateFileW);
    Hooks::CreateHook("AudioR CreateFileA", pa, Hook_CreateFileA, (LPVOID*)&oCreateFileA);
}

void Audio::Shutdown()
{
    g_dying = true; // сразу: хуки становятся сквозными, потоки выходят
    logFile << "AudioR: shutdown начат" << std::endl;
    if (!g_tablePatch.empty()) {
        // 84.82/84.93: откат только если там до сих пор наше значение.
        int restored = 0;
        for (size_t i = 0; i < g_tablePatch.size(); ++i) {
            unsigned cur = 0;
            if (GuardRead32(g_tablePatch[i].addr, cur) && cur == g_tablePatch[i].newV) {
                if (GuardWrite32(g_tablePatch[i].addr, g_tablePatch[i].oldV)) ++restored;
            }
        }
        g_tablePatch.clear();
        g_patched = false;
        logFile << "AudioR: rollback таблиц выполнен (" << restored << " полей)" << std::endl;
    }
    int lr = 0;
    for (size_t i = 0; i < g_loopPatch.size(); ++i) {
        unsigned cur = 0;
        if (GuardRead32(g_loopPatch[i].addr, cur) && cur == g_loopPatch[i].newV) {
            if (GuardWrite32(g_loopPatch[i].addr, g_loopPatch[i].oldV)) ++lr;
        }
    }
    if (!g_loopPatch.empty())
        logFile << "AudioR: rollback луп-копий выполнен (" << lr << "/"
                << g_loopPatch.size() << ")" << std::endl;
    logFile << "AudioR: shutdown окончен" << std::endl;
}

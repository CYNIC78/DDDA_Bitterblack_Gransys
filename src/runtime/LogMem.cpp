#include "stdafx.h"
#include "LogMem.h"
#include "MemProbe.h"
#include <tlhelp32.h>
#include <streambuf>
#include <cstring>

namespace {

static const size_t kCap = 8u * 1024u * 1024u; // 8 МБ; дальше тихий truncate
static const char*  kPath = "ddda_ai_overhaul.log";

class MemLogBuf : public std::streambuf {
public:
    MemLogBuf()
    {
        InitializeCriticalSectionAndSpinCount(&m_cs, 4000);
        m_mem.reserve(kCap);
    }

    ~MemLogBuf()
    {
        DeleteCriticalSection(&m_cs);
    }

    void Dump()
    {
        EnterCriticalSection(&m_cs);
        const char* p = m_mem.empty() ? "" : m_mem.data();
        const DWORD n = (DWORD)m_mem.size();
        LeaveCriticalSection(&m_cs);

        HANDLE f = CreateFileA(kPath, GENERIC_WRITE, FILE_SHARE_READ, 0,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (f == INVALID_HANDLE_VALUE) return;
        DWORD w = 0;
        if (n) WriteFile(f, p, n, &w, 0);
        CloseHandle(f);
    }

    void PeriodicFlush(DWORD intervalMs)
    {
        const DWORD now = GetTickCount();
        if (m_lastFlush && (now - m_lastFlush) < intervalMs) return;
        m_lastFlush = now;
        Dump();
    }

protected:
    int overflow(int ch) override
    {
        if (ch == EOF) return EOF;
        const char c = (char)ch;
        xsputn(&c, 1);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        if (!s || n <= 0) return 0;
        EnterCriticalSection(&m_cs);
        if (!m_full) {
            const size_t room = (m_mem.size() < kCap) ? (kCap - m_mem.size()) : 0;
            if ((size_t)n <= room) {
                m_mem.append(s, (size_t)n);
            } else {
                if (room) m_mem.append(s, room);
                static const char kCut[] =
                    "\n[LogMem] buffer full (8 MiB), further lines dropped\n";
                m_mem.append(kCut, sizeof(kCut) - 1);
                m_full = true;
            }
        }
        LeaveCriticalSection(&m_cs);
        return n; // не блокируем <<
    }

    int sync() override { return 0; } // endl не идёт на диск

private:
    CRITICAL_SECTION m_cs;
    std::string      m_mem;
    DWORD            m_lastFlush = 0;
    bool             m_full      = false;
};

static MemLogBuf g_buf;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = 0;
static PVOID g_vehHandler = nullptr;

// 85.24: счётчики и окно тишины для векторного обработчика.
//
// Разбор вылета 2026-09-25 показал две вещи.
//
// 1. Обработчик стоял с приоритетом «первым» (AddVectoredExceptionHandler(1,..))
//    и на КАЖДОЕ нарушение доступа выгружал на диск ВЕСЬ буфер лога (~200 КБ)
//    через CreateFileA(CREATE_ALWAYS). А `Rd`/`WrSafe` построены на `__try`,
//    то есть нарушение доступа — для них ШТАТНЫЙ ход событий, а не авария.
//    Мод читает память широко (в обходе тел трогает +0x73BF, 29631-й байт
//    тела, а тело гоблина ровно 29632 байта — существа поменьше на этом
//    чтении законно сбоят). Получалось: чтение -> сбой -> запись 200 КБ
//    внутри кадра игры. Это и есть подозреваемый №1 по «фризам».
//
// 2. При этом строку о самом сбое обработчик не писал вообще — отсюда
//    слепой лог и целый разбор вслепую.
//
// Теперь: полный дамп не чаще раза в 5 с; каждый сбой считается; а о сбое
// пишется строка с кодом и адресом. Настоящий, необработанный краш по-прежнему
// ловится фильтром OnCrash — он пишет строку и выгружает лог всегда.
static const DWORD kVehQuietMs = 5000;
static DWORD      g_vehLastDumpMs = 0;
static uint32_t   g_vehFaults = 0;
static uint32_t   g_vehDumps = 0;

// --- 85.25: КТО ИМЕННО КРУТИТ ПЛОХОЙ УКАЗАТЕЛЬ ------------------------------
//
// Зачем. Поле 85.24 дало 45 087 перехваченных нарушений доступа за сессию —
// по одному и тому же адресу ИНСТРУКЦИИ, ~14 000 на каждый мир, и без имени
// модуля. Грубая классификация («наш / игра / неведомо») не отвечает на
// главный вопрос: какую ПАМЯТЬ читают и чей это указатель. Поэтому здесь:
//   1) таблица загруженных модулей (Toolhelp32) — настоящее имя и смещение;
//   2) адрес, к которому шло обращение (`ExceptionInformation[1]`) и тип
//      доступа (чтение/запись);
//   3) регистры на момент сбоя: у нас почти всегда виновник сидит в одном
//      из них (ESI/EDI/EBX), и его можно сверить с телами из лога;
//   4) дедупликация по адресу инструкции: каждая НОВАЯ точка сбоя пишется
//      сразу, повторы только считаются. Строка идёт в буфер в памяти, а не на
//      диск, поэтому стоит почти ничего.
struct ModRange {
    uintptr_t lo;
    uintptr_t hi;
    char      name[40];
};

static ModRange g_mods[96];
static int      g_nMods = 0;
static bool     g_modsTried = false;
static DWORD    g_workerThreadId = 0;

static void LoadModuleTable()
{
    if (g_modsTried) return;
    g_modsTried = true;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32 me;
    memset(&me, 0, sizeof(me));
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            if (g_nMods >= (int)(sizeof(g_mods) / sizeof(g_mods[0]))) break;
            ModRange& m = g_mods[g_nMods++];
            m.lo = (uintptr_t)me.modBaseAddr;
            m.hi = m.lo + me.modBaseSize;
            lstrcpynA(m.name, me.szModule, sizeof(m.name));
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
}

// Имя модуля и смещение внутри него. Возвращает false, если адрес ничей:
// именно этот случай журнал Windows называет `unknown`.
static bool ModuleOfAddress(const void* addr, char* out, int cap)
{
    const uintptr_t a = (uintptr_t)addr;
    LoadModuleTable();
    for (int i = 0; i < g_nMods; ++i) {
        if (a < g_mods[i].lo || a >= g_mods[i].hi) continue;
        sprintf_s(out, cap, "%s+0x%X", g_mods[i].name,
                  (unsigned)(a - g_mods[i].lo));
        return true;
    }
    if (a < 0x00010000u) { sprintf_s(out, cap, "null-ish"); return false; }
    sprintf_s(out, cap, "not-in-any-module");
    return false;
}

// Куда шло обращение. Тело монстра живёт в игровой куче (0x10000000..0x18000000
// по FIELD_MAP) — именно этот ответ и нужен: «мы попали в тело или мимо».
static const char* DataClass(uintptr_t a)
{
    char mod[64];
    if (ModuleOfAddress((const void*)a, mod, sizeof(mod))) return "in-module";
    if (a >= 0x10000000u && a < 0x18000000u) return "game-heap-band";
    if (a < 0x00010000u) return "null-ish";
    if (a >= 0x30000000u && a < 0x80000000u) return "stack-or-module-band";
    return "other";
}

// Точки сбоя: одна запись на адрес инструкции.
struct FaultSite {
    uintptr_t pc;
    uintptr_t dataAddr;
    uint32_t  count;
};
static FaultSite g_sites[8];
static int       g_nSites = 0;

static LONG WINAPI OnCrash(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord) {
        char mod[64];
        ModuleOfAddress(info->ExceptionRecord->ExceptionAddress, mod, sizeof(mod));
        char buf[320];
        sprintf_s(buf, "\n!!! CRASH: Exception 0x%08X at address 0x%p  in %s"
                       "  vehFaults=%u  !!!\n",
                  info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress,
                  mod, g_vehFaults);
        logFile << buf << std::endl;
    }
    g_buf.Dump();
    if (g_prevFilter) return g_prevFilter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI OnVeh(EXCEPTION_POINTERS* info)
{
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD c = info->ExceptionRecord->ExceptionCode;
    const bool isFault = (c == EXCEPTION_ACCESS_VIOLATION
                       || c == EXCEPTION_ILLEGAL_INSTRUCTION
                       || c == EXCEPTION_STACK_OVERFLOW
                       || c == EXCEPTION_DATATYPE_MISALIGNMENT
                       || c == EXCEPTION_INT_DIVIDE_BY_ZERO
                       || c == 0xC0000005);
    if (!isFault) return EXCEPTION_CONTINUE_SEARCH;

    ++g_vehFaults;

    // Разбор самого сбоя. Для нарушения доступа в ExceptionInformation:
    //   [0] = тип (0 чтение, 1 запись, 8 исполнение), [1] = адрес обращения.
    const EXCEPTION_RECORD* er = info->ExceptionRecord;
    const DWORD  accessType = er->NumberParameters > 0
                            ? (DWORD)er->ExceptionInformation[0] : 0xFFFFFFFFu;
    const uintptr_t dataAddr = er->NumberParameters > 1
                             ? (uintptr_t)er->ExceptionInformation[1] : 0;

    const char* accessName = (accessType == 0) ? "READ"
                           : (accessType == 1) ? "WRITE"
                           : (accessType == 8) ? "EXECUTE" : "?";

    char pcMod[64];
    ModuleOfAddress(er->ExceptionAddress, pcMod, sizeof(pcMod));
    char dataMod[64];
    const bool dataInModule = ModuleOfAddress((const void*)dataAddr, dataMod,
                                              sizeof(dataMod));

    // --- Дедупликация по адресу инструкции: новая точка — строка сразу.
    // Повторы той же точки только считаются, иначе штатные сбои чтения
    // (девять раз за такт) залили бы лог за минуту.
    FaultSite* site = 0;
    for (int i = 0; i < g_nSites; ++i) {
        if (g_sites[i].pc == (uintptr_t)er->ExceptionAddress) {
            site = &g_sites[i];
            break;
        }
    }
    if (!site && g_nSites < (int)(sizeof(g_sites) / sizeof(g_sites[0]))) {
        site = &g_sites[g_nSites++];
        site->pc = (uintptr_t)er->ExceptionAddress;
        site->dataAddr = dataAddr;
        site->count = 0;
        const CONTEXT* ctx = info->ContextRecord;
        const DWORD tid = GetCurrentThreadId();
        char buf[640];
        if (ctx) {
            sprintf_s(buf,
                "\n[VEH] NEW SITE %s(%u) at 0x%08X in %s | data 0x%08X %s%s"
                " | thread %u%s\n"
                "      eip=%08X eax=%08X ebx=%08X ecx=%08X edx=%08X"
                " esi=%08X edi=%08X ebp=%08X esp=%08X\n",
                accessName, accessType,
                (unsigned)(uintptr_t)er->ExceptionAddress, pcMod,
                (unsigned)dataAddr, DataClass(dataAddr),
                dataInModule ? " (в модуле)" : "",
                tid, (g_workerThreadId && tid == g_workerThreadId)
                         ? " (наш рабочий поток)" : "",
                (unsigned)ctx->Eip, (unsigned)ctx->Eax, (unsigned)ctx->Ebx,
                (unsigned)ctx->Ecx, (unsigned)ctx->Edx, (unsigned)ctx->Esi,
                (unsigned)ctx->Edi, (unsigned)ctx->Ebp, (unsigned)ctx->Esp);
        } else {
            sprintf_s(buf,
                "\n[VEH] NEW SITE %s(%u) at 0x%08X in %s | data 0x%08X %s"
                " | thread %u (нет контекста)\n",
                accessName, accessType,
                (unsigned)(uintptr_t)er->ExceptionAddress, pcMod,
                (unsigned)dataAddr, DataClass(dataAddr), tid);
        }
        logFile << buf;
    }
    if (site) ++site->count;

    // Полный дамп на диск — редко. Строки выше уходят только в буфер в памяти,
    // поэтому стоят почти ничего; диск мы не трогаем чаще раза в 5 секунд.
    const DWORD now = GetTickCount();
    if (g_vehLastDumpMs && (now - g_vehLastDumpMs) < kVehQuietMs)
        return EXCEPTION_CONTINUE_SEARCH;
    g_vehLastDumpMs = now;
    ++g_vehDumps;
    char buf[256];
    sprintf_s(buf, "\n[VEH] dump #%u: faults=%u sites=%d\n",
              g_vehDumps, g_vehFaults, g_nSites);
    logFile << buf;
    g_buf.Dump();
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

std::ostream logFile(&g_buf);

namespace LogMem {

void Init()
{
    g_vehHandler = AddVectoredExceptionHandler(1, OnVeh);
    g_prevFilter = SetUnhandledExceptionFilter(OnCrash);
}

void FlushToDisk()
{
    g_buf.Dump();
}

void PeriodicFlush(DWORD intervalMs)
{
    g_buf.PeriodicFlush(intervalMs);
}

// ВАЖНО: именно здесь, в именованном пространстве LogMem, а НЕ в анонимном выше.
// Анонимное пространство даёт функции внутреннюю линковку — файл её видит, а
// PawnAI.cpp нет, и линковщик падает с «неразрешенный внешний символ».
// VEH_Faults добавлен сюда же и по той же причине.
void SetWorkerThreadId(DWORD tid)
{
    g_workerThreadId = tid;
}

unsigned VEH_Faults()
{
    return g_vehFaults;
}

} // namespace LogMem

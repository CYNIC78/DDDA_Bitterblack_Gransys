#include "stdafx.h"
#include "LogMem.h"
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

static LONG WINAPI OnCrash(EXCEPTION_POINTERS* info)
{
    g_buf.Dump();
    if (g_prevFilter) return g_prevFilter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI OnVeh(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord) {
        DWORD c = info->ExceptionRecord->ExceptionCode;
        if (c == EXCEPTION_ACCESS_VIOLATION
            || c == EXCEPTION_ILLEGAL_INSTRUCTION
            || c == EXCEPTION_STACK_OVERFLOW
            || c == EXCEPTION_DATATYPE_MISALIGNMENT
            || c == EXCEPTION_INT_DIVIDE_BY_ZERO
            || c == 0xC0000005) {
            g_buf.Dump();
        }
    }
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

} // namespace LogMem

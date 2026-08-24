// Runtime::PartyStatus — read-only прибор: статусы партии + downed/revive.
// См. PartyStatus.h и docs/PARTY_STATUS_OBSERVE.md.
//
// Два канала наблюдения, оба без единой записи:
//   A. Статусные блоки (cStatus / cEffectStatusManager) — якорный поиск
//      по имени класса + дельта-дамп. Поиск — единственный дорогой кусок:
//      полный обход тела по 4-байтовым указателям, поэтому он троттлится
//      (3 с на класс на тело) и после находки не повторяется.
//   B. Downed/revive FSM по имени live-акта. Переходы = строки лога;
//      строка = наблюдение, а не догадка.

#include "stdafx.h"
#include "PartyStatus.h"
#include "Runtime.h"
#include "MemProbe.h"

namespace Runtime {
namespace PartyStatus {

// Защищённые чтения живут в фундаменте рантайма (Rd/RegionOk/InWorld).
using namespace Mem;

// --- блоки-кандидаты (POSSESSION_RECON §5) -----------------------------
//
// cStatus (152 B) — главный подозреваемый: «состояние статусов существа».
// cEffectStatusManager (32 B) — менеджер наложенных эффектов.
// Остальные кандидаты (§5) — вложенные объекты; ищем их ВНУТРИ найденного
// cStatus следующим билдом, а не перебором всего тела (FIX_RULES §5.1).
struct BlockSpec {
    const char* cls;
    uint32_t    size;
};
static const BlockSpec kBlocks[] = {
    { "cStatus", 152 },
    { "cEffectStatusManager", 32 }
};
static const int kNBlocks        = 2;
static const int kBlockMaxDwords = 152 / 4;   // 38

// Размер тела для discovery-обхода: uPlayer 23056 (0x5A40) / uCmc 22752.
static const uint32_t kPartyBodyBytes = 0x5A40;

static const DWORD kTickMs          = 500;    // каденс прибора
static const DWORD kDiscoverMs      = 3000;   // один класс на тело за проход
static const DWORD kDeltaThrottleMs = 200;    // дельта-строка на блок
static const DWORD kHeartbeatMs     = 30000;  // строка живучести на тело (84.19: 10c спамило)
static const DWORD kFreshMs         = 5000;   // свежесть FSM для снапшота
// 84.17: опрос имён детей (лог 24: cStatus не найден по указателю —
// видно, что на самом деле висит на теле партии).
static const int    kSurveyMaxNames = 12;
static const DWORD  kSurveyLogMs    = 15000;  // строка опроса на тело

// --- действия ----------------------------------------------------------
//
// Набор downed совпадает с кандидатным списком PartyRecon (kCandidate),
// плюс предшественник cPlActCmcNeardeath — вход последовательности.
static const char* kDownedActs[] = {
    "cPlActCmcNeardeath",
    "cPlActCmcDead",
    "cPlActDead",
    "cPlActDmgDown",
    "cPlActDmgDownDamage",
    "cPlActDmgDownDead"
};
static const int kDownedActCount =
    (int)(sizeof(kDownedActs) / sizeof(kDownedActs[0]));
static const char* kReviveAct = "cPlReviveCMC";

// Действия, форсирующие немедленный discovery-проход: restraint =
// воспроизводимое «ослабленное» состояние на берегу Кассардиса (мы его
// и так гоняем в гоблинском QTE), StatusRecover/Refresh — статусная
// сторона игрока/пешки.
static const char* kStatusActs[] = {
    "cPlActGrabStart",
    "cPlActHagaijime",
    "cPlActHagaijime4Feet",
    "cPlActStatusRecover",
    "cPlActStatusRefresh"
};
static const int kStatusActCount =
    (int)(sizeof(kStatusActs) / sizeof(kStatusActs[0]));

static bool ActInSet(const char* act, const char** set, int n)
{
    if (!act || !act[0]) return false;
    for (int i = 0; i < n; ++i)
        if (!strcmp(act, set[i])) return true;
    return false;
}

static const char* SlotName(int slot)
{
    if (slot == PARTY_ARISEN) return "Arisen";
    if (slot == PARTY_MAIN)   return "MainPawn";
    if (slot == PARTY_HIRED1) return "Hired1";
    if (slot == PARTY_HIRED2) return "Hired2";
    return "?";
}

struct BodyTrack {
    uintptr_t body;
    int       slot;
    bool      haveAct;     // акт читался хотя бы раз (0 — не «старое»)
    DWORD     lastActMs;
    char      lastAct[48];

    // FSM downed/revive (канал B).
    bool      downedNow;
    DWORD     downedSinceMs;
    bool      reviveSeen;   // cPlReviveCMC наблюдался в текущем down
    bool      everRevived;  // полная последовательность наблюдалась когда-либо

    // Статусные блоки (канал A, якорный поиск).
    uintptr_t blockPtr[kNBlocks];
    uint32_t  blockOff[kNBlocks];
    bool      blockFound[kNBlocks];
    uint32_t  prevData[kNBlocks][kBlockMaxDwords];
    bool      havePrev[kNBlocks];
    uint32_t  changeCount[kNBlocks];
    DWORD     lastDeltaMs[kNBlocks];
    DWORD     lastDiscoverMs;
    int       discoverPhase;   // round-robin: один класс за проход
    DWORD     lastSurveyLogMs; // троттл строки опроса детей
    DWORD     lastHeartbeatMs;
};

static const int kMaxBodies = 4;
static BodyTrack s_body[kMaxBodies];
static int       s_nBody = 0;
static DWORD     s_lastTick = 0;
static char      s_status[128] = "party status: armed";

static BodyTrack* FindTrack(uintptr_t body)
{
    for (int i = 0; i < s_nBody; ++i)
        if (s_body[i].body == body) return &s_body[i];
    return 0;
}

// Тела партии ушли (смерть/смена) — забыли; новые — добавили.
static void SyncBodies(const uintptr_t* bodies, const int* slots, int n)
{
    for (int i = 0; i < s_nBody; ) {
        bool alive = false;
        for (int k = 0; k < n; ++k)
            if (bodies[k] == s_body[i].body) { alive = true; break; }
        if (alive) { ++i; continue; }
        s_body[i] = s_body[s_nBody - 1];
        --s_nBody;
    }
    for (int k = 0; k < n && s_nBody < kMaxBodies; ++k) {
        if (FindTrack(bodies[k])) continue;
        BodyTrack& T = s_body[s_nBody++];
        memset(&T, 0, sizeof(T));
        T.body = bodies[k];
        T.slot = slots[k];
        logFile << "PS: " << SlotName(T.slot) << " track @" << std::hex
                << T.body << std::dec
                << " (read-only status + downed/revive observer)" << std::endl;
    }
}

// --- канал B: downed/revive FSM -----------------------------------------
//
// Переходы:
//   * -> downed act                  DOWNED
//   downed -> cPlReviveCMC           REVIVE (разово за down)
//   downed/revive -> обычный act     RECOVERED (если было REVIVE) /
//                                    DOWN-END (без REVIVE — добита?)
// Полная последовательность = живая валидация: именно её мы передаём
// в downedRevivable, а не угаданную «скорее всего воскрешаемо».
static void FsmTick(BodyTrack& T, const char* act, DWORD now)
{
    const bool isDowned = ActInSet(act, kDownedActs, kDownedActCount);
    const bool isRevive = !strcmp(act, kReviveAct);

    if (isDowned && !T.downedNow) {
        T.downedNow = true;
        T.downedSinceMs = now;
        T.reviveSeen = false;
        logFile << "PS: " << SlotName(T.slot) << " DOWNED act=" << act
                << std::endl;
        return;
    }
    if (!T.downedNow) return;

    if (isRevive && !T.reviveSeen) {
        T.reviveSeen = true;
        logFile << "PS: " << SlotName(T.slot) << " REVIVE act=cPlReviveCMC"
                << " downed+" << (now - T.downedSinceMs) << "ms" << std::endl;
        return;
    }
    if (!isDowned && !isRevive) {
        if (T.reviveSeen) {
            T.everRevived = true;
            logFile << "PS: " << SlotName(T.slot) << " RECOVERED act=" << act
                    << " (revive sequence confirmed, downed+"
                    << (now - T.downedSinceMs) << "ms)" << std::endl;
        } else {
            logFile << "PS: " << SlotName(T.slot) << " DOWN-END act=" << act
                    << " (no revive observed)" << std::endl;
        }
        T.downedNow = false;
        T.reviveSeen = false;
    }
}

// --- канал A: статусные блоки -------------------------------------------

// Discovery: полный обход тела по имени класса. Один класс за проход,
// троттл kDiscoverMs — та же цена, что у планировщика пешки, и не чаще.
// 84.17: discovery + опрос имён детей за один проход (та же цена, что у
// FindChildByClass: указатель каждые 4 байта + DTI-имя, кэш имён по
// vtable). Лог 24 показал: cStatus/cEffectStatusManager не находятся по
// указателю за 5 минут — опрос показывает, что на теле вообще висит,
// и даёт карту для поиска блока статусов (по имени, по сигнатуре, по
// известному якорю).
static void DiscoverOne(BodyTrack& T, DWORD now, bool force)
{
    bool allFound = true;
    for (int i = 0; i < kNBlocks; ++i)
        if (!T.blockFound[i]) { allFound = false; break; }
    if (allFound) return;
    if (!force && now - T.lastDiscoverMs < kDiscoverMs) return;
    T.lastDiscoverMs = now;

    const int want = T.discoverPhase % kNBlocks;
    T.discoverPhase++;

    char     names[kSurveyMaxNames][48];
    uint32_t nameOff[kSurveyMaxNames];
    int      nNames = 0;
    uintptr_t found = 0;
    uint32_t foundOff = 0;

    for (uint32_t off = 0; off + 4 <= kPartyBodyBytes; off += 4) {
        uintptr_t cand = 0;
        if (!RdPtr((void*)(T.body + off), &cand)) continue;
        if (!LooksHeap(cand) || cand == T.body) continue;
        char nm[48] = {};
        if (!NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;
        if (!found && !T.blockFound[want] && !strcmp(nm, kBlocks[want].cls)) {
            found = cand;
            foundOff = off;
        }
        // Классоподобные имена (c*/r*/s*/u*) — карта детей. До
        // kSurveyMaxNames уникальных; первый встреченный оффсет.
        const char c0 = nm[0];
        if (c0 != 'c' && c0 != 'r' && c0 != 's' && c0 != 'u') continue;
        int dup = -1;
        for (int k = 0; k < nNames; ++k)
            if (!strcmp(names[k], nm)) { dup = k; break; }
        if (dup >= 0) continue;
        if (nNames < kSurveyMaxNames) {
            lstrcpynA(names[nNames], nm, sizeof(names[0]));
            nameOff[nNames] = off;
            ++nNames;
        }
    }

    if (now - T.lastSurveyLogMs >= kSurveyLogMs) {
        T.lastSurveyLogMs = now;
        logFile << "PS: " << SlotName(T.slot) << " children (scan "
                << kBlocks[want].cls << "):";
        for (int k = 0; k < nNames; ++k)
            logFile << " " << names[k] << "@+0x" << std::hex << nameOff[k]
                    << std::dec;
        if (nNames == kSurveyMaxNames) logFile << " (more)";
        logFile << std::endl;
    }

    if (!found) return;

    T.blockFound[want] = true;
    T.blockPtr[want] = found;
    T.blockOff[want] = foundOff;
    T.havePrev[want] = false;
    T.changeCount[want] = 0;
    T.lastDeltaMs[want] = 0;
    logFile << "PS: " << SlotName(T.slot) << " " << kBlocks[want].cls
            << " found @" << std::hex << found << std::dec
            << " off=+0x" << std::hex << foundOff << std::dec
            << " size=" << kBlocks[want].size << std::endl;
}

// Точечное чтение найденного блока + дельта-строка при изменении.
static void BlockTick(BodyTrack& T, DWORD now)
{
    for (int i = 0; i < kNBlocks; ++i) {
        if (!T.blockFound[i]) continue;
        const uint32_t nwords = kBlocks[i].size / 4;
        uint32_t data[kBlockMaxDwords] = {};
        if (!Rd((void*)T.blockPtr[i], data, (size_t)nwords * 4)) {
            // Указатель сгнил: объект могли пересоздать. Не гадать —
            // вернуть блок в состояние поиска.
            T.blockFound[i] = false;
            T.havePrev[i] = false;
            T.lastDiscoverMs = 0;
            logFile << "PS: " << SlotName(T.slot) << " " << kBlocks[i].cls
                    << " pointer lost, re-discover" << std::endl;
            continue;
        }
        if (!T.havePrev[i]) {
            memcpy(T.prevData[i], data, (size_t)nwords * 4);
            T.havePrev[i] = true;
            continue;   // базовая линия — молчим
        }

        // Собираем изменённые поля: до 24 ячейки + счётчик «и ещё».
        char cells[1600] = {};
        int pos = 0;
        int changed = 0;
        int printed = 0;
        for (uint32_t d = 0; d < nwords; ++d) {
            if (data[d] == T.prevData[i][d]) continue;
            ++changed;
            if (printed >= 24) continue;
            const int a = (int)T.prevData[i][d];
            const int b = (int)data[d];
            float fa = 0.0f, fb = 0.0f;
            memcpy(&fa, &a, 4);
            memcpy(&fb, &b, 4);
            // Оба значения «выглядят как float» — печатаем дробью,
            // иначе целым. Как в CARD-строках Aggro: решают числа.
            const bool floatish =
                (fa > -100000.0f && fa < 100000.0f
                    && (fa == 0.0f || fa > 0.0001f || fa < -0.0001f))
             && (fb > -100000.0f && fb < 100000.0f
                    && (fb == 0.0f || fb > 0.0001f || fb < -0.0001f));
            char cell[80] = {};
            if (floatish)
                sprintf_s(cell, sizeof(cell), " +%03X %.3f->%.3f",
                          (int)d * 4, fa, fb);
            else
                sprintf_s(cell, sizeof(cell), " +%03X %d->%d",
                          (int)d * 4, a, b);
            const int len = (int)strlen(cell);
            if (pos + len < (int)sizeof(cells)) {
                memcpy(cells + pos, cell, (size_t)len);
                pos += len;
            }
            ++printed;
        }
        if (!changed) continue;
        T.changeCount[i] += (uint32_t)changed;
        if (now - T.lastDeltaMs[i] >= kDeltaThrottleMs) {
            T.lastDeltaMs[i] = now;
            logFile << "PS: " << SlotName(T.slot) << " " << kBlocks[i].cls
                    << " @" << std::hex << T.blockPtr[i] << std::dec << cells;
            if (printed < changed)
                logFile << " (+" << (changed - printed) << " more)";
            logFile << std::endl;
        }
        memcpy(T.prevData[i], data, (size_t)nwords * 4);
    }
}

static void BodyTick(BodyTrack& T, DWORD now)
{
    char act[48] = {};
    if (!ReadLiveAct(T.body, act, sizeof(act)))
        return;   // тело умерло или нечитаемо: FSM и блоки не трогаем
    T.haveAct = true;
    T.lastActMs = now;
    const bool actChanged = !T.lastAct[0] || strcmp(T.lastAct, act) != 0;
    lstrcpynA(T.lastAct, act, sizeof(T.lastAct));

    FsmTick(T, act, now);

    // Событие: restraint/статусные акты форсируют discovery-проход —
    // именно под эти состояния мы и пришли.
    const bool statusAct = ActInSet(act, kStatusActs, kStatusActCount);
    if (actChanged && (statusAct || T.downedNow))
        T.lastDiscoverMs = 0;

    DiscoverOne(T, now, statusAct);
    BlockTick(T, now);

    if (now - T.lastHeartbeatMs >= kHeartbeatMs) {
        T.lastHeartbeatMs = now;
        logFile << "PS: " << SlotName(T.slot) << " @" << std::hex << T.body
                << std::dec << " hb: cStatus="
                << (T.blockFound[0] ? "found" : "scanning")
                << " mgr=" << (T.blockFound[1] ? "found" : "scanning")
                << " downed=" << (T.downedNow ? 1 : 0)
                << " everRevived=" << (T.everRevived ? 1 : 0) << std::endl;
    }
}

void Tick()
{
    if (!InWorld()) return;
    const DWORD now = GetTickCount();
    if (s_lastTick && now - s_lastTick < kTickMs) return;
    s_lastTick = now;

    uintptr_t bodies[kMaxBodies] = {};
    int slots[kMaxBodies] = {};
    int n = 0;
    const uintptr_t ar = ArisenBody();
    if (ar) {
        bodies[n] = ar;
        slots[n] = PARTY_ARISEN;
        ++n;
    }
    for (int r = 0; r < 3 && n < kMaxBodies; ++r) {
        uintptr_t b = 0;
        if (!PartyRecordInfo(r, 0, 0, &b) || !b) continue;
        bodies[n] = b;
        slots[n] = PARTY_MAIN + r;
        ++n;
    }
    if (!n) return;

    SyncBodies(bodies, slots, n);
    for (int i = 0; i < s_nBody; ++i)
        BodyTick(s_body[i], now);
}

void FillMemberStatus(uintptr_t body, int slot, PartyCombatMember& M)
{
    (void)slot;
    if (!body) return;
    BodyTrack* T = FindTrack(body);
    if (!T) return;
    // Свежесть: акт старше kFreshMs — данные уже не про «сейчас».
    const DWORD now = GetTickCount();
    const bool fresh = T->haveAct && (now - T->lastActMs < kFreshMs);
    M.downedValid = fresh && T->downedNow;
    M.downedRevivable = M.downedValid && T->everRevived;
    // statusMask/statusValid остаются 0/false: 84.16 не маппит поля блока
    // на именованные статусы (раскладка станет известна из PS-строк).
}

void DumpSnapshot()
{
    logFile << "PS: ===== manual party-status snapshot: " << s_nBody
            << " bodies =====" << std::endl;
    for (int i = 0; i < s_nBody; ++i) {
        BodyTrack& T = s_body[i];
        logFile << "PS:  " << SlotName(T.slot) << " @" << std::hex << T.body
                << std::dec
                << " act=" << (T.lastAct[0] ? T.lastAct : "?")
                << " downed=" << (T.downedNow ? 1 : 0);
        if (T.downedNow)
            logFile << " downed+" << (GetTickCount() - T.downedSinceMs)
                    << "ms";
        logFile << " everRevived=" << (T.everRevived ? 1 : 0) << std::endl;
        for (int b = 0; b < kNBlocks; ++b) {
            if (!T.blockFound[b]) {
                logFile << "PS:   " << kBlocks[b].cls
                        << " not-found (scanning)" << std::endl;
                continue;
            }
            const uint32_t nwords = kBlocks[b].size / 4;
            uint32_t data[kBlockMaxDwords] = {};
            if (!Rd((void*)T.blockPtr[b], data, (size_t)nwords * 4)) {
                logFile << "PS:   " << kBlocks[b].cls << " read-fail"
                        << std::endl;
                continue;
            }
            logFile << "PS:   " << kBlocks[b].cls << " @" << std::hex
                    << T.blockPtr[b] << std::dec
                    << " off=+0x" << std::hex << T.blockOff[b] << std::dec
                    << " chg=" << T.changeCount[b] << ":";
            for (uint32_t d = 0; d < nwords; ++d) {
                float f = 0.0f;
                memcpy(&f, &data[d], 4);
                char cell[24];
                if (f > -100000.0f && f < 100000.0f
                    && (f > 0.0001f || f < -0.0001f || data[d] == 0))
                    sprintf_s(cell, sizeof(cell), "%8.3f", f);
                else
                    sprintf_s(cell, sizeof(cell), "%8d", (int)data[d]);
                logFile << " " << cell;
            }
            logFile << std::endl;
        }
    }
    logFile << "PS: ===== end party-status snapshot =====" << std::endl;
}

void Init()
{
    memset(s_body, 0, sizeof(s_body));
    s_nBody = 0;
    s_lastTick = 0;
    lstrcpynA(s_status, "party status: armed", sizeof(s_status));
    logFile << "PS: observer armed (cStatus + cEffectStatusManager"
            << " + downed/revive FSM; read-only, no writes)" << std::endl;
}

void Shutdown()
{
    logFile << "PS: observer shutdown (bodies=" << s_nBody << ")" << std::endl;
    s_nBody = 0;
    s_lastTick = 0;
}

const char* Status() { return s_status; }

} // namespace PartyStatus
} // namespace Runtime

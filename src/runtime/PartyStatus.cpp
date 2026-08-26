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

static const DWORD kFreshMs         = 5000;   // свежесть FSM для снапшота
// 84.17: опрос имён детей (лог 24: cStatus не найден по указателю —
// видно, что на самом деле висит на теле партии).
static const int    kSurveyMaxNames = 12;
static const DWORD  kSurveyLogMs    = 15000;  // строка опроса на тело

// --- действия ----------------------------------------------------------
//
// 84.24: состояние падения читается с ТЕЛА, которое упало.
// Пешка HP=0: CmcNeardeath / CmcDead / DmgDownDead.
// Пешка в Разлом: CmcReturn. Подъём succor: выход из neardeath в обычный
// акт (не Return) — отдельного cPlActCmcRevive в атласе нет.
// Нокдаун: DmgDown → DmgStandUp, это не succor.
// cPlReviveCMC — акт Аризена «я поднимаю пешку». На теле пешки игнор.
// cPlActDead на Аризене — не succor-жертва (пешки ГГ не воскрешают).
static const char* kKnockdownActs[] = {
    "cPlActDmgDown",
    "cPlActDmgDownDamage"
};
static const int kKnockdownActCount =
    (int)(sizeof(kKnockdownActs) / sizeof(kKnockdownActs[0]));
static const char* kNeardeathActs[] = {
    "cPlActCmcNeardeath",
    "cPlActCmcDead",
    "cPlActDmgDownDead",
    "cPlActDmgCrumbleDead"
};
static const int kNeardeathActCount =
    (int)(sizeof(kNeardeathActs) / sizeof(kNeardeathActs[0]));
static const char* kRiftAct  = "cPlActCmcReturn";
static const char* kRaiseAct = "cPlReviveCMC";   // только слот Arisen
static const char* kArisenDeadAct = "cPlActDead";
static const int kKindNone      = 0;
static const int kKindKnockdown = 1;
static const int kKindNeardeath = 2;

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

    // FSM падения (канал B) — на этом теле, не на чужом.
    bool      downedNow;
    int       downedKind;   // kKindNone / Knockdown / Neardeath
    DWORD     downedSinceMs;
    bool      everRevived;  // пешка: RAISED из neardeath хотя бы раз
    bool      raiseNow;     // Arisen stretch cPlReviveCMC
    bool      deadNow;      // Arisen stretch cPlActDead

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

// --- канал B: падение/подъём с тела, которое упало (84.24) --------------
//
// Пешка:
//   * -> CmcNeardeath|CmcDead|DmgDownDead     DOWNED (ждёт succor)
//   neardeath -> CmcReturn                    RIFTED
//   neardeath -> обычный акт                  RAISED (тело встало)
//   * -> DmgDown|DmgDownDamage                KNOCKDOWN (не succor)
//   knockdown -> StandUp/обычный              KNOCKDOWN-END
//   knockdown -> neardeath                    эскалация в DOWNED
//   cPlReviveCMC на пешке                     игнор (акт Аризена)
// Аризен:
//   cPlReviveCMC                              RAISE (поднимает пешку)
//   cPlActDead                                DEAD (не succor-жертва)
//   DmgDown                                   KNOCKDOWN, как у пешки
static void ClearDowned(BodyTrack& T)
{
    T.downedNow = false;
    T.downedKind = kKindNone;
}

static void FsmTick(BodyTrack& T, const char* act, DWORD now)
{
    const bool isPawn = (T.slot != PARTY_ARISEN);
    const bool isKnockdown = ActInSet(act, kKnockdownActs, kKnockdownActCount);
    bool isNeardeath = ActInSet(act, kNeardeathActs, kNeardeathActCount);
    if (isPawn && !strcmp(act, kArisenDeadAct))
        isNeardeath = true;
    const bool isRift = !strcmp(act, kRiftAct);
    const bool isPlayerRaise = !strcmp(act, kRaiseAct);
    const bool isArisenDead = !isPawn && !strcmp(act, kArisenDeadAct);

    if (isPlayerRaise) {
        if (isPawn)
            return;
        if (!T.downedNow && !T.raiseNow) {
            T.raiseNow = true;
            logFile << "PS: Arisen RAISE act=cPlReviveCMC" << std::endl;
        }
        return;
    }
    if (T.raiseNow) T.raiseNow = false;

    if (isArisenDead) {
        if (T.downedNow) ClearDowned(T);
        if (!T.deadNow) {
            T.deadNow = true;
            logFile << "PS: Arisen DEAD act=cPlActDead" << std::endl;
        }
        return;
    }
    if (T.deadNow) {
        T.deadNow = false;
        logFile << "PS: Arisen DEAD-END act=" << act << std::endl;
        return;
    }

    if (isNeardeath && T.downedKind != kKindNeardeath) {
        const bool first = !T.downedNow;
        T.downedNow = true;
        T.downedKind = kKindNeardeath;
        if (first) T.downedSinceMs = now;
        logFile << "PS: " << SlotName(T.slot) << " DOWNED act=" << act
                << std::endl;
        return;
    }
    if (isKnockdown && !T.downedNow) {
        T.downedNow = true;
        T.downedKind = kKindKnockdown;
        T.downedSinceMs = now;
        logFile << "PS: " << SlotName(T.slot) << " KNOCKDOWN act=" << act
                << std::endl;
        return;
    }
    if (!T.downedNow) {
        if (isRift && isPawn)
            logFile << "PS: " << SlotName(T.slot) << " RIFTED act=" << act
                    << std::endl;
        return;
    }

    if (T.downedKind == kKindNeardeath) {
        if (isNeardeath) return;
        if (isRift) {
            logFile << "PS: " << SlotName(T.slot) << " RIFTED act=" << act
                    << " downed+" << (now - T.downedSinceMs) << "ms"
                    << std::endl;
            ClearDowned(T);
            return;
        }
        if (isPawn) T.everRevived = true;
        logFile << "PS: " << SlotName(T.slot) << " RAISED act=" << act
                << " downed+" << (now - T.downedSinceMs) << "ms" << std::endl;
        ClearDowned(T);
        return;
    }

    if (isKnockdown) return;
    if (isRift && isPawn) {
        logFile << "PS: " << SlotName(T.slot) << " RIFTED act=" << act
                << std::endl;
        ClearDowned(T);
        return;
    }
    logFile << "PS: " << SlotName(T.slot) << " KNOCKDOWN-END act=" << act
            << std::endl;
    ClearDowned(T);
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

    // Один раз на тело: повтор каждые 15 с заливал лог, карта детей не меняется.
    if (nNames > 0 && T.lastSurveyLogMs == 0) {
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
    // succor-revivable только у пешки, и только если neardeath уже
    // заканчивался RAISED на этом теле. Аризен пешками не поднимается.
    M.downedRevivable = M.downedValid
                     && T->everRevived
                     && T->slot != PARTY_ARISEN
                     && T->downedKind == kKindNeardeath;
    // statusMask/statusValid остаются 0/false: 84.16 не маппит поля блока
    // на именованные статусы (раскладка станет известна из PS-строк).
}

// 84.30: hex dump of the character RECORD (HP/stam live here) and the
// scene BODY. Status is not a named child of the body; A/B is a hex
// diff of these two blobs. Same button as before. Read-only.
static const uint32_t kSheetRecordBytes = 0x1660;
static const uint32_t kSheetPlayerBody  = 0x5A10;
static const uint32_t kSheetPawnBody    = 0x58E0;

static void SheetHex(const char* tag, uintptr_t base, uint32_t bytes)
{
    if (!tag || !base || !bytes) return;
    BYTE row[16];
    for (uint32_t off = 0; off < bytes; off += 16) {
        const uint32_t n = (bytes - off >= 16u) ? 16u : (bytes - off);
        if (!Rd((void*)(base + off), row, n)) {
            logFile << tag << " +" << std::hex << off << std::dec
                    << " read-fail" << std::endl;
            return;
        }
        char line[96];
        int pos = sprintf_s(line, sizeof(line), "%s +%04X", tag, (int)off);
        for (uint32_t i = 0; i < n && pos > 0 && pos < (int)sizeof(line) - 4; ++i)
            pos += sprintf_s(line + pos, sizeof(line) - (size_t)pos,
                             " %02X", row[i]);
        if (pos > 0) logFile << line << std::endl;
    }
}

static uintptr_t SheetRecordOf(int slot)
{
    if (!pBase || !*pBase) return 0;
    if (slot == PARTY_ARISEN)
        return (uintptr_t)(*pBase) + 0xA7000;
    if (slot >= PARTY_MAIN && slot <= PARTY_HIRED2)
        return (uintptr_t)(*pBase) + 0xA7000 + 0x7F0
             + (uintptr_t)(slot - PARTY_MAIN) * 0x1660;
    return 0;
}

static void DumpOneSheet(int slot, uintptr_t rec, uintptr_t body)
{
    const char* name = SlotName(slot);
    logFile << "PS: SHEET " << name
            << " rec=0x" << std::hex << rec
            << " body=0x" << body << std::dec << std::endl;
    if (rec) {
        float hp = 0, hpMax = 0, hpRec = 0;
        float sta = 0, staMax = 0, staRec = 0;
        float str = 0, def = 0, mag = 0, mdef = 0;
        int32_t voc = 0;
        uint16_t lvl = 0;
        const bool okHp  = Rd((void*)(rec + 0x96C), &hp, 4)
                        && Rd((void*)(rec + 0x970), &hpMax, 4)
                        && Rd((void*)(rec + 0x974), &hpRec, 4);
        const bool okSta = Rd((void*)(rec + 0x978), &sta, 4)
                        && Rd((void*)(rec + 0x97C), &staMax, 4)
                        && Rd((void*)(rec + 0x980), &staRec, 4);
        const bool okCore = Rd((void*)(rec + 0x984), &str, 4)
                         && Rd((void*)(rec + 0x988), &def, 4)
                         && Rd((void*)(rec + 0x98C), &mag, 4)
                         && Rd((void*)(rec + 0x990), &mdef, 4);
        Rd((void*)(rec + 0x6E0), &voc, 4);
        Rd((void*)(rec + 0xDD0), &lvl, 2);
        char line[320];
        sprintf_s(line, sizeof(line),
            "PS: SHEET %s voc=%d lvl=%u hp=%s%.1f/%.1f rec=%.1f "
            "sta=%s%.1f/%.1f rec=%.1f core=%s STR=%.1f DEF=%.1f MAG=%.1f MDEF=%.1f",
            name, (int)voc, (unsigned)lvl,
            okHp ? "" : "?", hp, hpMax, hpRec,
            okSta ? "" : "?", sta, staMax, staRec,
            okCore ? "" : "?", str, def, mag, mdef);
        logFile << line << std::endl;
        {
            int32_t cnt = 0, id0 = -1;
            Rd((void*)(rec + 0x0A2C), &cnt, 4);
            char st[220];
            int n = sprintf_s(st, sizeof(st),
                              "PS: SHEET %s status count=%d", name, (int)cnt);
            for (int i = 0; i < 40 && n > 0 && n < 180; ++i) {
                if (!Rd((void*)(rec + 0x0A30 + (uint32_t)i * 4), &id0, 4))
                    break;
                if (id0 == -1) continue;
                float tm = 0, p0 = 0, p1 = 0;
                Rd((void*)(rec + 0x0AD0 + (uint32_t)i * 4), &tm, 4);
                Rd((void*)(rec + 0x0B70 + (uint32_t)i * 4), &p0, 4);
                Rd((void*)(rec + 0x0C10 + (uint32_t)i * 4), &p1, 4);
                n += sprintf_s(st + n, sizeof(st) - (size_t)n,
                               " [%d]=id%d t=%.1f p0=%.2f p1=%.2f",
                               i, (int)id0, tm, p0, p1);
            }
            logFile << st << std::endl;
        }
        char tag[40];
        sprintf_s(tag, sizeof(tag), "PS: REC %s", name);
        SheetHex(tag, rec, kSheetRecordBytes);
    } else {
        logFile << "PS: SHEET " << name << " record unread" << std::endl;
    }
    if (body) {
        char kind[48] = {};
        NameOfLiveObject(body, kind, sizeof(kind));
        uint32_t bodyBytes = kSheetPawnBody;
        if (!strcmp(kind, "uPlayer")) bodyBytes = kSheetPlayerBody;
        logFile << "PS: SHEET " << name << " bodyKind="
                << (kind[0] ? kind : "?") << " bytes=" << bodyBytes
                << std::endl;
        char tag[40];
        sprintf_s(tag, sizeof(tag), "PS: BODY %s", name);
        SheetHex(tag, body, bodyBytes);
        uintptr_t rsp = 0;
        if (RdPtr((void*)(body + 0x2710), &rsp) && rsp && LooksHeap(rsp)) {
            char rn[48] = {};
            NameOfLiveObject(rsp, rn, sizeof(rn));
            logFile << "PS: SHEET " << name << " +0x2710 "
                    << (rn[0] ? rn : "?") << " @0x" << std::hex << rsp
                    << std::dec << std::endl;
            if (!strcmp(rn, "rStatusParam")) {
                sprintf_s(tag, sizeof(tag), "PS: RSP %s", name);
                SheetHex(tag, rsp, 120);
            }
        }
    } else {
        logFile << "PS: SHEET " << name << " body unread" << std::endl;
    }
}

static void DumpPartySheets()
{
    logFile << "PS: ===== party SHEET (record + body hex; status A/B) ====="
            << std::endl;
    bool dumped[PARTY_COMBAT_SLOTS] = {};
    for (int i = 0; i < s_nBody; ++i) {
        const int slot = s_body[i].slot;
        DumpOneSheet(slot, SheetRecordOf(slot), s_body[i].body);
        if (slot >= 0 && slot < PARTY_COMBAT_SLOTS) dumped[slot] = true;
    }
    for (int slot = 0; slot < PARTY_COMBAT_SLOTS; ++slot) {
        if (dumped[slot]) continue;
        const uintptr_t rec = SheetRecordOf(slot);
        uintptr_t body = 0;
        if (slot == PARTY_ARISEN) body = ArisenBody();
        else PartyRecordInfo(slot - PARTY_MAIN, 0, 0, &body);
        if (!rec && !body) continue;
        DumpOneSheet(slot, rec, body);
    }
    logFile << "PS: ===== end party SHEET =====" << std::endl;
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
    DumpPartySheets();
}

void Init()
{
    memset(s_body, 0, sizeof(s_body));
    s_nBody = 0;
    s_lastTick = 0;
    lstrcpynA(s_status, "party status: armed", sizeof(s_status));
    logFile << "PS: observer armed (cStatus child-scan + SHEET record/body hex"
            << " + pawn-body downed/raised/rifted; Arisen RAISE="
            << "cPlReviveCMC; read-only, no writes)" << std::endl;
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

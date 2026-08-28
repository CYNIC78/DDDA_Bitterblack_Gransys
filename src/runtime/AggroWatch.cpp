// Runtime::Aggro — прибор «на кого смотрит пачка». См. AggroWatch.h.
//
// Observer demand itself is read-only. Legacy manual PIN/FOCUS research writes
// remain gated by [aggro] watch. The Director owns a separate response-aware
// wolf target lease with exact record-slot/body validation before each mutation;
// it never treats observer demand as write permission.

#include "stdafx.h"
#include "AggroWatch.h"
#include "Runtime.h"
#include "MemProbe.h"

namespace Runtime {
namespace Aggro {

// Защищённые чтения живут в фундаменте рантайма (Rd/RdPtr/RegionOk).
using namespace Mem;

// --- настройки обхода -------------------------------------------------------
//
// Докуда смотреть в теле врага. У гоблина ссылки на партию встречались по
// +0x2B98, +0x322C, +0x5CA4, +0x6234 (ANATOMY_EM0100 §124, CHANGELOG_DAY19),
// поэтому 0x6800 покрывает известные случаи с запасом. Читаем килобайтными
// кусками: неудачный кусок пропускаем, а не бросаем весь обход — у разных
// видов тела разной длины, и упереться в конец аллокации это норма.
static const uint32_t kScanBytes    = 0x6800;
static const uint32_t kChunk        = 0x400;
static const uint32_t kRediscoverMs = 5000;   // как часто пересматривать тело
// Shared uEnemy prefix (goblin body is 256 B shorter at the tail). Wolf
// recon §15.2 measured this roster; log 23 found the same stride on uEm0100.
static const uint32_t kEm0100RosterBase   = 0x2FA0;
static const uint32_t kEm0100RosterStride = 0x28C;
static const int      kEm0100RosterCount  = 4;

static bool IsDirectorKind(const char* kind)
{
    return kind && kind[0]
        && (strcmp(kind, "uEm0200") == 0
         || strcmp(kind, "uEm0100") == 0
         || strcmp(kind, "uEm0101") == 0
         || strcmp(kind, "uEm0400") == 0);
}

static bool IsGoblinFamily(const char* kind)
{
    return kind && (strcmp(kind, "uEm0100") == 0
                 || strcmp(kind, "uEm0101") == 0);
}

static bool IsSaurianKind(const char* kind)
{
    return kind && strcmp(kind, "uEm0400") == 0;
}

static bool  s_enabled = false;         // explicit research/watch toggle
static bool  s_directorObserver = false;// quiet, read-only product demand
static bool  s_logEvents = false;       // verbose research events are opt-in
static Row   s_row[kMaxRows];
static int   s_nRow = 0;
static char  s_status[192] = "aggro watch: off";
static DWORD s_lastTick = 0;
static int   s_lastIdentityMask = -1; // log only fixed-slot availability changes

// Сводная статистика замера — то, ради чего прибор существует. Считаем,
// сколько тиков каждый член партии продержался целью в подвижных слотах.
static uint32_t s_holdTicks[kMemberSlots];
static uint32_t s_switchTotal = 0;

// Схождение стаи. Порог 3: две особи на одном члене — обычное дело, три и
// больше уже событие. Логируем только рост, иначе схождение на пять
// секунд залило бы лог сорока одинаковыми строками.
static const int kConvergeMin = 3;
static Converge  s_conv[kMemberSlots];
static int       s_convLogged[kMemberSlots];

// CARDWATCH (79.0, §18 AGGRO_RECON).
//
// Две особи, у которых найден ростер, ведутся непрерывно: кандидаты в
// поля карточки + живая дистанция до члена карточки в одной строке.
// Тело — не индексы строк (ForgetMissing сдвигает их), а адреса.
static bool      s_cardWatch = false;
static uintptr_t s_cardBody[2];
static int       s_nCard = 0;

// Двенадцать кандидатов в покарточные поля. Измерено на uEm0200 в боях
// 76.1/78.0: база и ШАГ карточки у каждой особи вычисляются по-прежнему
// арифметически (ClassifySlots), здесь только оффсеты ВНУТРИ карточки —
// они и есть результат замера, как и требует FIX_RULES §6.
static const uint32_t kCardFields[12] = {
    0x08, 0x0C, 0x10, 0x14,
    0x260, 0x264, 0x268, 0x26C,
    0x274, 0x278, 0x27C, 0x280
};
static const char* kCardNames[12] = {
    "08", "0c", "10", "14",
    "260", "264", "268", "26c",
    "274", "278", "27c", "280"
};
// Какие из полей печатать как float (остальные — как int). По поведению
// из ростеров 78.0: 10/268/26c/27c/280 дают дробные значения.
static const bool kCardFloat[12] = {
    false, false, true, true,
    false, false, true, true,
    false, false, true, true
};

// --- PIN (80.0) — первая мутация трека --------------------------------
//
// Замер 79.0 (AGGRO_RECON §19) дал формулу внимания:
//     attention = 300 - 10*d(m)     (линия «свежего восприятия»)
//     attention -= 9/тик            (затухание, ~5 c до нуля)
//     target ~= argmax(attention + урон-члены)
// Штырь перебивает argmax, удерживая внимание заштыренного члена на
// потолке 300. Значение 300 — В НАТИВНОМ ДИАПАЗОНЕ (0..300), не «магия».
static int      s_pinMember = -1;   // Member, -1 = выключен
static int      s_pinScope  = 0;    // 0 = ближайший волк, 1 = все
static bool     s_pinSuppress = false; // 81.0: гасить чужие карты до 0
static bool     s_pinFakehit = false;  // 82.0: пере-заявлять блок B
static int      s_pinFocus = MEMBER_NONE; // 83.0: член под ручным FOCUS

// Продуктовая response-aware аренда цели для Monster Director. Она не включает
// research watch и не зависит от порядка тел пешек. Director передаёт ожидаемое
// тело своей record-slot цели; перед каждой записью мы заново разрешаем тот же
// слот через PartyRecordInfo и требуем точное равенство.
static int       s_directorFocus = MEMBER_NONE;
static uintptr_t s_directorExpectedBody = 0;
static uintptr_t s_directorExcludedBody = 0;
static int       s_directorResponse = DIRECTOR_RESPONSE_NONE;
static char      s_directorKind[16] = "uEm0200";
static uint32_t  s_directorWrites = 0;
static bool      s_directorIdentityBlockLogged = false;
static float    s_pinFakehitValue = 150.0f;
static uint32_t s_pinWrites = 0;
static uint32_t s_pinSuppWrites = 0;
static uint32_t s_pinFakehitWrites = 0;
static uint32_t s_pinRollbacks = 0;
static uint32_t s_pinUnsafeSkips = 0;
static uint32_t s_pinLastLog = 0;
static uint32_t s_pinLastAnomaly = 0;
static const float kPinValue = 300.0f;         // perception ceiling (fC=4, линия при 0 м)
static const float kCombatPinValue = 500.0f;   // combat ceiling (fC=2, замер 82.0 §25.3)
static const float kSuppValue = 0.0f;          // нативное «полностью затухшее»
// Goblin combat ceiling (84.17, лог 24): наблюдаемый нативный максимум fC=5.
static const float kGobCombatPinValue = 484.0f;

// Журнал «что писали и когда» — для дельта-лога: не лить 30 строк в
// секунду, а одну на карточку, когда значение реально изменилось.
struct PinLog {
    uintptr_t body;
    uint32_t  cardOff;
    uint32_t  lastMs;
    float     lastWas;
};
static PinLog s_pinLog[128];
static int    s_nPinLog = 0;

// FOCUS не имеет права врать: если ручные тумблеры (pin/suppress/fakehit)
// рассобрали связку, FOCUS снимается с логом, а не молча. Если штырь
// вручную передёрнут на другого члена при живой связке — FOCUS следует
// за штырём (он и есть «кто зафокусирован»).
static void FocusRecheck(const char* why)
{
    if (s_pinFocus < 0) return;
    if (s_pinMember < 0 || !s_pinSuppress || !s_pinFakehit) {
        logFile << "Aggro: FOCUS disengaged: " << MemberName(s_pinFocus)
                << "  (manual override: " << why << ")" << std::endl;
        s_pinFocus = MEMBER_NONE;
        return;
    }
    if (s_pinMember != s_pinFocus) {
        logFile << "Aggro: FOCUS follows pin: "
                << MemberName(s_pinMember) << "  (" << why << ")"
                << std::endl;
        s_pinFocus = s_pinMember;
    }
}

// --- партия -----------------------------------------------------------------

struct PartyRef {
    uintptr_t body;
    int       member;
};

// Собрать живые тела партии с ДЕТЕРМИНИРОВАННЫМИ номерами.
//
// Старый путь назначал Hired1/Hired2 по порядку PawnBodyAt(). Для ручного
// прибора это было терпимо, для автоматического Director — нет: перестановка
// тел могла направить стаю на другую пешку. Теперь каждый pawn member выводится
// только из подтверждённого character-record index: record 0 = MainPawn,
// record 1 = Hired1, record 2 = Hired2. Если PartyRecon не связал запись с
// телом, слот просто отсутствует — никаких догадок по порядку.
static int CollectParty(PartyRef* out, int cap)
{
    int n = 0;
    const uintptr_t ar = ArisenBody();
    if (ar && n < cap) {
        out[n].body = ar;
        out[n].member = MEMBER_ARISEN;
        ++n;
    }

    for (int recordIdx = 0; recordIdx < 3 && n < cap; ++recordIdx) {
        uintptr_t body = 0;
        if (!PartyRecordInfo(recordIdx, 0, 0, &body) || !body) continue;
        out[n].body = body;
        out[n].member = MEMBER_MAIN + recordIdx;
        ++n;
    }
    return n;
}

static const char* MemberIdentityStatus(int member, int state)
{
    // state: 0 exact, 1 record unavailable, 2 body unresolved/duplicate,
    //        3 absent (record жива, 0 тел — рифт / таймер воскрешения).
    static const char* kStatus[4][4] = {
        { "identity-Arisen-exact",   "identity-Arisen-record-unavailable",
          "identity-Arisen-body-unresolved-or-duplicate",
          "identity-Arisen-absent" },
        { "identity-MainPawn-exact", "identity-MainPawn-record-unavailable",
          "identity-MainPawn-body-unresolved-or-duplicate",
          "identity-MainPawn-absent" },
        { "identity-Hired1-exact",   "identity-Hired1-record-unavailable",
          "identity-Hired1-body-unresolved-or-duplicate",
          "identity-Hired1-absent" },
        { "identity-Hired2-exact",   "identity-Hired2-record-unavailable",
          "identity-Hired2-body-unresolved-or-duplicate",
          "identity-Hired2-absent" }
    };
    if (member < MEMBER_ARISEN || member > MEMBER_HIRED2)
        return "identity-invalid-slot";
    if (state < 0 || state > 3) state = 2;
    return kStatus[member][state];
}

const char* ResolveMemberBodyStatus(int member, uintptr_t* bodyOut)
{
    if (bodyOut) *bodyOut = 0;
    if (member < MEMBER_ARISEN || member > MEMBER_HIRED2)
        return "identity-invalid-slot";

    uintptr_t body = 0;
    if (member == MEMBER_ARISEN) {
        body = ArisenBody();
        // Аризен в Разлом не уходит. Ноль тел — дыра, не vacant-слот.
        if (!body) return MemberIdentityStatus(member, 2);
    } else {
        const int recordIdx = member - MEMBER_MAIN;
        if (!PartyRecordInfo(recordIdx, 0, 0, &body))
            return MemberIdentityStatus(member, 1);
        const int claims = PartyRecordBodyClaimCount(recordIdx);
        if (claims == 0) return MemberIdentityStatus(member, 3);
        if (claims != 1 || !body) return MemberIdentityStatus(member, 2);
    }
    if (bodyOut) *bodyOut = body;
    return MemberIdentityStatus(member, 0);
}

bool ResolveMemberBody(int member, uintptr_t* bodyOut)
{
    const char* status = ResolveMemberBodyStatus(member, bodyOut);
    return status && strstr(status, "-exact") != 0;
}

static void LogIdentityAvailabilityIfChanged()
{
    int mask = 0;
    for (int member = MEMBER_ARISEN; member <= MEMBER_HIRED2; ++member) {
        uintptr_t body = 0;
        if (ResolveMemberBody(member, &body) && body) mask |= 1 << member;
    }
    if (mask == s_lastIdentityMask) return;
    s_lastIdentityMask = mask;
    logFile << "Aggro: fixed-slot identity availability"
            << " Arisen=" << ((mask & (1 << MEMBER_ARISEN)) ? "exact" : "unresolved")
            << " MainPawn=" << ((mask & (1 << MEMBER_MAIN)) ? "exact" : "unresolved")
            << " Hired1=" << ((mask & (1 << MEMBER_HIRED1)) ? "exact" : "unresolved")
            << " Hired2=" << ((mask & (1 << MEMBER_HIRED2)) ? "exact" : "unresolved")
            << std::endl;
}

const char* MemberName(int member)
{
    switch (member) {
    case MEMBER_ARISEN: return "Arisen";
    case MEMBER_MAIN:   return "MainPawn";
    case MEMBER_HIRED1: return "Hired1";
    case MEMBER_HIRED2: return "Hired2";
    case MEMBER_OTHERPAWN: return "Pawn(?)";
    default:            return "-";
    }
}

// --- обход тела -------------------------------------------------------------

// Найти в теле врага все смещения, где лежит указатель на члена партии.
// Смещения не назначаются заранее: что нашли, то и наблюдаем.
static int DiscoverSlots(uintptr_t body, const PartyRef* party, int nParty,
                         Slot* out, int cap)
{
    int n = 0;
    BYTE buf[kChunk];

    for (uint32_t base = 0; base + kChunk <= kScanBytes && n < cap; base += kChunk) {
        if (!RegionOk(body + base, kChunk)) continue;
        if (!Rd((void*)(body + base), buf, kChunk)) continue;

        for (uint32_t i = 0; i + 4 <= kChunk && n < cap; i += 4) {
            uintptr_t v = 0;
            memcpy(&v, buf + i, 4);
            if (!v) continue;
            for (int p = 0; p < nParty; ++p) {
                if (v != party[p].body) continue;
                out[n].off      = base + i;
                out[n].member   = party[p].member;
                out[n].switches = 0;
                out[n].holdMs   = 0;
                out[n].sinceMs  = GetTickCount();
                ++n;
                break;
            }
        }
    }
    return n;
}

// Кто лежит в слоте сейчас. MEMBER_NONE — там больше не член партии
// (обычная ситуация: цель сброшена или указывает на врага/предмет).
static int ReadSlot(uintptr_t body, uint32_t off,
                    const PartyRef* party, int nParty)
{
    uintptr_t v = 0;
    if (!RdPtr((void*)(body + off), &v)) return MEMBER_NONE;
    if (!v) return MEMBER_NONE;
    for (int p = 0; p < nParty; ++p)
        if (v == party[p].body) return party[p].member;

    // Не наш член партии. Но это может быть НЕРАЗРЕШЁННАЯ наёмная пешка —
    // тогда молчаливый MEMBER_NONE превратил бы смену цели в «цель
    // потеряна». Спрашиваем у игры имя класса: uCmc/uPl* здесь означает
    // именно пешку, которую разбор партии ещё не нашёл.
    if (!LooksHeap(v)) return MEMBER_NONE;
    char nm[48] = {};
    if (NameOfLiveObject(v, nm, sizeof(nm)) && nm[0]
        && (strncmp(nm, "uCmc", 4) == 0 || strncmp(nm, "uPl", 3) == 0))
        return MEMBER_OTHERPAWN;
    return MEMBER_NONE;
}

// --- классификация слотов ---------------------------------------------------
//
// Ростер опознаётся не по зашитому оффсету, а по ФОРМЕ: три и больше
// слота, стоящие через одинаковый шаг. Постоянная связь так не ложится, а
// массив карточек ложится всегда — у любого вида, с любым шагом.
static void ClassifySlots(Row& R)
{
    for (int i = 0; i < R.nSlots; ++i) R.slot[i].kind = SLOT_TARGET;
    R.rosterBase = 0; R.rosterStride = 0; R.rosterCount = 0;
    if (R.nSlots < 3) return;

    // Перебираем пары как кандидатов на (база, шаг) и ищем самую длинную
    // прогрессию. Слотов максимум kMaxSlots — перебор дешёвый.
    int bestN = 0; uint32_t bestBase = 0, bestStride = 0;
    for (int a = 0; a < R.nSlots; ++a) {
        for (int b = 0; b < R.nSlots; ++b) {
            if (a == b) continue;
            if (R.slot[b].off <= R.slot[a].off) continue;
            const uint32_t stride = R.slot[b].off - R.slot[a].off;
            if (stride < 0x40) continue;           // слишком плотно для карточки
            int n = 1;
            for (;;) {
                const uint32_t want = R.slot[a].off + (uint32_t)n * stride;
                bool found = false;
                for (int k = 0; k < R.nSlots; ++k)
                    if (R.slot[k].off == want) { found = true; break; }
                if (!found) break;
                ++n;
                if (n > kMaxParty) break;
            }
            if (n >= 3 && n > bestN) { bestN = n; bestBase = R.slot[a].off; bestStride = stride; }
        }
    }
    if (bestN < 3) return;

    R.rosterBase = bestBase; R.rosterStride = bestStride; R.rosterCount = bestN;
    for (int k = 0; k < bestN; ++k) {
        const uint32_t want = bestBase + (uint32_t)k * bestStride;
        for (int i = 0; i < R.nSlots; ++i)
            if (R.slot[i].off == want) R.slot[i].kind = SLOT_ROSTER;
    }
}

static bool IsEm0100RosterOff(uint32_t off)
{
    if (off < kEm0100RosterBase) return false;
    const uint32_t d = off - kEm0100RosterBase;
    return (d % kEm0100RosterStride) == 0
        && (int)(d / kEm0100RosterStride) < kEm0100RosterCount;
}

// Empty goblin cards are invisible to DiscoverSlots. Force the proven
// 0x2FA0/0x28C x4 array so ALERT can pin or wake the mark card.
static void EnsureGoblinRosterSlots(Row& R, const PartyRef* party, int nParty)
{
    // Same proven 0x2FA0/0x28C x4 on uEm0100 / uEm0101 / live uEm0400 (84.29).
    // Forces the measured roster so ClassifySlots cannot pin a false 0x14E0/0x48.
    if (!IsGoblinFamily(R.kind) && !IsSaurianKind(R.kind)) return;
    const DWORD now = GetTickCount();
    for (int k = 0; k < kEm0100RosterCount; ++k) {
        const uint32_t off = kEm0100RosterBase
                           + (uint32_t)k * kEm0100RosterStride;
        if (!RegionOk(R.body + off, 20)) continue;
        int existing = -1;
        for (int i = 0; i < R.nSlots; ++i)
            if (R.slot[i].off == off) { existing = i; break; }
        if (existing >= 0) {
            R.slot[existing].kind = SLOT_ROSTER;
            continue;
        }
        if (R.nSlots >= kMaxSlots) {
            int drop = -1;
            for (int i = R.nSlots - 1; i >= 0; --i)
                if (R.slot[i].kind != SLOT_ROSTER) { drop = i; break; }
            if (drop < 0) continue;
            R.slot[drop] = R.slot[R.nSlots - 1];
            --R.nSlots;
        }
        Slot& S = R.slot[R.nSlots++];
        memset(&S, 0, sizeof(S));
        S.off = off;
        S.kind = SLOT_ROSTER;
        S.member = MEMBER_NONE;
        S.sinceMs = now;
        uintptr_t p = 0;
        if (RdPtr((void*)(R.body + off), &p) && p) {
            for (int q = 0; q < nParty; ++q)
                if (p == party[q].body) { S.member = party[q].member; break; }
        }
    }
    R.rosterBase = kEm0100RosterBase;
    R.rosterStride = kEm0100RosterStride;
    R.rosterCount = kEm0100RosterCount;
}

// --- строки наблюдения ------------------------------------------------------

static Row* FindRow(uintptr_t body)
{
    for (int i = 0; i < s_nRow; ++i)
        if (s_row[i].body == body) return &s_row[i];
    return 0;
}

static Row* AddRow(uintptr_t body, const char* kind)
{
    if (s_nRow >= kMaxRows) return 0;
    Row& R = s_row[s_nRow++];
    memset(&R, 0, sizeof(R));
    R.body = body;
    R.best = -1;
    lstrcpynA(R.kind, kind ? kind : "?", sizeof(R.kind));
    return &R;
}

// Забыть тех, кого больше нет в мире: иначе таблица начнёт отвечать про
// давно убитых (та же болезнь, что лечил ForgetMissing у режиссёра).
static void ForgetMissing()
{
    const int n = EnemyCount();
    int w = 0;
    for (int i = 0; i < s_nRow; ++i) {
        bool alive = false;
        for (int k = 0; k < n; ++k)
            if (EnemyBodyAt(k, 0) == s_row[i].body) { alive = true; break; }
        if (!alive) continue;
        if (w != i) s_row[w] = s_row[i];
        ++w;
    }
    s_nRow = w;
}

// --- CARDWATCH: одна особь --------------------------------------------------
//
// Позиция тела: +0x40/+0x44/+0x48 (SOURCE_OF_TRUTH §2, те же смещения, что
// использует PawnHaste). 100 мирских единиц = 1 метр.
static bool ReadBodyPos(uintptr_t body, float* xyz)
{
    return Rd((void*)(body + 0x40), xyz, 12);
}

// Одна особь: по карточке одна строка, только при изменении (дельта) или
// по сердечнику раз в 4 c. Это и есть «эталонная» строка замера 79.0:
// поля карточки и живая дистанция до её члена в одном моменте времени —
// на них и видно, какое поле ЕСТЬ дистанция, а какое — ненависть.
static void CardWatchRow(Row& R, const PartyRef* party, int nParty, DWORD now)
{
    float wp[3] = {};
    const bool havePos = ReadBodyPos(R.body, wp);

    for (int k = 0; k < R.rosterCount && k < kMaxParty; ++k) {
        const uintptr_t card =
            R.body + R.rosterBase + (uintptr_t)k * R.rosterStride;

        // Кто сейчас в этой карточке: указатель лежит в голове карточки.
        uintptr_t p = 0;
        RdPtr((void*)card, &p);
        int      member = MEMBER_NONE;
        uintptr_t mBody = 0;
        if (p) {
            for (int q = 0; q < nParty; ++q) {
                if (p != party[q].body) continue;
                member = party[q].member;
                mBody  = p;
                break;
            }
        }

        // Живая дистанция до члена карточки, метры.
        float dist = -1.0f;
        if (member >= 0 && havePos && mBody) {
            float pp[3] = {};
            if (ReadBodyPos(mBody, pp)) {
                const float dx = wp[0] - pp[0];
                const float dy = wp[1] - pp[1];
                const float dz = wp[2] - pp[2];
                dist = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
            }
        }

        // Двенадцать кандидатов в поля.
        uint32_t v[12];
        for (int f = 0; f < 12; ++f) {
            if (!Rd((void*)(card + kCardFields[f]), &v[f], 4)) return;
        }

        // Дельта: печатаем, только когда что-то сдвинулось. Целые поля —
        // любое изменение; дробные — прыжок больше 0.05; дистанция —
        // больше 15 см. Сердечник 4 c держит шкалу времени, даже если все
        // стоят.
        CardWatchState& C = R.card[k];
        bool changed = !C.have || (now - C.lastMs) >= 4000
                       || member != C.lastMember;
        if (!changed) {
            for (int f = 0; f < 12; ++f) {
                if (v[f] == C.lastVal[f]) continue;
                if (!kCardFloat[f]) { changed = true; break; }
                float a, b;
                memcpy(&a, &C.lastVal[f], 4);
                memcpy(&b, &v[f], 4);
                if (fabsf(a - b) > 0.05f) { changed = true; break; }
            }
        }
        if (!changed && dist >= 0.0f && C.lastDist >= 0.0f
            && fabsf(dist - C.lastDist) > 0.15f)
            changed = true;
        if (!changed) continue;

        // Строка: особь, номер карточки, член, 12 полей, дистанция, цель.
        logFile << "Aggro: CARD @" << std::hex << R.body << std::dec
                << " c" << k << " " << MemberName(member);
        for (int f = 0; f < 12; ++f) {
            float fv;
            memcpy(&fv, &v[f], 4);
            char cell[24];
            if (kCardFloat[f] && fv > -100000.0f && fv < 100000.0f
                && (fv > 0.0001f || fv < -0.0001f || v[f] == 0))
                sprintf_s(cell, "%8.3f", fv);
            else
                sprintf_s(cell, "%8d", (int)v[f]);
            logFile << " " << kCardNames[f] << "=" << cell;
            if (f == 3 || f == 7) logFile << " |";
        }
        char dc[16];
        if (dist >= 0.0f)
            sprintf_s(dc, "d=%6.2fm", dist);
        else
            lstrcpynA(dc, "d=-", sizeof(dc));
        logFile << "  " << dc;
        if (k == 0)
            logFile << "  tgt=" << MemberName(R.targetMember)
                    << "  act " << R.act;
        logFile << std::endl;

        memcpy(C.lastVal, v, sizeof(v));
        C.lastDist = dist;
        C.lastMs   = now;
        C.lastMember = member;
        C.have     = true;
    }
}

// --- PIN: одна особь ------------------------------------------------------
//
// Manual PIN stays exact uEm0200. Director may pin exact uEm0100 / uEm0101
// on grab-alert and hob PackMark. Live 84.26: hob card heads are goblin-family
// (f8 low-bit + fC=4), not wolf flag==1. Unknown heads stay fail-closed.
static bool IsPinnableKind(const char* kind, bool director)
{
    if (!kind) return false;
    if (strcmp(kind, "uEm0200") == 0) return true;
    return director && (IsGoblinFamily(kind) || IsSaurianKind(kind));
}

// Goblin live card heads (84.17, лог 24 — GOBCARD-HEAD строки):
//   fC=4 — восприятие: потолок 300 (линия при контакте, как у волка);
//   fC=5 — боевой режим: att до 484 (наблюдаемый максимум), проверка 0..520
//          по волчьему образцу.
// Флаг +0x08 — (константа_карты) | младший_байт: на части карт старшие биты
// ненулевые (1028.0, 1.0, -1025.0, ...), поэтому гейт по (flag & 1), а не
// flag == 1. Пустая карточка — fC=0; переходная fC=1 — fail-closed, как у
// волка. Константа старших битов НИКОГДА не пишется.
bool LiveGoblinCardMode(uint32_t flag, uint32_t mode,
                        float* pinCeiling, float* maxNative)
{
    if ((flag & 1u) != 1u) return false;
    if (mode == 4) {
        if (pinCeiling) *pinCeiling = kPinValue;
        if (maxNative) *maxNative = 320.0f;
        return true;
    }
    if (mode == 5) {
        if (pinCeiling) *pinCeiling = kGobCombatPinValue;
        if (maxNative) *maxNative = 520.0f;
        return true;
    }
    return false;
}

// Director must not tear a free responder off a live fight with someone
// else. Wolf: fC=2 on a non-mark party card = already chewing that member.
// Goblin (84.17, лог 24): fC=5 — тот же боевой режим, флаг по младнему
// биту. Они занимают свою цель, свободные идут на марку. Bой САМОГО марка
// сохраняется (reinforce). Perception-only stays redirectable.
static bool CombatOccupiesOther(const Row& R, const PartyRef* party, int nParty,
                                int mark)
{
    if (mark < 0) return false;
    const bool goblin = IsGoblinFamily(R.kind);
    const bool saurian = IsSaurianKind(R.kind);
    for (int k = 0; k < R.nSlots; ++k) {
        const Slot& S = R.slot[k];
        if (S.kind != SLOT_ROSTER) continue;
        uintptr_t p = 0;
        if (!RdPtr((void*)(R.body + S.off), &p) || !p) continue;
        int actual = MEMBER_NONE;
        for (int q = 0; q < nParty; ++q) {
            if (p != party[q].body) continue;
            actual = party[q].member;
            break;
        }
        if (actual == MEMBER_NONE || actual == mark) continue;
        uint32_t flag = 0, mode = 0;
        if (!Rd((void*)(R.body + S.off + 0x08), &flag, 4)
            || !Rd((void*)(R.body + S.off + 0x0C), &mode, 4))
            continue;
        if (goblin) {
            if ((flag & 1u) && mode == 5) return true;
        } else if (saurian) {
            // Live 84.29: f8 low-bit + fC=2 combat (not goblin fC=5).
            if ((flag & 1u) && mode == 2) return true;
        } else if (flag == 1 && mode == 2) {
            return true;
        }
    }
    return false;
}

// Genuine unsafe-write evidence stays automatic, but repeated instances are
// represented by the bounded summary rather than one line every two seconds.
static void PinAnomaly(uintptr_t body, const char* what)
{
    ++s_pinUnsafeSkips;
    const DWORD now = GetTickCount();
    if (s_pinLastAnomaly && now - s_pinLastAnomaly < 10000) return;
    s_pinLastAnomaly = now;
    logFile << "Aggro: PIN anomaly @" << std::hex << body << std::dec
            << "  " << what << "  (skipped, no write)" << std::endl;
}

static bool DirectorIdentityExactNow()
{
    if (s_directorFocus < 0 || !s_directorExpectedBody) return false;
    uintptr_t resolved = 0;
    return ResolveMemberBody(s_directorFocus, &resolved)
        && resolved == s_directorExpectedBody;
}

// Proven live uEm0200 card heads (AGGRO_RECON §19.3 / §25.3).
// Perception fC=4: +0x10 native 0..300, pin ceiling 300, weight 1.0 (unread).
// Combat     fC=2: +0x10 native 0..~500, pin ceiling 500, weight 0.2 (unread).
// Dead 0/0, transitional fC=1, and any other head stay fail-closed.
// Weight +0x14 is not written: flipping 0.2↔1.0 is an unproven mode change.
static bool LiveWolfCardMode(uint32_t flag, uint32_t mode,
                             float* pinCeiling, float* maxNative)
{
    if (flag != 1) return false;
    if (mode == 4) {
        if (pinCeiling) *pinCeiling = kPinValue;
        if (maxNative) *maxNative = 320.0f;
        return true;
    }
    if (mode == 2) {
        if (pinCeiling) *pinCeiling = kCombatPinValue;
        if (maxNative) *maxNative = 520.0f;
        return true;
    }
    return false;
}

// Log 23: mark cards on uEm0100 were exact empty shells (0/0/0/0) with a
// stuck party pointer. Wake only those shells at the proven roster offsets
// to a native perception-at-contact head. Not a pack: no suppress/fakehit.
static int TryGoblinEmptyCardWake(Row& R, Slot& S, const char* who)
{
    if (!IsGoblinFamily(R.kind)) return -1;
    if (!IsEm0100RosterOff(S.off)) return -1;
    const uintptr_t card = R.body + S.off;
    uint32_t flag = 0, mode = 0;
    float att = 0.0f, weight = 0.0f;
    if (!Rd((void*)(card + 0x08), &flag, 4)
        || !Rd((void*)(card + 0x0C), &mode, 4)
        || !Rd((void*)(card + 0x10), &att, 4)
        || !Rd((void*)(card + 0x14), &weight, 4))
        return -1;
    if (flag != 0 || mode != 0 || att != 0.0f || weight != 0.0f)
        return -1;

    const uint32_t wantFlag = 1, wantMode = 4;
    const float wantAtt = kPinValue;
    const float wantW = 1.0f;
    if (!WrSafe((void*)(card + 0x08), &wantFlag, 4)
        || !WrSafe((void*)(card + 0x0C), &wantMode, 4)
        || !WrSafe((void*)(card + 0x10), &wantAtt, 4)
        || !WrSafe((void*)(card + 0x14), &wantW, 4)) {
        WrSafe((void*)(card + 0x08), &flag, 4);
        WrSafe((void*)(card + 0x0C), &mode, 4);
        WrSafe((void*)(card + 0x10), &att, 4);
        WrSafe((void*)(card + 0x14), &weight, 4);
        PinAnomaly(R.body, "goblin-card-wake: WrSafe failed");
        return 1;
    }
    uint32_t backFlag = 0, backMode = 0;
    float backAtt = 0.0f, backW = 0.0f;
    if (!Rd((void*)(card + 0x08), &backFlag, 4) || backFlag != wantFlag
        || !Rd((void*)(card + 0x0C), &backMode, 4) || backMode != wantMode
        || !Rd((void*)(card + 0x10), &backAtt, 4) || backAtt != wantAtt
        || !Rd((void*)(card + 0x14), &backW, 4) || backW != wantW) {
        WrSafe((void*)(card + 0x08), &flag, 4);
        WrSafe((void*)(card + 0x0C), &mode, 4);
        WrSafe((void*)(card + 0x10), &att, 4);
        WrSafe((void*)(card + 0x14), &weight, 4);
        ++s_pinRollbacks;
        logFile << "Aggro: PIN ROLLBACK goblin-card-wake @"
                << std::hex << R.body << std::dec
                << " card +0x" << std::hex << S.off << std::dec
                << std::endl;
        return 2;
    }
    ++s_pinWrites;
    ++s_directorWrites;
    logFile << "Aggro: DIRECTOR goblin-card-wake @"
            << std::hex << R.body << std::dec
            << " card +0x" << std::hex << S.off << std::dec
            << "  " << (who ? who : "?")
            << "  0/0 -> 1/4 att=300 w=1.0" << std::endl;
    return 0;
}

// Live uEm0400 (saurian log 84.29): f8=0xBF800001, fC=4 perception / fC=2 combat.
// Block B +274 is clean 0/1 — wolf fakehit, not goblin high-bit constant.
static const uint32_t kSaurianLiveFlag = 0xBF800001u;

static bool LiveSaurianCardMode(uint32_t flag, uint32_t mode,
                                float* pinCeiling, float* maxNative)
{
    if ((flag & 1u) != 1u) return false;
    if (mode == 4) {
        if (pinCeiling) *pinCeiling = kPinValue;
        if (maxNative) *maxNative = 320.0f;
        return true;
    }
    if (mode == 2) {
        if (pinCeiling) *pinCeiling = kCombatPinValue;
        if (maxNative) *maxNative = 520.0f;
        return true;
    }
    return false;
}

static int TrySaurianEmptyCardWake(Row& R, Slot& S, const char* who)
{
    if (!IsSaurianKind(R.kind)) return -1;
    if (!IsEm0100RosterOff(S.off)) return -1;
    const uintptr_t card = R.body + S.off;
    uint32_t flag = 0, mode = 0;
    float att = 0.0f, weight = 0.0f;
    if (!Rd((void*)(card + 0x08), &flag, 4)
        || !Rd((void*)(card + 0x0C), &mode, 4)
        || !Rd((void*)(card + 0x10), &att, 4)
        || !Rd((void*)(card + 0x14), &weight, 4))
        return -1;
    if (flag != 0 || mode != 0 || att != 0.0f || weight != 0.0f)
        return -1;

    const uint32_t wantFlag = kSaurianLiveFlag, wantMode = 4;
    const float wantAtt = kPinValue;
    const float wantW = 1.0f;
    if (!WrSafe((void*)(card + 0x08), &wantFlag, 4)
        || !WrSafe((void*)(card + 0x0C), &wantMode, 4)
        || !WrSafe((void*)(card + 0x10), &wantAtt, 4)
        || !WrSafe((void*)(card + 0x14), &wantW, 4)) {
        WrSafe((void*)(card + 0x08), &flag, 4);
        WrSafe((void*)(card + 0x0C), &mode, 4);
        WrSafe((void*)(card + 0x10), &att, 4);
        WrSafe((void*)(card + 0x14), &weight, 4);
        PinAnomaly(R.body, "saurian-card-wake: WrSafe failed");
        return 1;
    }
    uint32_t backFlag = 0, backMode = 0;
    float backAtt = 0.0f, backW = 0.0f;
    if (!Rd((void*)(card + 0x08), &backFlag, 4) || backFlag != wantFlag
        || !Rd((void*)(card + 0x0C), &backMode, 4) || backMode != wantMode
        || !Rd((void*)(card + 0x10), &backAtt, 4) || backAtt != wantAtt
        || !Rd((void*)(card + 0x14), &backW, 4) || backW != wantW) {
        WrSafe((void*)(card + 0x08), &flag, 4);
        WrSafe((void*)(card + 0x0C), &mode, 4);
        WrSafe((void*)(card + 0x10), &att, 4);
        WrSafe((void*)(card + 0x14), &weight, 4);
        ++s_pinRollbacks;
        logFile << "Aggro: PIN ROLLBACK saurian-card-wake @"
                << std::hex << R.body << std::dec
                << " card +0x" << std::hex << S.off << std::dec
                << std::endl;
        return 2;
    }
    ++s_pinWrites;
    ++s_directorWrites;
    logFile << "Aggro: DIRECTOR saurian-card-wake @"
            << std::hex << R.body << std::dec
            << " card +0x" << std::hex << S.off << std::dec
            << "  " << (who ? who : "?")
            << "  0/0 -> BF800001/4 att=300 w=1.0" << std::endl;
    return 0;
}

// --- GOBCARD / CARDRECON (84.16, универсализирован в 84.18) -------------
//
// Универсальный карточный рекон для ЛЮБОГО вида врага. Метод тот же,
// которым найдена волчья карта (AGGRO_RECON §15.2): DiscoverSlots (все
// указатели на членов партии в теле) + ClassifySlots (арифметическая
// прогрессия слотов = массив карточек: база/шаг — РЕЗУЛЬТАТ замера, а не
// допущение). Потом — временной дифф карт. Новый монстр = один бой +
// строки лога, без видового кода.
//
// Трекинг: до 4 тел, одно на вид (дедуп по kind). Пустые карточки
// невидимы для DiscoverSlots — пока ростер не найден, перескан раз в 5 с.
//
// Строки:
//   GOBCARD track    — тело + вид взяты на трекинг;
//   GOBCARD roster   — найденный ростер base/stride/число; refmatch, если
//                      раскладка совпала с волчьим референсом (2FA0/28C) —
//                      однострочное доказательство «вид лежит как волк»;
//   GOBCARD-HEAD     — голова карточки при смене состояния;
//   GOBCARD-DIFF     — изменившиеся поля (временной дифф);
//   GOBCARD-STILL    — heartbeat;
//   GOBCARD-DUMP     — полный дамп (MARK / snapshot to log).
//
// ЗАПИСЕЙ НЕТ: только Rd. Работает и при выключенном Director.

static const int      kReconTrackMax   = 4;
static const uint32_t kReconCardCap    = 0x400;   // потолок чтения карты
static const int      kReconCardDwords = (int)(kReconCardCap / 4);
static const DWORD    kReconSnapMs     = 300;     // каденс снимка на тело

static const DWORD    kReconHeadThrottleMs = 1000;
static const DWORD    kReconRediscoverMs   = 5000; // перескан ростера
// Волчий референс (замер 76.1, AGGRO_RECON §15.2) — для строки refmatch.
static const uint32_t kReconRefBase   = 0x2FA0;
static const uint32_t kReconRefStride = 0x28C;

struct CardReconTrack {
    uintptr_t body;
    char      kind[16];
    uint32_t  base, stride;
    int       cards;
    bool      haveRoster;
    DWORD     lastTryMs;
    DWORD     lastSnapMs;
    DWORD     lastStillMs;
    DWORD     lastHeadMs;
    uint32_t  lastHead[kMaxParty];   // f8 | (fC << 16) последней строки
    bool      haveHead[kMaxParty];
    uint32_t  prev[kMaxParty][kReconCardDwords];
    bool      havePrev[kMaxParty];
};
static CardReconTrack s_reconTrack[kReconTrackMax];
static int   s_nReconTrack = 0;
static DWORD s_reconLastTick = 0;

// Имя владельца карточки (указатель в голове): лучший усиливающийся
// вариант, только чтение. Нерезолвлённый — "-".
static void CardReconName(uintptr_t ptr, char* out, int cap)
{
    out[0] = 0;
    if (!ptr) return;
    const uintptr_t ar = ArisenBody();
    if (ptr == ar) { lstrcpynA(out, "Arisen", cap); return; }
    for (int r = 0; r < 3; ++r) {
        uintptr_t b = 0;
        if (!PartyRecordInfo(r, 0, 0, &b) || b != ptr) continue;
        lstrcpynA(out, r == 0 ? "MainPawn" : (r == 1 ? "Hired1" : "Hired2"), cap);
        return;
    }
}

static void CardReconForgetMissing()
{
    const int n = EnemyCount();
    for (int i = 0; i < s_nReconTrack; ) {
        bool alive = false;
        for (int k = 0; k < n; ++k)
            if (EnemyBodyAt(k, 0) == s_reconTrack[i].body) { alive = true; break; }
        if (alive) { ++i; continue; }
        s_reconTrack[i] = s_reconTrack[s_nReconTrack - 1];
        --s_nReconTrack;
    }
}

static void CardReconSelect()
{
    // Первое тело каждого вида (дедуп по kind) в порядке WorldScan.
    const int n = EnemyCount();
    for (int k = 0; k < n && s_nReconTrack < kReconTrackMax; ++k) {
        const char* kind = 0;
        const uintptr_t body = EnemyBodyAt(k, &kind);
        if (!body || !kind || !kind[0]) continue;
        bool tracked = false, kindTaken = false;
        for (int i = 0; i < s_nReconTrack; ++i) {
            if (s_reconTrack[i].body == body) tracked = true;
            if (!strcmp(s_reconTrack[i].kind, kind)) kindTaken = true;
        }
        if (tracked || kindTaken) continue;
        CardReconTrack& T = s_reconTrack[s_nReconTrack++];
        memset(&T, 0, sizeof(T));
        T.body = body;
        lstrcpynA(T.kind, kind, sizeof(T.kind));
        if (s_logEvents || s_cardWatch) {
            logFile << "Aggro: GOBCARD track @" << std::hex << body << std::dec
                    << " " << kind << " (read-only card recon)" << std::endl;
        }
    }
}

// Поиск ростера — точно те функции, которыми найден волчий массив:
// DiscoverSlots + ClassifySlots по временному Row.
static bool CardReconDiscoverRoster(CardReconTrack& T)
{
    PartyRef party[kMaxParty];
    const int nParty = CollectParty(party, kMaxParty);
    if (nParty <= 0) return false;
    Slot fresh[kMaxSlots];
    const int nf = DiscoverSlots(T.body, party, nParty, fresh, kMaxSlots);
    if (nf < 3) return false;
    Row tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.body = T.body;
    tmp.nSlots = nf;
    memcpy(tmp.slot, fresh, (size_t)nf * sizeof(Slot));
    ClassifySlots(tmp);
    if (tmp.rosterCount < 3 || !tmp.rosterStride) return false;

    T.base = tmp.rosterBase;
    T.stride = tmp.rosterStride;
    T.cards = tmp.rosterCount > kMaxParty ? kMaxParty : tmp.rosterCount;
    T.haveRoster = true;
    for (int c = 0; c < kMaxParty; ++c) T.havePrev[c] = false;
    if (s_logEvents || s_cardWatch) {
        logFile << "Aggro: GOBCARD roster @" << std::hex << T.body << std::dec
                << " " << T.kind << " base=+0x" << std::hex << T.base
                << " stride=+0x" << T.stride << " x" << std::dec << T.cards;
        if (T.base == kReconRefBase && T.stride == kReconRefStride)
            logFile << "  refmatch (layout == uEm0200 reference)";
        logFile << std::endl;
    }
    return true;
}

static void CardReconDiscoverTick(CardReconTrack& T, DWORD now)
{
    if (T.haveRoster) return;
    if (now - T.lastTryMs < kReconRediscoverMs) return;
    T.lastTryMs = now;
    CardReconDiscoverRoster(T);
}

// Одна карточка: прочитать (stride, потолок kReconCardCap), сравнить со
// предыдущим снимком, напечатать ТОЛЬКО изменившиеся поля.
static bool CardReconDiffCard(CardReconTrack& T, int c, DWORD now)
{
    const uint32_t bytes = T.stride < kReconCardCap ? T.stride : kReconCardCap;
    const int nD = (int)(bytes / 4);
    if (nD < 6) return false;   // голова — 24 B, минимум 6 dwords
    const uintptr_t card = T.body + T.base + (uintptr_t)c * T.stride;
    uint32_t data[kReconCardDwords];
    if (!Rd((void*)card, data, bytes)) return false;

    // HEAD: при смене состояния (f8/fC) — строка формы, троттл на тело.
    const uint32_t head = data[2] | (data[3] << 16);
    if ((!T.haveHead[c] || head != T.lastHead[c])
        && now - T.lastHeadMs >= kReconHeadThrottleMs) {
        T.lastHeadMs = now;
        if (s_logEvents || s_cardWatch) {
            float att = 0.0f, w = 0.0f;
            memcpy(&att, &data[4], 4);
            memcpy(&w, &data[5], 4);
            char who[16] = {};
            CardReconName(data[0], who, sizeof(who));
            logFile << "Aggro: GOBCARD-HEAD @" << std::hex << T.body << std::dec
                    << " " << T.kind << " c" << c << " " << (who[0] ? who : "-")
                    << " f8=" << data[2] << " fC=" << data[3]
                    << " att=" << att << " w=" << w << std::endl;
        }
    }
    T.lastHead[c] = head;
    T.haveHead[c] = true;

    // Дельта: первое чтение — тихая базовая линия.
    if (!T.havePrev[c]) {
        memcpy(T.prev[c], data, bytes);
        T.havePrev[c] = true;
        return false;
    }

    // Тихий дифф (84.19): монотонное затухание внимания (+0x10, -9..-24/тик)
    // — не событие, в лог не идёт. Печатаем: состояние карты
    // (+0x08/+0x0C/+0x14), блоки урона (+0x260..+0x284) — всегда;
    // внимание (+0x10) — только скачок |д| >= 30 (ре-восприятие, сброс,
    // пин); прочее — любое изменение (редко).
    char cells[2048] = {};
    int pos = 0;
    int changed = 0;
    int printed = 0;
    for (int d = 0; d < nD; ++d) {
        if (data[d] == T.prev[c][d]) continue;
        const int off = d * 4;
        const int a = (int)T.prev[c][d];
        const int b = (int)data[d];
        float fa = 0.0f, fb = 0.0f;
        memcpy(&fa, &a, 4);
        memcpy(&fb, &b, 4);
        bool significant = true;
        if (off == 0x10 && fabsf(fb - fa) < 30.0f)
            significant = false;   // обычное затухание/докачка линией
        if (!significant) continue;
        ++changed;
        if (printed >= 24) continue;
        const bool floatish =
            (fa > -100000.0f && fa < 100000.0f
                && (fa == 0.0f || fa > 0.0001f || fa < -0.0001f))
         && (fb > -100000.0f && fb < 100000.0f
                && (fb == 0.0f || fb > 0.0001f || fb < -0.0001f));
        char cell[80] = {};
        if (floatish)
            sprintf_s(cell, sizeof(cell), " +%03X %.3f->%.3f", off, fa, fb);
        else
            sprintf_s(cell, sizeof(cell), " +%03X %d->%d", off, a, b);
        const int len = (int)strlen(cell);
        if (pos + len < (int)sizeof(cells)) {
            memcpy(cells + pos, cell, (size_t)len);
            pos += len;
        }
        ++printed;
    }
    if (!changed) return false;
    if (s_logEvents || s_cardWatch) {
        logFile << "Aggro: GOBCARD-DIFF @" << std::hex << T.body << std::dec
                << " " << T.kind << " c" << c << cells;
        if (printed < changed)
            logFile << "(+" << (changed - printed) << " more)";
        logFile << std::endl;
    }
    memcpy(T.prev[c], data, bytes);
    return true;
}

static void CardReconDumpOne(CardReconTrack& T)
{
    if (!T.haveRoster) {
        logFile << "Aggro: GOBCARD-DUMP @" << std::hex << T.body << std::dec
                << " " << T.kind << " roster=scanning" << std::endl;
        return;
    }
    const uint32_t bytes = T.stride < kReconCardCap ? T.stride : kReconCardCap;
    const int nD = (int)(bytes / 4);
    for (int c = 0; c < T.cards && c < kMaxParty; ++c) {
        const uintptr_t card = T.body + T.base + (uintptr_t)c * T.stride;
        uint32_t data[kReconCardDwords];
        if (!Rd((void*)card, data, bytes)) continue;
        char who[16] = {};
        CardReconName(data[0], who, sizeof(who));
        logFile << "Aggro: GOBCARD-DUMP @" << std::hex << T.body << std::dec
                << " " << T.kind << " c" << c << " " << (who[0] ? who : "-");
        for (int d = 0; d < nD; ++d) {
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

void CardReconTick()
{
    if (!InWorld()) return;
    const DWORD now = GetTickCount();
    if (s_reconLastTick && now - s_reconLastTick < 150) return;
    s_reconLastTick = now;

    CardReconForgetMissing();
    CardReconSelect();
    for (int i = 0; i < s_nReconTrack; ++i) {
        CardReconTrack& T = s_reconTrack[i];
        CardReconDiscoverTick(T, now);
        if (!T.haveRoster) continue;
        if (now - T.lastSnapMs < kReconSnapMs) continue;
        T.lastSnapMs = now;
        bool any = false;
        for (int c = 0; c < T.cards && c < kMaxParty; ++c)
            if (CardReconDiffCard(T, c, now)) any = true;
        (void)any;
    }
}

void CardReconDump()
{
    for (int i = 0; i < s_nReconTrack; ++i)
        CardReconDumpOne(s_reconTrack[i]);
    if (!s_nReconTrack)
        logFile << "Aggro: GOBCARD: no enemies in world" << std::endl;
}

// Одна карта: форма -> диапазон -> запись -> readback -> откат.
// want = kPinValue/kCombatPinValue (штырь) или kSuppValue (0, подавление).
// who — фактический владелец карты (по пере-читанному указателю).
static int PinWriteCard(Row& R, Slot& S, const char* who, float want, DWORD now, bool director)
{
    // Tick-level validation is not a reusable permission: resolve the fixed
    // record slot immediately before every Director card mutation.
    if (director && !DirectorIdentityExactNow()) {
        DirectorFocusSet(MEMBER_NONE, 0);
        return -3;
    }
    const uintptr_t card = R.body + S.off;

    uint32_t flag = 0, c4 = 0;
    if (!Rd((void*)(card + 0x08), &flag, 4)
        || !Rd((void*)(card + 0x0C), &c4, 4)) {
        PinAnomaly(R.body, "shape: unreadable head");
        return 1;
    }
    // Гейт формы специфичен для вида: волк — flag==1 && fC∈{4,2};
    // гоблин (84.17) — (flag&1)==1 && fC∈{4,5}, константа старших битов
    // не трогается. Пустая оболочка гоблина 0/0/0/0 — только wake.
    const bool goblin = IsGoblinFamily(R.kind);
    const bool saurian = IsSaurianKind(R.kind);
    float pinCeil = 0.0f, maxNative = 0.0f;
    const bool live = goblin
        ? LiveGoblinCardMode(flag, c4, &pinCeil, &maxNative)
        : (saurian
            ? LiveSaurianCardMode(flag, c4, &pinCeil, &maxNative)
            : LiveWolfCardMode(flag, c4, &pinCeil, &maxNative));
    if (!live) {
        if (director && want != kSuppValue) {
            const int woke = goblin ? TryGoblinEmptyCardWake(R, S, who)
                                    : TrySaurianEmptyCardWake(R, S, who);
            if (woke == 0) return 0;
            if (woke > 0) return woke;
        }
        float f10 = 0.0f, f14 = 0.0f;
        Rd((void*)(card + 0x10), &f10, 4);
        Rd((void*)(card + 0x14), &f14, 4);
        char buf[96] = {};
        if (goblin)
            sprintf_s(buf, "shape: f8=%u fC=%u 10=%.1f w=%.2f (not live goblin head)",
                      flag, c4, f10, f14);
        else if (saurian)
            sprintf_s(buf, "shape: f8=%u fC=%u 10=%.1f w=%.2f (not live saurian head)",
                      flag, c4, f10, f14);
        else
            sprintf_s(buf, "shape: f8=%u fC=%u 10=%.1f w=%.2f (not live 1/4 or 1/2)",
                      flag, c4, f10, f14);
        PinAnomaly(R.body, buf);
        return 1;
    }
    const float writeWant = (want == kSuppValue) ? kSuppValue : pinCeil;

    uintptr_t att = card + 0x10;
    float cur = 0.0f;
    if (!Rd((void*)att, &cur, 4)) return 1;
    // Native range is mode-specific. A combat card at 484 is legal; writing
    // the perception ceiling 300 into it would LOWER attention, so pin uses
    // the mode's own ceiling.
    if (cur < -0.001f || cur > maxNative) {
        PinAnomaly(R.body, "attention value out of native range");
        return 1;
    }

    // Запись -> readback -> при рассинхроне откат к прочитанному.
    if (!WrSafe((void*)att, &writeWant, 4)) {
        PinAnomaly(R.body, "WrSafe failed");
        return 1;
    }
    float back = 0.0f;
    if (!Rd((void*)att, &back, 4) || back != writeWant) {
        WrSafe((void*)att, &cur, 4);
        ++s_pinRollbacks;
        // Откат — редкое важное событие: логируем сразу, без гетеризации.
        logFile << "Aggro: PIN ROLLBACK @" << std::hex << R.body << std::dec
                << " card +0x" << std::hex << S.off << std::dec
                << "  readback " << back << " != " << writeWant
                << "  restored " << cur << std::endl;
        return 2;
    }
    if (writeWant == kSuppValue) ++s_pinSuppWrites; else ++s_pinWrites;
    if (director) ++s_directorWrites;

    // Successful per-card readbacks are research telemetry, not product
    // evidence. Quiet Director operation keeps transitions and summaries;
    // [aggro] logEvents=on explicitly restores this detail.
    if (!s_logEvents) return 0;

    // Дельта-лог: одна строка на карточку, и только когда значение
    // ушло дальше полутора единиц от прошлого записанного.
    PinLog* pl = 0;
    for (int q = 0; q < s_nPinLog; ++q) {
        if (s_pinLog[q].body != R.body || s_pinLog[q].cardOff != S.off)
            continue;
        pl = &s_pinLog[q];
        break;
    }
    bool logNow = false;
    if (!pl) {
        if (s_nPinLog < 128) {
            pl = &s_pinLog[s_nPinLog++];
            pl->body = R.body;
            pl->cardOff = S.off;
            pl->lastMs = 0;
            pl->lastWas = -1e6f;
        }
        logNow = true;
    } else if (now - pl->lastMs >= 2000
               && fabsf(cur - pl->lastWas) > 1.5f) {
        logNow = true;
    }
    if (logNow && pl) {
        pl->lastMs = now;
        pl->lastWas = cur;
        logFile << "Aggro: PIN @" << std::hex << R.body << std::dec
                << " card +0x" << std::hex << S.off << std::dec
                << "  " << (who ? who : "?")
                << "  att " << cur << " -> " << writeWant
                << (writeWant == kSuppValue ? "  supp ok" : "  readback ok")
                << std::endl;
    }
    return 0;
}

// 82.0: фейк-хит на одной карте — пере-заявка «свежего урона» в блоке B.
// 274=1 (флаг, int, стабилен) и 27c=value (float, затухает 9/тик —
// поэтому пере-заявляем каждый тик). Блок B восприниманием НЕ сбрасывается
// (§23), поэтому в мурле это единственный член карточки, который может
// прилипнуть. Тот же контракт: нативный диапазон -> write -> readback ->
// откат. Дельта-лог в отдельном «пространстве» PinLog (off | 0x1000000),
// чтобы не мешать штырю на той же карточке.
static void PinFakehitCard(Row& R, Slot& S, const char* who, DWORD now, bool director)
{
    if (director && !DirectorIdentityExactNow()) {
        DirectorFocusSet(MEMBER_NONE, 0);
        return;
    }
    const uintptr_t card = R.body + S.off;

    // Флаг 274: живое целое 0/1.
    uint32_t flagCur = 0;
    if (!Rd((void*)(card + 0x274), &flagCur, 4)
        || (flagCur != 0 && flagCur != 1)) {
        PinAnomaly(R.body, "fakehit: 274 flag is not 0/1");
        return;
    }
    // Значение 27c: нативный диапазон 0..~500 (замеры до 499).
    float valCur = 0.0f;
    if (!Rd((void*)(card + 0x27C), &valCur, 4)
        || valCur < -0.001f || valCur > 600.0f) {
        PinAnomaly(R.body, "fakehit: 27c out of native range");
        return;
    }

    const uint32_t wantFlag = 1;
    const float    wantVal  = s_pinFakehitValue;
    bool wrote = false;

    if (flagCur != wantFlag) {
        if (!WrSafe((void*)(card + 0x274), &wantFlag, 4)) return;
        uint32_t back = 0;
        if (!Rd((void*)(card + 0x274), &back, 4) || back != wantFlag) {
            WrSafe((void*)(card + 0x274), &flagCur, 4);
            ++s_pinRollbacks;
            logFile << "Aggro: PIN ROLLBACK fakehit flag @"
                    << std::hex << R.body << std::dec
                    << "  readback " << back << " != 1  restored "
                    << flagCur << std::endl;
            return;
        }
        wrote = true;
    }

    if (valCur != wantVal) {
        if (!WrSafe((void*)(card + 0x27C), &wantVal, 4)) return;
        float back = 0.0f;
        if (!Rd((void*)(card + 0x27C), &back, 4) || back != wantVal) {
            WrSafe((void*)(card + 0x27C), &valCur, 4);
            ++s_pinRollbacks;
            logFile << "Aggro: PIN ROLLBACK fakehit val @"
                    << std::hex << R.body << std::dec
                    << "  readback " << back << " != " << wantVal
                    << "  restored " << valCur << std::endl;
            return;
        }
        wrote = true;
    }
    if (!wrote) return;
    ++s_pinFakehitWrites;
    if (director) ++s_directorWrites;

    // Detailed successful card telemetry is opt-in research verbosity.
    if (!s_logEvents) return;

    PinLog* pl = 0;
    const uint32_t fhOff = S.off | 0x1000000u;   // отдельное пространство
    for (int q = 0; q < s_nPinLog; ++q) {
        if (s_pinLog[q].body != R.body || s_pinLog[q].cardOff != fhOff)
            continue;
        pl = &s_pinLog[q];
        break;
    }
    bool logNow = false;
    if (!pl) {
        if (s_nPinLog < 128) {
            pl = &s_pinLog[s_nPinLog++];
            pl->body = R.body;
            pl->cardOff = fhOff;
            pl->lastMs = 0;
            pl->lastWas = -1e6f;
        }
        logNow = true;
    } else if (now - pl->lastMs >= 2000
               && fabsf(valCur - pl->lastWas) > 1.5f) {
        logNow = true;
    }
    if (logNow && pl) {
        pl->lastMs = now;
        pl->lastWas = valCur;
        logFile << "Aggro: PIN @" << std::hex << R.body << std::dec
                << " card +0x" << std::hex << S.off << std::dec
                << "  " << (who ? who : "?")
                << "  fakehit 27c " << valCur << " -> " << wantVal
                << "  ok" << std::endl;
    }
}

// 84.19: голос сигнала. Один раз на lease: «директор дал агр через
// фейк-хит». Разряжается DirectorFocusSet, срабатывает при первой
// успешной записи блока B.
static uintptr_t s_fhSignalLease = 0;

// 84.17: фейк-урон гоблину — тот же блок B на тех же оффсетах, что у
// волка (лог 24: +0x274 флаг / +0x278 счётчик / +0x27C значение /
// +0x280 вес; нативный диапазон 0..~500, затухание -24/тик — поэтому
// пере-заявляем каждый тик, пока жив lease).
//
// ОТЛИЧИЕ ОТ ВОЛКА: флаг +0x274 — (константа_карты) | младший_байт.
// Старшие биты — константа карты (1028.0, 1.0, -1025.0, ...), её писать
// нельзя: ставим только младший бит (V -> V+1), если он ещё 0.
// +0x278/+0x280 не трогаем (счётчик и вес — минимальная поверхность).
static void GoblinFakehitCard(Row& R, Slot& S, const char* who, DWORD now,
                              bool director)
{
    if (!IsGoblinFamily(R.kind)) return;
    if (director && !DirectorIdentityExactNow()) {
        DirectorFocusSet(MEMBER_NONE, 0);
        return;
    }
    const uintptr_t card = R.body + S.off;

    // Флаг +274: живое целое; младший байт 0/1, старшие — константа.
    uint32_t flagCur = 0;
    if (!Rd((void*)(card + 0x274), &flagCur, 4)) return;
    // Значение +27C: нативный диапазон 0..~500 (лог 24: до 495.0).
    float valCur = 0.0f;
    if (!Rd((void*)(card + 0x27C), &valCur, 4)
        || valCur < -0.001f || valCur > 600.0f) {
        PinAnomaly(R.body, "goblin-fakehit: 27C out of native range");
        return;
    }

    const float wantVal = s_pinFakehitValue;
    bool wrote = false;

    if ((flagCur & 1u) == 0) {
        const uint32_t wantFlag = flagCur + 1;   // только младший бит
        if (!WrSafe((void*)(card + 0x274), &wantFlag, 4)) return;
        uint32_t back = 0;
        if (!Rd((void*)(card + 0x274), &back, 4) || back != wantFlag) {
            WrSafe((void*)(card + 0x274), &flagCur, 4);
            ++s_pinRollbacks;
            logFile << "Aggro: PIN ROLLBACK goblin-fakehit flag @"
                    << std::hex << R.body << std::dec
                    << "  readback " << back << " != " << wantFlag
                    << "  restored " << flagCur << std::endl;
            return;
        }
        wrote = true;
    }

    if (valCur != wantVal) {
        if (!WrSafe((void*)(card + 0x27C), &wantVal, 4)) return;
        float back = 0.0f;
        if (!Rd((void*)(card + 0x27C), &back, 4) || back != wantVal) {
            WrSafe((void*)(card + 0x27C), &valCur, 4);
            ++s_pinRollbacks;
            logFile << "Aggro: PIN ROLLBACK goblin-fakehit val @"
                    << std::hex << R.body << std::dec
                    << "  readback " << back << " != " << wantVal
                    << "  restored " << valCur << std::endl;
            return;
        }
        wrote = true;
    }
    if (!wrote) return;
    ++s_pinFakehitWrites;
    if (director) {
        ++s_directorWrites;
        // Сигнал — событие, а не поток записей: одна строка на lease.
        if (s_directorExpectedBody != s_fhSignalLease) {
            s_fhSignalLease = s_directorExpectedBody;
            logFile << "Aggro: DIRECTOR fakehit-signal "
                    << MemberName(s_directorFocus)
                    << " via block B (goblin) first write @"
                    << std::hex << R.body << std::dec
                    << " card +0x" << S.off << std::dec << std::endl;
        }
    }

    // Детальная телеметрия по карточкам — opt-in research verbosity.
    if (!s_logEvents) return;

    PinLog* pl = 0;
    const uint32_t fhOff = S.off | 0x2000000u;   // пространство goblin-fakehit
    for (int q = 0; q < s_nPinLog; ++q) {
        if (s_pinLog[q].body != R.body || s_pinLog[q].cardOff != fhOff)
            continue;
        pl = &s_pinLog[q];
        break;
    }
    bool logNow = false;
    if (!pl) {
        if (s_nPinLog < 128) {
            pl = &s_pinLog[s_nPinLog++];
            pl->body = R.body;
            pl->cardOff = fhOff;
            pl->lastMs = 0;
            pl->lastWas = -1e6f;
        }
        logNow = true;
    } else if (now - pl->lastMs >= 2000
               && fabsf(valCur - pl->lastWas) > 1.5f) {
        logNow = true;
    }
    if (logNow && pl) {
        pl->lastMs = now;
        pl->lastWas = valCur;
        logFile << "Aggro: PIN @" << std::hex << R.body << std::dec
                << " card +0x" << std::hex << S.off << std::dec
                << "  " << (who ? who : "?")
                << "  goblin-fakehit 27c " << valCur << " -> " << wantVal
                << "  ok" << std::endl;
    }
}

// 83.0/84.12: диагностика формы. Живые 1/4 и 1/2 уже пишутся. Сюда
// попадают мёртвая оболочка 0/0, переходное fC=1 и чужая голова —
// печатаем ГОЛОВЫ всех карт особи: только чтение, троттл 5 с.
struct ShapeWatch { uintptr_t body; DWORD lastMs; };
static ShapeWatch s_shapeW[8];
static int        s_nShapeW = 0;

static void PinShapeDump(Row& R, const PartyRef* party, int nParty, DWORD now)
{
    // Full card-shape censuses are useful in explicit research sessions but
    // duplicate the automatic bounded anomaly/unsafe-skip evidence otherwise.
    if (!s_logEvents) return;

    ShapeWatch* sw = 0;
    for (int i = 0; i < s_nShapeW; ++i) {
        if (s_shapeW[i].body != R.body) continue;
        sw = &s_shapeW[i];
        break;
    }
    if (sw && now - sw->lastMs < 5000) return;
    if (!sw) {
        if (s_nShapeW >= 8) return;
        sw = &s_shapeW[s_nShapeW++];
        sw->body = R.body;
        sw->lastMs = 0;
    }
    sw->lastMs = now;

    logFile << "Aggro: PIN shape @" << std::hex << R.body << std::dec;
    for (int k = 0; k < R.nSlots; ++k) {
        const Slot& S = R.slot[k];
        if (S.kind != SLOT_ROSTER) continue;
        uintptr_t p = 0;
        RdPtr((void*)(R.body + S.off), &p);
        const char* who = "-";
        for (int q = 0; q < nParty; ++q) {
            if (p != party[q].body) continue;
            who = MemberName(party[q].member);
            break;
        }
        uint32_t f8 = 0, fC = 0;
        float f10 = 0.0f, f14 = 0.0f;
        Rd((void*)(R.body + S.off + 0x08), &f8, 4);
        Rd((void*)(R.body + S.off + 0x0C), &fC, 4);
        Rd((void*)(R.body + S.off + 0x10), &f10, 4);
        Rd((void*)(R.body + S.off + 0x14), &f14, 4);
        logFile << "  " << who << " f8=" << f8 << " fC=" << fC
                << " 10=" << f10 << " w=" << f14;
    }
    logFile << std::endl;
}

// Волк: штырь на заштыренной карте; при включённом подавлении — ноль
// на остальных живых картах той же особи. Классификация по ПЕРЕ-ЧИТАННОМУ
// указателю: ротация кэша между тиками не должна перепутать «чужую»
// и «свою» карту.
static void PinRow(Row& R, const PartyRef* party, int nParty, DWORD now,
                   int targetMember, bool suppress, bool fakehit, bool director)
{
    uintptr_t mBody = 0;
    for (int p = 0; p < nParty; ++p) {
        if (party[p].member != targetMember) continue;
        mBody = party[p].body;
        break;
    }
    if (!mBody) return;
    if (director && CombatOccupiesOther(R, party, nParty, targetMember))
        return;

    for (int k = 0; k < R.nSlots; ++k) {
        Slot& S = R.slot[k];
        if (S.kind != SLOT_ROSTER) continue;

        // Кто реально сидит в этой карте СЕЙЧАС.
        uintptr_t p = 0;
        if (!RdPtr((void*)(R.body + S.off), &p)) continue;
        int actual = MEMBER_NONE;
        for (int q = 0; q < nParty; ++q) {
            if (p != party[q].body) continue;
            actual = party[q].member;
            break;
        }
        if (actual == MEMBER_NONE) continue;   // мёртвая карта — не трогаем
        const char* who = MemberName(actual);

        if (actual == targetMember) {
            // Фейк-хит — только если штырь реально прошёл (форма карты
            // валидна, запись и readback ок): на аномальной карте ни
            // один член формулы не трогаем. Блок B у волка и гоблина на
            // одних оффсетах, но флаг у гоблина — с константой в старших
            // битах, поэтому диспетчер по виду.
            const int rc = PinWriteCard(R, S, who, kPinValue, now, director);
            if (rc == 0) {
                if (fakehit) {
                    if (IsGoblinFamily(R.kind))
                        GoblinFakehitCard(R, S, who, now, director);
                    else
                        PinFakehitCard(R, S, who, now, director);
                }
            } else {
                // Запись не прошла — смотрим, что там с формой (5 с троттл).
                PinShapeDump(R, party, nParty, now);
            }
        } else if (suppress) {
            const int rc = PinWriteCard(R, S, who, kSuppValue, now, director);
            if (rc != 0)
                PinShapeDump(R, party, nParty, now);
        }
    }
}

static void PinSummary(int nWolves, int nLeft, DWORD now, int targetMember, int scope,
                       bool suppress, bool fakehit, bool director)
{
    // First summary is immediateiate; unchanged leases then report at most once
    // per ten seconds. Engagement/release transitions are logged elsewhere.
    if (s_pinLastLog && now - s_pinLastLog < 10000) return;
    s_pinLastLog = now;
    // held: сколько особей ДЕРЖАТ заштыренного в текущей цели прямо сейчас
    // (метрика «прилипло» в лог — глазу больше не приходится гадать).
    int held = 0;
    for (int i = 0; i < s_nRow; ++i) {
        if (!IsPinnableKind(s_row[i].kind, director)) continue;
        if (director && s_directorKind[0]
            && strcmp(s_row[i].kind, s_directorKind) != 0) continue;
        if (director && s_row[i].body == s_directorExcludedBody) continue;
        if (s_row[i].targetMember == targetMember) ++held;
    }
    logFile << "Aggro: " << (director ? "DIRECTOR" : "PIN") << " "
            << MemberName(targetMember)
            << " scope=" << (scope ? "all" : "nearest")
            << (suppress ? " SUPPRESS" : "")
            << (fakehit ? " FAKEHIT" : "");
    if (director)
        logFile << (s_directorResponse == DIRECTOR_RESPONSE_ALERT
                    ? " ALERT" : " ALARM");
    else if (s_pinFocus >= 0)
        logFile << " FOCUS";
    logFile << "  wolves " << nWolves;
    if (director && s_directorExcludedBody)
        logFile << "  excluded 0x" << std::hex << s_directorExcludedBody << std::dec;
    logFile << "  held " << held
            << "  left " << nLeft
            << "  writes " << s_pinWrites
            << "  supp " << s_pinSuppWrites
            << "  fakehit " << s_pinFakehitWrites
            << "  unsafeSkips " << s_pinUnsafeSkips
            << "  rollbacks " << s_pinRollbacks << std::endl;
}

// --- жизненный цикл ---------------------------------------------------------

void Init()
{
    s_enabled = config.getBool("aggro", "watch", false);
    s_cardWatch = config.getBool("aggro", "cardwatch", false);
    s_logEvents = config.getBool("aggro", "logEvents", false);
    s_directorObserver = false;
    s_directorFocus = MEMBER_NONE;
    s_directorExpectedBody = 0;
    s_directorExcludedBody = 0;
    s_directorResponse = DIRECTOR_RESPONSE_NONE;
    lstrcpynA(s_directorKind, "uEm0200", sizeof(s_directorKind));
    s_directorWrites = 0;
    s_directorIdentityBlockLogged = false;
    s_lastIdentityMask = -1;
    // Не const: сигнатура getEnum принимает неконстантный массив
    // (инициализаторы — строковые литералы, указатели в парах const).
    static std::pair<int, LPCSTR> kPinMap[] = {
        { MEMBER_NONE,   "off" },
        { MEMBER_ARISEN, "Arisen" },
        { MEMBER_MAIN,   "MainPawn" },
        { MEMBER_HIRED1, "Hired1" },
        { MEMBER_HIRED2, "Hired2" }
    };
    static std::pair<int, LPCSTR> kScopeMap[] = {
        { 0, "nearest" }, { 1, "all" }
    };
    s_pinMember = config.getEnum("aggro", "pin", MEMBER_NONE,
                                 kPinMap, 5);
    s_pinScope  = config.getEnum("aggro", "pin_scope", 0, kScopeMap, 2);
    s_pinSuppress = config.getBool("aggro", "pin_suppress", false);
    s_pinFakehit = config.getBool("aggro", "pin_fakehit", false);
    s_pinFakehitValue = config.getFloat("aggro", "pin_fakehit_value", 150.0f);
    if (s_pinFakehitValue < 1.0f) s_pinFakehitValue = 1.0f;
    if (s_pinFakehitValue > 500.0f) s_pinFakehitValue = 500.0f;
    s_pinFocus = MEMBER_NONE;
    s_nRow = 0;
    s_nCard = 0;
    s_nPinLog = 0;
    s_pinWrites = 0;
    s_pinSuppWrites = 0;
    s_pinFakehitWrites = 0;
    s_pinRollbacks = 0;
    s_pinUnsafeSkips = 0;
    s_pinLastLog = 0;
    s_pinLastAnomaly = 0;
    s_switchTotal = 0;
    s_nReconTrack = 0;
    s_reconLastTick = 0;
    for (int i = 0; i < kMemberSlots; ++i) s_holdTicks[i] = 0;
    memset(s_conv, 0, sizeof(s_conv));
    memset(s_convLogged, 0, sizeof(s_convLogged));
    for (int i = 0; i < kMemberSlots; ++i) s_conv[i].member = i;
    lstrcpynA(s_status, s_enabled ? "aggro watch: armed" : "aggro watch: off",
              sizeof(s_status));
    logFile << "Aggro: watch " << (s_enabled ? "enabled" : "disabled")
            << "  event-log " << (s_logEvents ? "on" : "off")
            << "  cardwatch " << (s_cardWatch ? "on" : "off")
            << "  pin " << MemberName(s_pinMember)
            << std::endl;
}

void Shutdown()
{
    // One bounded footer keeps the automatic mutation evidence available
    // even when successful per-card research lines were intentionally quiet.
    logFile << "Aggro: shutdown summary directorWrites=" << s_directorWrites
            << " pin=" << s_pinWrites
            << " supp=" << s_pinSuppWrites
            << " fakehit=" << s_pinFakehitWrites
            << " unsafeSkips=" << s_pinUnsafeSkips
            << " rollbacks=" << s_pinRollbacks
            << " targetSwitches=" << s_switchTotal << std::endl;

    // Штырь снимается молча: процесс завершается, а поле и так затухнет.
    s_pinMember = MEMBER_NONE;
    s_directorFocus = MEMBER_NONE;
    s_directorExpectedBody = 0;
    s_directorExcludedBody = 0;
    s_directorResponse = DIRECTOR_RESPONSE_NONE;
    s_nRow = 0;
    s_nReconTrack = 0;
}

bool Enabled() { return s_enabled; }

void SetObserverDemand(bool on)
{
    if (s_directorObserver == on) return;
    s_directorObserver = on;
    if (!on) DirectorFocusSet(MEMBER_NONE, 0);
    if (!s_enabled) {
        s_nRow = 0;
        s_nCard = 0;
        s_switchTotal = 0;
        for (int i = 0; i < kMemberSlots; ++i) s_holdTicks[i] = 0;
        memset(s_conv, 0, sizeof(s_conv));
        memset(s_convLogged, 0, sizeof(s_convLogged));
        for (int i = 0; i < kMemberSlots; ++i) s_conv[i].member = i;
        lstrcpynA(s_status, on ? "aggro observer: Director read-only"
                              : "aggro watch: off", sizeof(s_status));
    }
}

bool ObserverDemanded() { return s_directorObserver; }

bool DirectorFocusSet(int member, uintptr_t expectedBody,
                      uintptr_t excludedEnemyBody, int response,
                      const char* exactKind)
{
    if (member < 0) {
        if (s_directorFocus >= 0)
            logFile << "Aggro: DIRECTOR "
                    << (s_directorResponse == DIRECTOR_RESPONSE_ALERT
                        ? "ALERT" : "ALARM")
                    << " released: " << MemberName(s_directorFocus)
                    << "  (native decay takes over)" << std::endl;
        s_directorFocus = MEMBER_NONE;
        s_directorExpectedBody = 0;
        s_directorExcludedBody = 0;
        s_directorResponse = DIRECTOR_RESPONSE_NONE;
        lstrcpynA(s_directorKind, "uEm0200", sizeof(s_directorKind));
        s_directorIdentityBlockLogged = false;
        return true;
    }
    if (member > MEMBER_HIRED2 || !expectedBody
        || (response != DIRECTOR_RESPONSE_ALERT
            && response != DIRECTOR_RESPONSE_ALARM)
        || !IsDirectorKind(exactKind)) {
        if (exactKind && exactKind[0] && !IsDirectorKind(exactKind)
            && !s_directorIdentityBlockLogged) {
            logFile << "Aggro: DIRECTOR response blocked: kind=" << exactKind
                    << " not in {uEm0200,uEm0100,uEm0101,uEm0400} no-write"
                    << std::endl;
            s_directorIdentityBlockLogged = true;
        }
        return false;
    }

    uintptr_t resolved = 0;
    if (!ResolveMemberBody(member, &resolved) || resolved != expectedBody) {
        if (!s_directorIdentityBlockLogged) {
            logFile << "Aggro: DIRECTOR response blocked: slot="
                    << MemberName(member)
                    << " expected=0x" << std::hex << expectedBody
                    << " resolved=0x" << resolved << std::dec
                    << " identity=AMBIGUOUS no-write" << std::endl;
            s_directorIdentityBlockLogged = true;
        }
        s_directorFocus = MEMBER_NONE;
        s_directorExpectedBody = 0;
        s_directorExcludedBody = 0;
        s_directorResponse = DIRECTOR_RESPONSE_NONE;
        lstrcpynA(s_directorKind, "uEm0200", sizeof(s_directorKind));
        return false;
    }

    if (s_directorFocus != member || s_directorExpectedBody != expectedBody
        || s_directorExcludedBody != excludedEnemyBody
        || s_directorResponse != response
        || strcmp(s_directorKind, exactKind) != 0) {
        // Product response owns the actuator exclusively. Do not let an old
        // research PIN silently resume when the Director later releases.
        s_pinMember = MEMBER_NONE;
        s_pinFocus = MEMBER_NONE;
        s_pinSuppress = false;
        s_pinFakehit = false;
        s_nPinLog = 0;
        s_pinLastLog = 0;
        logFile << "Aggro: DIRECTOR "
                << (response == DIRECTOR_RESPONSE_ALERT ? "ALERT " : "ALARM ")
                << MemberName(member)
                << " expected=0x" << std::hex << expectedBody
                << " excludedEnemy=0x" << excludedEnemyBody << std::dec
                << " identity=EXACT kind=" << exactKind
                << " scope=free-kind leave-engaged "
                << (response == DIRECTOR_RESPONSE_ALERT
                    ? "pin-only" : "pin+suppress+fakehit")
                << std::endl;
    }
    s_directorFocus = member;
    s_directorExpectedBody = expectedBody;
    s_directorExcludedBody = excludedEnemyBody;
    s_directorResponse = response;
    s_fhSignalLease = 0;   // 84.19: новая аренда — сигнал ещё не дан
    lstrcpynA(s_directorKind, exactKind, sizeof(s_directorKind));
    s_directorIdentityBlockLogged = false;
    return true;
}

int DirectorFocusMember() { return s_directorFocus; }
int DirectorResponseLevel() { return s_directorResponse; }
uint32_t DirectorWriteCount() { return s_directorWrites; }

void SetEnabled(bool on)
{
    if (s_enabled == on) return;
    s_enabled = on;
    s_nRow = 0;
    s_nCard = 0;
    s_switchTotal = 0;
    for (int i = 0; i < kMemberSlots; ++i) s_holdTicks[i] = 0;
    // Штырь живёт на строках обзора: прибор снят — штырь не на чем
    // держаться. Снимаем, а не молчим: молчание = записываем, а сами
    // об этом не знаем. FOCUS заодно — он над штырём.
    if (!on && s_pinMember >= 0) {
        logFile << "Aggro: PIN released with watch OFF: "
                << MemberName(s_pinMember)
                << "  (engine decay takes over)" << std::endl;
        s_pinMember = MEMBER_NONE;
        s_nPinLog = 0;
    }
    s_pinFocus = MEMBER_NONE;
    logFile << "Aggro: watch " << (on ? "ON" : "OFF") << " (counters reset)"
            << std::endl;
    lstrcpynA(s_status, on ? "aggro watch: armed"
                            : (s_directorObserver
                                ? "aggro observer: Director read-only"
                                : "aggro watch: off"),
              sizeof(s_status));
}

void PinSet(int member, int scope)
{
    if (s_directorFocus >= 0) return;  // product lease owns the actuator

    if (scope < 0) scope = 0;
    if (scope > 1) scope = 1;
    if (member >= kMemberSlots) member = MEMBER_NONE;
    if (member == s_pinMember && scope == s_pinScope) return;
    if (s_pinMember >= 0)
        logFile << "Aggro: PIN released: " << MemberName(s_pinMember)
                << "  (engine decay takes over, field back to zero in ~5 s)"
                << std::endl;
    s_pinMember = member;
    s_pinScope = scope;
    s_nPinLog = 0;
    s_pinLastLog = 0;
    if (member >= 0)
        logFile << "Aggro: PIN armed: " << MemberName(member)
                << "  scope=" << (scope ? "all" : "nearest")
                << "  (uEm0200 cards only, re-asserts att=300, readback ok)"
                << std::endl;
    FocusRecheck("pin");
}

int PinMember() { return s_pinMember; }
int PinScope()  { return s_pinScope; }

void PinStats(uint32_t* writesOut, uint32_t* rollbacksOut)
{
    if (writesOut) *writesOut = s_pinWrites;
    if (rollbacksOut) *rollbacksOut = s_pinRollbacks;
}

void PinSuppressSet(bool on)
{
    if (s_directorFocus >= 0) return;  // product lease owns the actuator

    if (s_pinSuppress == on) return;
    s_pinSuppress = on;
    logFile << "Aggro: PIN suppress " << (on ? "ON" : "OFF")
            << (on ? " (other live cards -> 0, argmax gets clean)" : "")
            << std::endl;
    FocusRecheck("suppress");
}

bool PinSuppressOn() { return s_pinSuppress; }

void PinFakehitSet(bool on)
{
    if (s_directorFocus >= 0) return;  // product lease owns the actuator

    if (s_pinFakehit == on) return;
    s_pinFakehit = on;
    logFile << "Aggro: PIN fakehit " << (on ? "ON" : "OFF")
            << (on ? (" (re-asserts block B: 274=1, 27c="
                      + std::to_string((int)s_pinFakehitValue)
                      + " on the pinned card)").c_str() : "")
            << std::endl;
    FocusRecheck("fakehit");
}

bool PinFakehitOn() { return s_pinFakehit; }

void PinFocusSet(int member)
{
    if (s_directorFocus >= 0) return;  // product lease owns the actuator

    if (member >= kMemberSlots) member = MEMBER_NONE;
    if (member == s_pinFocus) return;
    if (member >= 0) {
        // Сначала объявляем, потом собираем связку: setters внутри
        // вызовут FocusRecheck, а фокус ещё не выставлен — ложных
        // disengage не будет.
        logFile << "Aggro: FOCUS " << MemberName(member)
                << "  (pin+suppress+fakehit, scope="
                << (s_pinScope ? "all" : "nearest") << ")" << std::endl;
        PinSet(member, s_pinScope);
        PinSuppressSet(true);
        PinFakehitSet(true);
        s_pinFocus = member;
    } else {
        if (s_pinFocus >= 0) {
            logFile << "Aggro: FOCUS released: " << MemberName(s_pinFocus)
                    << "  (pin+suppress+fakehit all off)" << std::endl;
            s_pinFocus = MEMBER_NONE;  // чтобы setters не дублировали лог
        }
        PinSet(MEMBER_NONE, s_pinScope);
        PinSuppressSet(false);
        PinFakehitSet(false);
    }
}

int PinFocusMember() { return s_pinFocus; }

bool CardWatchOn() { return s_cardWatch; }

void SetCardWatch(bool on)
{
    if (s_cardWatch == on) return;
    s_cardWatch = on;
    if (!on) s_nCard = 0;
    logFile << "Aggro: cardwatch " << (on ? "ON" : "OFF") << std::endl;
}

int CardWatchCount() { return s_nCard; }

void MarkEvent(const char* tag)
{
    logFile << "Aggro: ---- MARK: " << (tag ? tag : "(no tag)")
            << " ---- switches so far " << s_switchTotal << std::endl;
    // GOBCARD (84.16): MARK режет замер на A/B — полные карточки goblin
    // в момент MARK дают вторую точку для временного диффа.
    CardReconDump();
    // Счётчики долей обнуляем: отметка режет замер на A и B, иначе доли
    // двух состояний смешались бы в одну бесполезную сумму.
    for (int i = 0; i < kMemberSlots; ++i) s_holdTicks[i] = 0;
    for (int i = 0; i < kMemberSlots; ++i) { s_conv[i].peak = 0; s_convLogged[i] = 0; }
    s_switchTotal = 0;
}

void Tick()
{
    if (!s_enabled && !s_directorObserver) return;

    // Director's demand is intentionally silent: it needs the native target
    // channel, not the historical research transcript. Manual diagnostics
    // (MARK/snapshot/roster) remain available regardless of this gate.
    const bool emitEvents = s_enabled && s_logEvents;
    const DWORD now = GetTickCount();
    if (s_lastTick && (now - s_lastTick) < 120) return;
    s_lastTick = now;

    LogIdentityAvailabilityIfChanged();
    PartyRef party[kMaxParty];
    const int nParty = CollectParty(party, kMaxParty);
    if (nParty <= 0) {
        lstrcpynA(s_status, "aggro watch: party not resolved", sizeof(s_status));
        return;
    }
    // Записи персонажей врать не могут и доступны всегда, живые тела —
    // надо найти. Расхождение печатаем, а не прячем.
    const int wantPawns = PartyRecordPawnCount();
    const int havePawns = nParty - 1;   // минус Аризен

    ForgetMissing();

    const int nEnemy = EnemyCount();
    int moving = 0;
    uint32_t bestOff = 0;
    uint32_t bestSw = 0;

    for (int i = 0; i < nEnemy; ++i) {
        const char* kind = 0;
        const uintptr_t body = EnemyBodyAt(i, &kind);
        if (!body) continue;

        Row* R = FindRow(body);
        if (!R) R = AddRow(body, kind);
        if (!R) continue;

        ReadLiveAct(body, R->act, sizeof(R->act));

        // Полный обход — только при первом появлении и раз в kRediscoverMs.
        // Состав слотов меняется: часть подобъектов создаётся при переходе
        // в бой, поэтому один обход на всю жизнь особи дал бы неполную
        // картину.
        if (R->nSlots == 0 || (now - R->lastScanMs) > kRediscoverMs) {
            Slot fresh[kMaxSlots];
            const int nf = DiscoverSlots(body, party, nParty, fresh, kMaxSlots);
            // Уже наблюдаемые смещения сохраняем со счётчиками: пересмотр
            // не должен обнулять историю смен, ради которой всё затевалось.
            for (int f = 0; f < nf; ++f) {
                bool known = false;
                for (int k = 0; k < R->nSlots; ++k) {
                    if (R->slot[k].off == fresh[f].off) { known = true; break; }
                }
                if (known) continue;
                if (R->nSlots >= kMaxSlots) break;
                R->slot[R->nSlots++] = fresh[f];
            }
            ClassifySlots(*R);
            R->lastScanMs = now;
        }
        EnsureGoblinRosterSlots(*R, party, nParty);

        // Обычный тик: перечитываем только известные смещения.
        for (int k = 0; k < R->nSlots; ++k) {
            Slot& S = R->slot[k];
            const int m = ReadSlot(body, S.off, party, nParty);
            if (m != S.member) {
                // Смена члена партии в слоте — это и есть событие, за
                // которым мы пришли. У него должен быть голос.
                if (S.member != MEMBER_NONE || m != MEMBER_NONE) {
                    ++S.switches;
                    ++s_switchTotal;
                    // High-volume research line: opt-in. Counters and target
                    // resolution still update in quiet observer mode.
                    if (emitEvents) {
                        logFile << "Aggro: " << R->kind
                                << " @" << std::hex << R->body << std::dec
                                << (S.kind == SLOT_ROSTER ? " [roster]" : " [target]")
                                << " +0x" << std::hex << S.off << std::dec
                                << "  " << MemberName(S.member)
                                << " -> " << MemberName(m)
                                << "  held " << (now - S.sinceMs) << " ms"
                                << "  act " << R->act << std::endl;
                    }
                }
                S.member = m;
                S.sinceMs = now;
            }
            S.holdMs = now - S.sinceMs;
                // Ростер в долю НЕ идёт: он статичен и перекашивал счёт (§15.5).
            if (S.kind != SLOT_ROSTER && m >= 0 && m < kMemberSlots) ++s_holdTicks[m];
        }

        // Самый ЛИПКИЙ подвижный слот считаем целью особи: быстрый слот
        // (пара 0x2B98/0x5CA4) сбрасывается в none между действиями и для
        // подсчёта схождения не годится — стая «расходилась» бы на каждом
        // замахе. Липкий (0x585C/0x6334) держит члена партии всё время боя.
        R->targetMember = MEMBER_NONE;
        {
            uint32_t bestHold = 0;
            for (int k = 0; k < R->nSlots; ++k) {
                if (R->slot[k].kind == SLOT_ROSTER) continue;
                if (R->slot[k].member == MEMBER_NONE) continue;
                if (R->slot[k].switches == 0) continue;   // застывший — не цель
                if (R->slot[k].holdMs >= bestHold) {
                    bestHold = R->slot[k].holdMs;
                    R->targetMember = R->slot[k].member;
                }
            }
        }

        // Самый подвижный слот этой особи — кандидат в цель.
        R->best = -1;
        uint32_t bs = 0;
        for (int k = 0; k < R->nSlots; ++k) {
            if (R->slot[k].kind == SLOT_ROSTER) continue;
            if (R->slot[k].switches > bs) { bs = R->slot[k].switches; R->best = k; }
        }
        if (R->best >= 0) {
            ++moving;
            if (bs > bestSw) { bestSw = bs; bestOff = R->slot[R->best].off; }
        }
    }

    // --- СХОЖДЕНИЕ СТАИ ------------------------------------------------
    //
    // Считаем, сколько особей смотрят на одного, и СРАЗУ спрашиваем у игры
    // действие самой жертвы. Без действия жертвы число бессмысленно: пять
    // волков на стоящем бойце и пять на лежачем — разные явления.
    {
        int tally[kMemberSlots];
        for (int m = 0; m < kMemberSlots; ++m) tally[m] = 0;
        for (int i = 0; i < s_nRow; ++i) {
            const int m = s_row[i].targetMember;
            if (m >= 0 && m < kMemberSlots) ++tally[m];
        }
        for (int m = 0; m < kMemberSlots; ++m) {
            s_conv[m].member = m;
            s_conv[m].count = tally[m];
            if (tally[m] > s_conv[m].peak) s_conv[m].peak = tally[m];

            if (tally[m] >= kConvergeMin) {
                // Действие жертвы читаем только когда схождение уже есть:
                // лишних чтений по всем телам каждый тик не делаем.
                for (int p = 0; p < nParty; ++p) {
                    if (party[p].member != m) continue;
                    ReadLiveAct(party[p].body, s_conv[m].memberAct,
                                sizeof(s_conv[m].memberAct));
                    break;
                }

                // Эпизод: первый тик >= порога ставит отсчёт.
                if (!s_conv[m].sinceMs) s_conv[m].sinceMs = now;
                if (tally[m] > s_conv[m].episodePeak)
                    s_conv[m].episodePeak = tally[m];

                // Логируем только РОСТ: иначе пятисекундное схождение дало бы
                // сорок одинаковых строк.
                if (tally[m] > s_convLogged[m]) {
                    s_convLogged[m] = tally[m];
                    if (emitEvents)
                        logFile << "Aggro: CONVERGE " << tally[m] << " foes -> "
                                << MemberName(m)
                                << "  victim act " << (s_conv[m].memberAct[0]
                                                       ? s_conv[m].memberAct : "?")
                                << std::endl;
                }
                // Таймлайн внутри эпизода: каждые 750 мс — что СЕЙЧАС
                // делает жертва. На догпайле именно тут ловим QTE/вал:
                // «CONVERGE 6 -> Hired1» один раз, а CONVHOLD 6 -> Hired1
                // с act cPlActGrabStart — это и есть сцена, которую
                // тестер видел глазами.
                if (now - s_conv[m].lastHoldMs >= 750) {
                    s_conv[m].lastHoldMs = now;
                    if (emitEvents)
                        logFile << "Aggro: CONVHOLD " << tally[m]
                                << " foes on " << MemberName(m)
                                << "  victim " << (s_conv[m].memberAct[0]
                                                   ? s_conv[m].memberAct : "?")
                                << "  +" << (now - s_conv[m].sinceMs)
                                << " ms" << std::endl;
                }
            } else {
                // Эпизод закончился: закрываем строкой-итогом. memberAct
                // ещё держит последнее увиденное действие жертвы — оно и
                // есть «чем кончилось схождение».
                if (s_conv[m].sinceMs) {
                    if (emitEvents)
                        logFile << "Aggro: CONVENDED " << MemberName(m)
                                << "  peak " << s_conv[m].episodePeak
                                << "  dur " << (now - s_conv[m].sinceMs)
                                << " ms  last victim "
                                << (s_conv[m].memberAct[0]
                                     ? s_conv[m].memberAct : "?")
                                << std::endl;
                    s_conv[m].sinceMs = 0;
                    s_conv[m].episodePeak = 0;
                    s_conv[m].lastHoldMs = 0;
                }
                s_conv[m].memberAct[0] = 0;
                s_convLogged[m] = 0;
            }
        }
    }

    // --- CARDWATCH -----------------------------------------------------
    //
    // Две особи с найденным ростером ведутся непрерывно (дельта-логика,
    // см. CardWatchState). Полный census не делаем: только карточки этих
    // двух тел + позиция волка и позиция члена карточки (652 байта не
    // читаем целиком — двенадцать точечных чтений по 4 байта).
    if (s_enabled && s_cardWatch && emitEvents) {
        // Умершие/вышедшие особи выбрасываем, место добровольно пустует.
        for (int i = 0; i < s_nCard; ++i)
            if (!FindRow(s_cardBody[i])) {
                s_cardBody[i] = s_cardBody[s_nCard - 1];
                --s_nCard;
                --i;
            }
        // Добрать до двух: любые особи с ростером из трёх и более карточек.
        for (int i = 0; i < s_nRow && s_nCard < 2; ++i) {
            Row& R = s_row[i];
            if (R.rosterCount < 3) continue;
            bool taken = false;
            for (int q = 0; q < s_nCard; ++q)
                if (s_cardBody[q] == R.body) { taken = true; break; }
            if (taken) continue;
            s_cardBody[s_nCard++] = R.body;
        }
        for (int i = 0; i < s_nCard; ++i) {
            Row* R = FindRow(s_cardBody[i]);
            if (!R) continue;
            CardWatchRow(*R, party, nParty, now);
        }
    } else {
        s_nCard = 0;
    }

    // --- PIN / DIRECTOR FOCUS actuator ----------------------------------
    // Manual PIN still requires watch=on. The product lease is separate and
    // may run under the quiet observer, but only while its record-slot body
    // still resolves to the exact expected pointer. Any ambiguity releases it
    // before a card write.
    bool directorActive = false;
    if (s_directorFocus >= 0 && s_directorFocus <= MEMBER_HIRED2) {
        uintptr_t resolved = 0;
        if (ResolveMemberBody(s_directorFocus, &resolved)
            && resolved == s_directorExpectedBody) {
            directorActive = true;
            s_directorIdentityBlockLogged = false;
        } else {
            if (!s_directorIdentityBlockLogged) {
                logFile << "Aggro: DIRECTOR FOCUS lost identity: slot="
                        << MemberName(s_directorFocus)
                        << " expected=0x" << std::hex << s_directorExpectedBody
                        << " resolved=0x" << resolved << std::dec
                        << " released before write" << std::endl;
                s_directorIdentityBlockLogged = true;
            }
            s_directorFocus = MEMBER_NONE;
            s_directorExpectedBody = 0;
            s_directorExcludedBody = 0;
            s_directorResponse = DIRECTOR_RESPONSE_NONE;
            lstrcpynA(s_directorKind, "uEm0200", sizeof(s_directorKind));
        }
    }

    const bool manualActive = !directorActive && s_enabled
                           && s_pinMember >= 0 && s_pinMember < kMemberSlots;
    if (directorActive || manualActive) {
        const int activeMember = directorActive ? s_directorFocus : s_pinMember;
        const int activeScope = directorActive ? 1 : s_pinScope;
        const bool directorAlarm = directorActive
                                && s_directorResponse == DIRECTOR_RESPONSE_ALARM;
        // 84.17: goblin-lease (GOBLIN-GRAB-ALERT) получает фейк-урон даже
        // в ALERT: блок B — «липкий» член, без него пин в мурле перебивает
        // восприятие (у волка это дало 18.7% -> 75.6%). Suppress на goblin
        // не даётся: он не проверен на виде (видоспецифичный допуск).
        const bool goblinLease = directorActive && s_directorKind[0]
                               && !strcmp(s_directorKind, "uEm0100");
        const bool hobLease = directorActive && s_directorKind[0]
                           && !strcmp(s_directorKind, "uEm0101");
        const bool activeSuppress = directorActive ? directorAlarm : s_pinSuppress;
        const bool activeFakehit = directorActive
            ? (directorAlarm || goblinLease || hobLease) : s_pinFakehit;
        uintptr_t mBody = 0;
        for (int p = 0; p < nParty; ++p) {
            if (party[p].member != activeMember) continue;
            mBody = party[p].body;
            break;
        }
        if (mBody) {
            if (activeScope == 1) {
                int nW = 0;
                int nLeft = 0;
                for (int i = 0; i < s_nRow; ++i) {
                    if (!IsPinnableKind(s_row[i].kind, directorActive)) continue;
                    if (directorActive && s_directorKind[0]
                        && strcmp(s_row[i].kind, s_directorKind) != 0)
                        continue;
                    if (directorActive && s_row[i].body == s_directorExcludedBody)
                        continue;
                    ++nW;
                    if (directorActive
                        && CombatOccupiesOther(s_row[i], party, nParty, activeMember)) {
                        ++nLeft;
                        continue;
                    }
                    PinRow(s_row[i], party, nParty, now, activeMember,
                           activeSuppress, activeFakehit, directorActive);
                }
                PinSummary(nW, nLeft, now, activeMember, activeScope,
                           activeSuppress, activeFakehit, directorActive);
            } else {
                // Ближайший к заштыренному члену волк: «приведи к нему».
                float mp[3] = {};
                if (ReadBodyPos(mBody, mp)) {
                    Row* best = 0;
                    float bestD = 1.0e9f;
                    for (int i = 0; i < s_nRow; ++i) {
                        Row& R = s_row[i];
                        if (!IsPinnableKind(R.kind, directorActive)) continue;
                        if (directorActive && s_directorKind[0]
                            && strcmp(R.kind, s_directorKind) != 0) continue;
                        float wp[3] = {};
                        if (!ReadBodyPos(R.body, wp)) continue;
                        const float dx = wp[0] - mp[0];
                        const float dy = wp[1] - mp[1];
                        const float dz = wp[2] - mp[2];
                        const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
                        if (d < bestD) { bestD = d; best = &R; }
                    }
                    int nLeft = 0;
                    if (best) {
                        if (directorActive
                            && CombatOccupiesOther(*best, party, nParty, activeMember))
                            ++nLeft;
                        else
                            PinRow(*best, party, nParty, now, activeMember,
                                   activeSuppress, activeFakehit, directorActive);
                    }
                    PinSummary(best ? 1 : 0, nLeft, now, activeMember, activeScope,
                               activeSuppress, activeFakehit, directorActive);
                }
            }
        }
    }

    // Статус — факт, а не намерение: сколько особей, сколько подвижных
    // слотов и какое смещение чаще всего двигается по всей пачке.
    const char* warn = (wantPawns > havePawns) ? "  [PARTY INCOMPLETE]" : "";
    char cardInfo[40] = {};
    if (s_cardWatch && s_nCard)
        wsprintfA(cardInfo, "  cardwatch %d", s_nCard);
    if (bestSw)
        wsprintfA(s_status,
                  "aggro watch: %d enemies, %d moving, top +0x%X (%u sw), pawns %d/%d%s%s",
                  nEnemy, moving, bestOff, bestSw, havePawns, wantPawns, warn,
                  cardInfo);
    else
        wsprintfA(s_status,
                  "aggro watch: %d enemies, no slot moved yet, pawns %d/%d%s%s",
                  nEnemy, havePawns, wantPawns, warn, cardInfo);
}

int RowCount() { return s_nRow; }

const Row* RowAt(int i)
{
    if (i < 0 || i >= s_nRow) return 0;
    return &s_row[i];
}

const char* Status() { return s_status; }

const Converge* ConvergeAt(int member)
{
    if (member < 0 || member >= kMemberSlots) return 0;
    return &s_conv[member];
}

void DumpSnapshot()
{
    logFile << "Aggro: ===== snapshot: " << s_nRow << " enemies =====" << std::endl;
    for (int i = 0; i < s_nRow; ++i) {
        const Row& R = s_row[i];
        logFile << "Aggro:  " << R.kind << " @" << std::hex << R.body << std::dec
                << "  act " << R.act << "  slots " << R.nSlots;
        if (R.rosterCount)
            logFile << "  roster base +0x" << std::hex << R.rosterBase
                    << " stride 0x" << R.rosterStride << std::dec
                    << " x" << R.rosterCount;
        logFile << std::endl;
        for (int k = 0; k < R.nSlots; ++k) {
            const Slot& S = R.slot[k];
            logFile << "Aggro:    +0x" << std::hex << S.off << std::dec
                    << (S.kind == SLOT_ROSTER ? " [roster] " : " [target] ")
                    << MemberName(S.member)
                    << "  switches " << S.switches
                    << "  held " << S.holdMs << " ms"
                    << (k == R.best ? "   <== most movement" : "") << std::endl;
        }
    }
    logFile << "Aggro:  pack convergence peaks since last MARK:";
    for (int m = 0; m < kMemberSlots; ++m)
        if (s_conv[m].peak) logFile << "  " << MemberName(m) << "=" << s_conv[m].peak;
    logFile << std::endl;
    logFile << "Aggro:  share since last MARK:";
    for (int m = 0; m < kMemberSlots; ++m)
        logFile << "  " << MemberName(m) << "=" << s_holdTicks[m];
    logFile << "   total switches " << s_switchTotal << std::endl;
    CardReconDump();
}


// ЭТАП 2 БЕЗ МОЛОТА (см. AGGRO_RECON §15.6).
//
// ЗАМЫСЕЛ. Карточка известного актёра — 652 байта. Большая часть из них
// одинакова у всех четырёх членов партии: это общие настройки, а не
// сведения о конкретном человеке. Значит искать надо не «поле ненависти»
// вслепую, а РАЗЛИЧИЯ между карточками одной особи.
//
// Поле, различающееся у четверых, — покарточная величина. Дальше её
// опознаём по поведению: меняется с расстоянием — дистанция; скачет,
// когда этот член партии ударил, — свежий урон; стоит намертво и зависит
// от снаряжения — тот самый статический вес.
//
// Печатаем только различия: полный дамп 4x652 залил бы лог четырьмя
// тысячами бесполезных строк.
void DumpRoster()
{
    int printed = 0;
    for (int i = 0; i < s_nRow; ++i) {
        const Row& R = s_row[i];
        if (R.rosterCount < 3 || !R.rosterStride) continue;

        logFile << "Aggro: ===== roster card diff: " << R.kind
                << " @" << std::hex << R.body << std::dec
                << "  base +0x" << std::hex << R.rosterBase
                << " stride 0x" << R.rosterStride << std::dec
                << " x" << R.rosterCount << " =====" << std::endl;

        // Шапка: какой столбец какому члену партии принадлежит.
        logFile << "Aggro:   columns:";
        for (int k = 0; k < R.rosterCount; ++k) {
            const uint32_t off = R.rosterBase + (uint32_t)k * R.rosterStride;
            const char* who = "?";
            for (int q = 0; q < R.nSlots; ++q)
                if (R.slot[q].off == off) who = MemberName(R.slot[q].member);
            logFile << "  [" << k << "]=" << who;
        }
        logFile << std::endl;

        int rows = 0;
        for (uint32_t d = 0; d + 4 <= R.rosterStride && rows < 96; d += 4) {
            uint32_t v[kMaxParty]; bool ok = true;
            for (int k = 0; k < R.rosterCount; ++k) {
                const uintptr_t a = R.body + R.rosterBase + (uintptr_t)k * R.rosterStride + d;
                if (!Rd((void*)a, &v[k], 4)) { ok = false; break; }
            }
            if (!ok) continue;

            bool same = true;
            for (int k = 1; k < R.rosterCount; ++k) if (v[k] != v[0]) { same = false; break; }
            if (same) continue;   // общее для всех — не про члена партии

            // Указатели пропускаем: это ссылки на тела, они и так различны
            // у каждой карточки и ничего про вес не говорят.
            bool ptrish = true;
            for (int k = 0; k < R.rosterCount; ++k) if (!LooksHeap(v[k])) { ptrish = false; break; }
            if (ptrish) continue;

            logFile << "Aggro:   +0x" << std::hex << d << std::dec << "  ";
            for (int k = 0; k < R.rosterCount; ++k) {
                float f; memcpy(&f, &v[k], 4);
                char cell[48];
                // Печатаем и как целое, и как float: какой смысл у поля,
                // решают числа, а не наша догадка.
                if (f > -100000.0f && f < 100000.0f && (f > 0.0001f || f < -0.0001f || v[k] == 0))
                    sprintf_s(cell, "%12.3f", f);
                else
                    sprintf_s(cell, "%12d", (int)v[k]);
                logFile << cell;
            }
            logFile << std::endl;
            ++rows;
        }
        logFile << "Aggro:   (" << rows << " differing fields)" << std::endl;
        if (++printed >= 2) break;   // двух особей достаточно для сверки
    }
    if (!printed)
        logFile << "Aggro: no enemy has a roster array yet"
                   " (need a foe that has perceived the party)" << std::endl;
}

} // namespace Aggro
} // namespace Runtime

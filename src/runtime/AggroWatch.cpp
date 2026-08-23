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
static const float kPinValue = 300.0f;   // нативный потолок (линия при 0 м)
static const float kSuppValue = 0.0f;    // нативное «полностью затухшее»

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
    // state: 0 exact, 1 record unavailable, 2 body unresolved/duplicate.
    static const char* kStatus[4][3] = {
        { "identity-Arisen-exact",   "identity-Arisen-record-unavailable",
          "identity-Arisen-body-unresolved-or-duplicate" },
        { "identity-MainPawn-exact", "identity-MainPawn-record-unavailable",
          "identity-MainPawn-body-unresolved-or-duplicate" },
        { "identity-Hired1-exact",   "identity-Hired1-record-unavailable",
          "identity-Hired1-body-unresolved-or-duplicate" },
        { "identity-Hired2-exact",   "identity-Hired2-record-unavailable",
          "identity-Hired2-body-unresolved-or-duplicate" }
    };
    if (member < MEMBER_ARISEN || member > MEMBER_HIRED2)
        return "identity-invalid-slot";
    if (state < 0 || state > 2) state = 2;
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
        if (!body) return MemberIdentityStatus(member, 2);
    } else {
        const int recordIdx = member - MEMBER_MAIN;
        if (!PartyRecordInfo(recordIdx, 0, 0, &body))
            return MemberIdentityStatus(member, 1);
        // PartyRecordInfo returns a body only for exactly one claimant.
        if (!body) return MemberIdentityStatus(member, 2);
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
    // прогрессию. Слотов максимум 8 — перебор дешёвый.
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
// Только uEm0200: карточка верифицирована на волках (два боя, два
// набора особей, база/шаг/поля сошлись). На другом виде +0x10 от головы
// карточки — чужое поле, и писать в него нельзя (FIX_RULES: субобъект —
// по проверенному имени/форме, а не по совпадению смещения).
static bool IsPinnableKind(const char* kind)
{
    // The card layout is admitted for this exact live DTI species only.
    // Prefix matching could silently write an unverified subtype/layout.
    return kind && strcmp(kind, "uEm0200") == 0;
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

// Одна карта: форма -> диапазон -> запись -> readback -> откат.
// want = kPinValue (300, штырь) или kSuppValue (0, подавление, 81.0).
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

    // Форма карты перед записью: живая (+0x08=1) и «наша» (+0x0C=4 —
    // константа, увиденная на обоих волках в обоих боях). Иначе это
    // уже не та структура — не пишем.
    uint32_t flag = 0, c4 = 0;
    if (!Rd((void*)(card + 0x08), &flag, 4)
        || !Rd((void*)(card + 0x0C), &c4, 4)
        || flag != 1 || c4 != 4) {
        // 81.0: аномалия не молчит — печатаем фактическую форму, чтобы
        // понять, старая ли это указатель-оболочка или чужая структура.
        char buf[64] = {};
        wsprintfA(buf, "shape: f8=%u fC=%u (want 1/4) — stale/other card?",
                  flag, c4);
        PinAnomaly(R.body, buf);
        return 1;
    }

    uintptr_t att = card + 0x10;
    float cur = 0.0f;
    if (!Rd((void*)att, &cur, 4)) return 1;
    // Нативный диапазон поля: 0..300 (линия 300-10d). Если там другое
    // число — наше поле уже не то, запись отменяется.
    if (cur < -0.001f || cur > 320.0f) {
        PinAnomaly(R.body, "attention value out of native range");
        return 1;
    }

    // Запись -> readback -> при рассинхроне откат к прочитанному.
    if (!WrSafe((void*)att, &want, 4)) {
        PinAnomaly(R.body, "WrSafe failed");
        return 1;
    }
    float back = 0.0f;
    if (!Rd((void*)att, &back, 4) || back != want) {
        WrSafe((void*)att, &cur, 4);
        ++s_pinRollbacks;
        // Откат — редкое важное событие: логируем сразу, без гетеризации.
        logFile << "Aggro: PIN ROLLBACK @" << std::hex << R.body << std::dec
                << " card +0x" << std::hex << S.off << std::dec
                << "  readback " << back << " != " << want
                << "  restored " << cur << std::endl;
        return 2;
    }
    if (want == kPinValue) ++s_pinWrites; else ++s_pinSuppWrites;
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
                << "  att " << cur << " -> " << want
                << (want == kPinValue ? "  readback ok" : "  supp ok")
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

// 83.0: диагностика формы. Карта не легла в 1/4 (переходное fC=1/2,
// мёртвая оболочка, чужая структура) — печатаем ГОЛОВЫ всех карт
// особи: только чтение, троттл 5 с на особь. На этих строках ловим
// переходы 4->2->4 и учимся режимам «боевой» карты (10 до ~500,
// вес 0.2, медленное затухание — замер 82.0, §25.3).
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
            // один член формулы не трогаем.
            const int rc = PinWriteCard(R, S, who, kPinValue, now, director);
            if (rc == 0) {
                if (fakehit)
                    PinFakehitCard(R, S, who, now, director);
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

static void PinSummary(int nWolves, DWORD now, int targetMember, int scope,
                       bool suppress, bool fakehit, bool director)
{
    // First summary is immediate; unchanged leases then report at most once
    // per ten seconds. Engagement/release transitions are logged elsewhere.
    if (s_pinLastLog && now - s_pinLastLog < 10000) return;
    s_pinLastLog = now;
    // held: сколько особей ДЕРЖАТ заштыренного в текущей цели прямо сейчас
    // (метрика «прилипло» в лог — глазу больше не приходится гадать).
    int held = 0;
    for (int i = 0; i < s_nRow; ++i) {
        if (!IsPinnableKind(s_row[i].kind)) continue;
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
                      uintptr_t excludedEnemyBody, int response)
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
        s_directorIdentityBlockLogged = false;
        return true;
    }
    if (member > MEMBER_HIRED2 || !expectedBody
        || (response != DIRECTOR_RESPONSE_ALERT
            && response != DIRECTOR_RESPONSE_ALARM))
        return false;

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
        return false;
    }

    if (s_directorFocus != member || s_directorExpectedBody != expectedBody
        || s_directorExcludedBody != excludedEnemyBody
        || s_directorResponse != response) {
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
                << " identity=EXACT scope=free-wolves "
                << (response == DIRECTOR_RESPONSE_ALERT
                    ? "pin-only" : "pin+suppress+fakehit")
                << std::endl;
    }
    s_directorFocus = member;
    s_directorExpectedBody = expectedBody;
    s_directorExcludedBody = excludedEnemyBody;
    s_directorResponse = response;
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
        }
    }

    const bool manualActive = !directorActive && s_enabled
                           && s_pinMember >= 0 && s_pinMember < kMemberSlots;
    if (directorActive || manualActive) {
        const int activeMember = directorActive ? s_directorFocus : s_pinMember;
        const int activeScope = directorActive ? 1 : s_pinScope;
        const bool directorAlarm = directorActive
                                && s_directorResponse == DIRECTOR_RESPONSE_ALARM;
        const bool activeSuppress = directorActive ? directorAlarm : s_pinSuppress;
        const bool activeFakehit = directorActive ? directorAlarm : s_pinFakehit;
        uintptr_t mBody = 0;
        for (int p = 0; p < nParty; ++p) {
            if (party[p].member != activeMember) continue;
            mBody = party[p].body;
            break;
        }
        if (mBody) {
            if (activeScope == 1) {
                int nW = 0;
                for (int i = 0; i < s_nRow; ++i) {
                    if (!IsPinnableKind(s_row[i].kind)) continue;
                    if (directorActive && s_row[i].body == s_directorExcludedBody)
                        continue;
                    PinRow(s_row[i], party, nParty, now, activeMember,
                           activeSuppress, activeFakehit, directorActive);
                    ++nW;
                }
                PinSummary(nW, now, activeMember, activeScope,
                           activeSuppress, activeFakehit, directorActive);
            } else {
                // Ближайший к заштыренному члену волк: «приведи к нему».
                float mp[3] = {};
                if (ReadBodyPos(mBody, mp)) {
                    Row* best = 0;
                    float bestD = 1.0e9f;
                    for (int i = 0; i < s_nRow; ++i) {
                        Row& R = s_row[i];
                        if (!IsPinnableKind(R.kind)) continue;
                        float wp[3] = {};
                        if (!ReadBodyPos(R.body, wp)) continue;
                        const float dx = wp[0] - mp[0];
                        const float dy = wp[1] - mp[1];
                        const float dz = wp[2] - mp[2];
                        const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
                        if (d < bestD) { bestD = d; best = &R; }
                    }
                    if (best)
                        PinRow(*best, party, nParty, now, activeMember,
                               activeSuppress, activeFakehit, directorActive);
                    PinSummary(best ? 1 : 0, now, activeMember, activeScope,
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

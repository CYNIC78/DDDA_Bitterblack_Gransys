// MonsterAI::Director — Build 84 / session Build 012.
//
// Absolute-current-HP PackMark and table-driven restraint cues remain intact.
// Every admitted order now retains target plus normalized urgency. Aggro owns
// target response strength; Tempo consumes urgency through one non-stacking
// immutable-endpoint mobilization envelope per exact free uEm0200 responder.
// Ordinary evidence completion decays; unsafe identity/readiness/topology,
// timeout, disable, shutdown, rollback, or stale-world failures reset at once.

#include "stdafx.h"
#include "MonsterDirector.h"
#include "TacticalCues.h"
#include "SpeciesCard.h"
#include "SpeciesTuning.h"
#include "PackObserve.h"
#include "../runtime/Runtime.h"
#include "../CombatBus.h"
#include "../runtime/MonsterTempo.h"
#include "../runtime/AggroWatch.h"
#include "../runtime/PartyStatus.h"
#include "../ActMap.Generated.h"
#include <math.h>
#include <string.h>

namespace MonsterAI {

static const int   kMaxViews       = 32;
static const DWORD kTickMs         = 150;
// 85.29: не чаще одной строки BLOCKED на одно и то же состояние.
static const DWORD kFallenDiagRepeatMs = 3000;
static const DWORD kWorldFreshMs   = 450; // three situation scans; stale fails closed
static const DWORD kDecisionMs     = 500;
static const DWORD kMinHoldMs      = 2500;
// HEARTBEAT removed: log only SELECT/SWITCH/MODE/CLEAR (state change).
static const float kSwitchMargin   = 1.20f;
static const float kBiasIsolation  = 0.20f;
// Data-driven first commit boundary: in the validated fight MainPawn became a
// genuine focus opportunity at 164.8 HP versus a 331.3 HP runner (+101%).
static const float kFocusIsolation = 1.00f;

static bool        s_enabled = false;
static MonsterView s_view[kMaxViews];
static int         s_nView = 0;
static DWORD       s_lastTick = 0;
static DWORD       s_lastDecision = 0;
static DWORD       s_lastLog = 0;
static char        s_status[420] = "Monster Director: disabled";

struct TargetScore {
    bool  valid;
    bool  hpValid;
    float lowAbsoluteHp;
    float huntScore;
};

// 85.36: адаптер «живой конфиг -> интерфейс модуля чисел вида».
// Отдельная прослойка нужна не для красоты: рантайм-фикстура директора
// подменяет конфиг своим двойником, и модуль SpeciesTuning не должен знать ни
// про iniConfig, ни про этот двойник.
struct IniSpeciesReader : MonsterAI::SpeciesIniReader,
                          Runtime::Tempo::RanksIniReader {
    float Float(const char* section, const char* key, float defValue) override {
        return config.getFloat(section, key, defValue);
    }
    bool Bool(const char* section, const char* key, bool defValue) override {
        return config.getBool(section, key, defValue);
    }
};

static Runtime::PartyCombatSnapshot s_party;
static Runtime::PartyCombatSnapshot s_cueParty;
static TargetScore s_score[Runtime::PARTY_COMBAT_SLOTS];
static int         s_order[Runtime::PARTY_COMBAT_SLOTS] = { -1, -1, -1, -1 };
static int         s_wolfCount = 0;
static int         s_hobCount = 0;
static int         s_saurCount = 0;
static int         s_mark = -1;
static int         s_runner = -1;
static DWORD       s_markSince = 0;
static uint64_t    s_partySignature = 0;
static bool        s_havePartySignature = false;
static int         s_mode = RECOMMEND_NONE; // advisory focus opportunity only
static int         s_lastLoggedMode = -1;
static char        s_reason[96] = "waiting";

struct TacticalRuntime {
    bool      active;
    bool      timeoutBlocked;
    bool      partialLogged;
    uint64_t  partialSignature;
    int       situation;
    int       response;
    float     urgency;
    int       targetSlot;
    uintptr_t targetBody;
    uintptr_t victimBody;
    bool      excludeVictim;
    DWORD     sinceMs;
    uint32_t  maxLeaseMs;
    uint64_t  topologySignature;
    float     pairDistanceM;
    char      targetAct[64];
    char      victimAct[64];
    char      responderKind[16];
    // 85.29: почему встреча у тела не состоялась. Телеметрия, а не состояние
    // боя: на решения не влияет, живёт для строки BLOCKED и для тестов.
    MonsterAI::TacticalFallenDiag fallen;   // 11: встреча у тела
    MonsterAI::TacticalFallenDiag finish;   // 12: добивание
};
static TacticalRuntime s_tactical;
static bool             s_policyHardResetPending = false;
static char             s_policyHardResetReason[96] = {};

// Ground restraint is no longer a special observer. Its proved exact party
// actions live in TacticalCues beside every other data-driven situation.

// Every admitted Director order is an emergency. ALERT and ALARM remain
// tactically distinct in Aggro and cue lease, but both request full normalized
// mobilization from the same bounded per-body Tempo envelope.
static const int   kMaxPolicyWolves = 16; // matches Tempo's bounded table
static const float kEmergencyUrgency = 1.0f;
static const DWORD kPolicyTtlMs = 600;    // fail-safe; normal release is explicit
static bool        s_actuatorEnabled = false;
static bool        s_policyEngaged = false;
static uintptr_t   s_ownedWolf[kMaxPolicyWolves] = {};
static int         s_nOwnedWolf = 0;
static int         s_policyTarget = -1;
static uintptr_t   s_policyTargetBody = 0;
static int         s_policySituation = TACTICAL_SITUATION_NONE;
static int         s_policyResponse = TACTICAL_RESPONSE_NONE;
static float       s_policyUrgency = 0.0f;
static float       s_policyL0Lo = 0.0f, s_policyL0Hi = 0.0f;
static float       s_policyA0Lo = 0.0f, s_policyA0Hi = 0.0f;
static float       s_policyL1Lo = 0.0f, s_policyL1Hi = 0.0f;
static float       s_policyA1Lo = 0.0f, s_policyA1Hi = 0.0f;
static uintptr_t   s_policyExcludedBody = 0;
static uint64_t    s_policyEventTopology = 0;
static uintptr_t   s_responderWolf[kMaxPolicyWolves] = {};
static int         s_nResponderWolf = 0;
// 85.23: сколько особей вообще получает приказ. 0 = все (как было), N = N
// ближайших к очагу. Причина: приказ «выручай собрата» превращался в «вся
// пачка разом», а это уже не тактика, а ганк. Ключ [monsterAI] responderMax.
static int         s_policyResponderMax = 0;

// 85.25: «услышал каст» — брать ближайшего монстра вместо отказа, когда рядом
// их несколько. Хватов и прижимов не касается: там уникальность пары
// доказывает, кого именно держат. См. SetNearestPairFallback в TacticalCues.h.
static bool        s_chantNearest = true;

// ---- 85.28: FALLEN GUARD -------------------------------------------------
// Радиус подхода игрока к телу павшей пешки. Сам механизм живёт в движке
// приказов (TacticalCues, ситуация FALLEN-GUARD): в 85.27 он был встроен в
// выбор цели и не сработал — выбор цели в игре ничего не делает, его читает
// только панель. Здесь остаётся только ключ и передача его в матчер.
static float       s_fallenGuardRadius = 10.0f;  // 0 = выключено

// 85.33: ПАРАЛЛЕЛЬНЫЙ ПРИКАЗ (второе событие).
//
// Владелец: «трубит горнист, и одновременно упала пешка — директор даёт сигналы,
// пачка делится: часть защищает горниста, вторая атакует лежащую пешку».
//
// Устройство намеренно асимметричное. Главный приказ живёт как жил: аренда,
// продолжение, таймаут, освобождение, темп. Второй приказ — ТОЛЬКО внимание
// (агрессия): он пересчитывается каждый скан, темпа не арендует и обязательств
// не берёт. Так проверенный механизм главного приказа не получает ни одной новой
// ветки, а пачка получает вторую задачу.
//
// Ключ `parallelOrders`: 0 (по умолчанию в этой сборке) = деление только
// ЗАПИСЫВАЕТСЯ в лог, мир не меняется; 1 = деление исполняется.
static bool        s_parallelOrders = false;

// 85.34: исполнители второго приказа, которым выдан темп (а с ним и адреналин).
// Список нужен ровно для одного: снять оболочки у тех, кто выбыл из набора.
static uintptr_t   s_secOwned[kMaxPolicyWolves] = {};
static int         s_nSecOwned = 0;

// Снять оболочки со ВТОРОГО приказа. Темп первого приказа не трогаем никогда:
// списки исполнителей не пересекаются, но лишняя попытка освободить чужое тело
// стоила бы тихого снятия темпа у того, кто идёт по главному приказу.
static void ReleaseSecondaryMobilization(const char* reason)
{
    if (!s_nSecOwned) return;
    for (int i = 0; i < s_nSecOwned; ++i)
        Runtime::Tempo::ReleaseDirectorMobilization(s_secOwned[i]);
    logFile << "Monster Director: adrenaline secondary released (" << s_nSecOwned
            << " bodies, reason=" << (reason ? reason : "clear") << ")" << std::endl;
    s_nSecOwned = 0;
}
struct ParallelOrder {
    bool        active;
    int         situation;
    int         targetSlot;
    uintptr_t   targetBody;
    const char* responderKind;
    int         response;          // TACTICAL_RESPONSE_*
    float       urgency;           // 85.34: уровень всплеска у второго приказа
    float       score;
    float       pairDistanceM;
    int         nResponder;
    uintptr_t   responders[kMaxPolicyWolves];
    // Доли главного приказа: когда деление включено, первый берёт СВОЮ часть,
    // а не всех ближних (иначе одни и те же особи получили бы два приказа).
    int         nPrimary;
    uintptr_t   primary[kMaxPolicyWolves];
    uint32_t    signature;         // для лога: менялось ли деление
};
static ParallelOrder s_parallel;
static uint32_t      s_lastSplitLogSignature = 0;
static DWORD         s_lastSplitLogMs = 0;
// 85.30: добивание лежащей пешки в сознании (ключ `pawnFinish`). Включено по
// умолчанию — это прямое указание владельца по полю 85.29. Ключ
// fallenGuardRadius к нему НЕ относится: у добивания нет радиуса от игрока,
// работает близость монстров к пешке.
static bool        s_pawnFinish = true;
static int         s_gameplayWrites = 0;
static char        s_policyStatus[128] = "actuator-off";
// Release coalescing is deliberately separate from UI/status text. NONE/BIAS
// may alternate with a fail-closed reason while no row is owned; that must not
// replay actuator cleanup or print a fake gameplay transition every 150 ms.
static bool        s_inactiveResetLatched = false;
static bool        s_inactiveSafetyLatched = false;
static char        s_inactiveSafetyReason[96] = {};
static uint32_t    s_inactiveSafetySuppressed = 0;

static float Clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static const char* ModeName(int mode)
{
    if (mode == RECOMMEND_BIAS) return "BIAS";
    if (mode == RECOMMEND_FOCUS) return "FOCUS-WINDOW";
    return "NONE";
}

static bool IsWolf(const MonsterView& v)
{
    return !strcmp(v.kind, "uEm0200");
}

static bool IsHob(const MonsterView& v)
{
    return !strcmp(v.kind, "uEm0101");
}

static bool IsSaur(const MonsterView& v)
{
    return !strcmp(v.kind, "uEm0400");
}

struct FirstSeen { uintptr_t body; DWORD ms; };
static FirstSeen s_seen[kMaxViews];
static int       s_nSeen = 0;

static DWORD FirstSeenMs(uintptr_t body, DWORD now)
{
    for (int i = 0; i < s_nSeen; ++i)
        if (s_seen[i].body == body) return s_seen[i].ms;
    if (s_nSeen < kMaxViews) {
        s_seen[s_nSeen].body = body;
        s_seen[s_nSeen].ms = now;
        ++s_nSeen;
    }
    return now;
}

static void ForgetMissing()
{
    int out = 0;
    for (int i = 0; i < s_nSeen; ++i) {
        bool alive = false;
        for (int k = 0; k < s_nView; ++k) {
            if (s_view[k].body == s_seen[i].body) {
                alive = true;
                break;
            }
        }
        if (!alive) continue;
        if (out != i) s_seen[out] = s_seen[i];
        ++out;
    }
    s_nSeen = out;
}

static void UpdateViews(DWORD now)
{
    const WorldReport w = CombatBus::Instance().LastWorld();
    s_nView = 0;
    if (!w.timestampMs || now - w.timestampMs > kWorldFreshMs) {
        ForgetMissing();
        return;
    }

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    const bool haveArisen = Runtime::GetArisenWorldPos(&ax, &ay, &az);

    for (int i = 0; i < w.count && s_nView < kMaxViews; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;

        MonsterView& v = s_view[s_nView++];
        memset(&v, 0, sizeof(v));
        v.body = u.ptr;
        lstrcpynA(v.kind, u.kind, sizeof(v.kind));
        lstrcpynA(v.act, u.actName, sizeof(v.act));
        v.dead = false; // dead bodies are excluded from WorldReport::units
        v.attacking = ActMap::NameIsAttack(v.act);
        v.x = u.x;
        v.y = u.y;
        v.z = u.z;
        v.positionValid = (v.x == v.x && v.y == v.y && v.z == v.z);
        v.distM = -1.0f;
        if (haveArisen && v.positionValid) {
            const float dx = v.x - ax;
            const float dy = v.y - ay;
            const float dz = v.z - az;
            v.distM = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
        }
        v.locoFactor = 1.0f;
        v.atkFactor = 1.0f;
        Runtime::Tempo::GetFactors(v.body, &v.locoFactor, &v.atkFactor);
        v.seenMs = (uint32_t)FirstSeenMs(v.body, now);
    }
    ForgetMissing();
}

static void TacticalRelease(const char* reason, bool timeoutBlock,
                            bool hardReset, DWORD now)
{
    if (s_tactical.active) {
        logFile << "Monster Director: situation RELEASED name="
                << TacticalSituationName(s_tactical.situation)
                << " response=" << TacticalResponseName(s_tactical.response)
                << " urgency=" << s_tactical.urgency
                << " reason=" << (reason ? reason : "unknown")
                << " actuation=" << (hardReset ? "HARD-RESET" : "DECAY")
                << " target=" << Runtime::PartyCombatSlotName(s_tactical.targetSlot)
                << " victim=0x" << std::hex << s_tactical.victimBody << std::dec
                << " dur=" << (now - s_tactical.sinceMs) << "ms"
                << std::endl;
    }
    if (hardReset) {
        s_policyHardResetPending = true;
        lstrcpynA(s_policyHardResetReason, reason ? reason : "tactical-unsafe-release",
                  sizeof(s_policyHardResetReason));
    }
    s_tactical.active = false;
    s_tactical.timeoutBlocked = timeoutBlock;
    if (!timeoutBlock) {
        s_tactical.situation = TACTICAL_SITUATION_NONE;
        s_tactical.response = TACTICAL_RESPONSE_NONE;
        s_tactical.urgency = 0.0f;
        s_tactical.targetSlot = -1;
        s_tactical.targetBody = 0;
        s_tactical.victimBody = 0;
        s_tactical.excludeVictim = false;
        s_tactical.sinceMs = 0;
        s_tactical.maxLeaseMs = 0;
        s_tactical.topologySignature = 0;
        s_tactical.pairDistanceM = -1.0f;
        s_tactical.targetAct[0] = 0;
        s_tactical.victimAct[0] = 0;
        s_tactical.responderKind[0] = 0;
    }
}

static void TacticalEnter(const TacticalMatch& m, DWORD now,
                          uint64_t topologySignature)
{
    s_tactical.active = true;
    s_tactical.timeoutBlocked = false;
    s_tactical.partialLogged = false;
    s_tactical.partialSignature = 0;
    s_tactical.situation = m.situation;
    s_tactical.response = m.response;
    s_tactical.urgency = m.urgency;
    s_tactical.targetSlot = m.targetSlot;
    s_tactical.targetBody = m.targetBody;
    s_tactical.victimBody = m.evidenceBody;
    s_tactical.excludeVictim = m.excludeEvidenceBody;
    s_tactical.sinceMs = now;
    s_tactical.maxLeaseMs = m.maxLeaseMs;
    s_tactical.topologySignature = topologySignature;
    s_tactical.pairDistanceM = m.pairDistanceM;
    lstrcpynA(s_tactical.targetAct, m.targetAct ? m.targetAct : "?",
              sizeof(s_tactical.targetAct));
    lstrcpynA(s_tactical.victimAct, m.evidenceAct ? m.evidenceAct : "?",
              sizeof(s_tactical.victimAct));
    lstrcpynA(s_tactical.responderKind, m.responderKind ? m.responderKind : "",
              sizeof(s_tactical.responderKind));
    logFile << "Monster Director: situation ENGAGED name=" << m.name
            << " response=" << TacticalResponseName(m.response)
            << " urgency=" << m.urgency
            << " target=" << Runtime::PartyCombatSlotName(m.targetSlot)
            << " holderAct=" << (m.targetAct ? m.targetAct : "?")
            << " victim=0x" << std::hex << m.evidenceBody << std::dec
            << " victimAct=" << (m.evidenceAct ? m.evidenceAct : "?")
            << " distance=" << m.pairDistanceM << "m"
            << " leaseMax=" << m.maxLeaseMs << "ms";
    // 85.25: если пар в радиусе было несколько — показываем, из скольких выбрали
    // ближайшую. У строгих правил (хват, прижим) этого не бывает: там пара
    // ровно одна, поэтому поле просто отсутствует.
    if (m.pairsConsidered > 1)
        logFile << " nearestOf=" << m.pairsConsidered;
    logFile << std::endl;
}

static uint64_t TacticalPartialSignature(const TacticalScan& scan)
{
    uint64_t h = 1469598103934665603ULL;
#define TACTICAL_HASH_VALUE(v) do { h ^= (uint64_t)(v); h *= 1099511628211ULL; } while (0)
    TACTICAL_HASH_VALUE(scan.situation);
    TACTICAL_HASH_VALUE(scan.response);
    TACTICAL_HASH_VALUE(scan.targetCandidates);
    TACTICAL_HASH_VALUE(scan.evidenceCandidates);
    TACTICAL_HASH_VALUE(scan.pairCandidates);
    TACTICAL_HASH_VALUE(scan.positionRejected);
    TACTICAL_HASH_VALUE(scan.firstTargetSlot);
    TACTICAL_HASH_VALUE(scan.firstTargetBody);
    TACTICAL_HASH_VALUE(scan.firstEvidenceBody);
    const char* acts[2] = { scan.firstTargetAct, scan.firstEvidenceAct };
    for (int a = 0; a < 2; ++a) {
        const char* p = acts[a];
        if (!p) { TACTICAL_HASH_VALUE(0); continue; }
        while (*p) TACTICAL_HASH_VALUE((unsigned char)*p++);
        TACTICAL_HASH_VALUE(0xFF);
    }
#undef TACTICAL_HASH_VALUE
    return h;
}

static uint64_t TacticalEventTopology(
    const Runtime::PartyCombatSnapshot& snapshot)
{
    // Unlike strategic hold memory, a tactical event owns exact live bodies.
    // Any party record/body substitution must end that event and its lease.
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = snapshot.member[i];
        h ^= (uint64_t)m.slot; h *= 1099511628211ULL;
        h ^= (uint64_t)m.pawnRecordIdx; h *= 1099511628211ULL;
        h ^= (uint64_t)m.record; h *= 1099511628211ULL;
        h ^= (uint64_t)m.body; h *= 1099511628211ULL;
    }
    return h;
}

static bool ExactPartyIdentity(const Runtime::PartyCombatSnapshot& snapshot,
                               const char** reasonOut);

// 85.32: след решения «вес вместо старшинства». Печатается один раз на смену
// решения, ровно когда близкое событие действительно обошло старшее, — иначе
// проверить в поле нечего.
static uint64_t s_lastWeightLineSignature = 0;
static DWORD    s_lastWeightLineMs = 0;

static DWORD    s_lastFallenLineMs = 0;
static uint64_t s_lastFallenSignature = 0;
static DWORD    s_lastFinishLineMs = 0;
static uint64_t s_lastFinishSignature = 0;

static void ReportOneBlocker(const MonsterAI::TacticalFallenDiag& d,
                             const Runtime::PartyCombatSnapshot& fresh,
                             const MonsterAI::TacticalPartyActor* party,
                             int nParty, DWORD now, DWORD& lastMs,
                             uint64_t& lastSignature);

// 85.29: одна строка «почему встреча не состоялась».
//
// Печатается ТОЛЬКО когда тема реально есть: пешка лежит (по вердикту
// наблюдателя или по акту) либо происходит что-то подозрительное — акт похож на
// падение, но в наш список не попал, или тело есть, а акт не читается. Иначе в
// лог полез бы мусор из каждого боя.
static bool ActLooksLikeFall(const char* act)
{
    // Только для телеметрии: если игровой акт падения назван иначе, чем в наших
    // списках, строка BLOCKED покажет его настоящее имя. На поведение не влияет.
    if (!act || !act[0]) return false;
    return strstr(act, "Down") != 0 || strstr(act, "Dead") != 0
        || strstr(act, "Crumble") != 0 || strstr(act, "Cmc") != 0;
}

static void ReportFallenGuardBlocker(const MonsterAI::TacticalScan& scan,
                                     const Runtime::PartyCombatSnapshot& fresh,
                                     const MonsterAI::TacticalPartyActor* party,
                                     int nParty, DWORD now)
{
    if (scan.matched) return;                        // событие состоялось
    // 85.30: два механизма — две строки. Каждый объясняется своим каналом, и
    // «молчит именно добивание» отличается от «молчит именно встреча».
    ReportOneBlocker(scan.fallen, fresh, party, nParty, now, s_lastFallenLineMs,
                     s_lastFallenSignature);
    ReportOneBlocker(scan.finish, fresh, party, nParty, now, s_lastFinishLineMs,
                     s_lastFinishSignature);
}

static void ReportOneBlocker(const MonsterAI::TacticalFallenDiag& d,
                             const Runtime::PartyCombatSnapshot& fresh,
                             const MonsterAI::TacticalPartyActor* party,
                             int nParty, DWORD now, DWORD& lastMs,
                             uint64_t& lastSignature)
{
    if (!d.reason) return;                           // замечаний нет
    if (!strcmp(d.reason, "mechanism-off")) return;  // ключ 0 — об этом сказано в баннере

    bool interesting = d.pawnDowned;
    if (!interesting) {
        for (int p = 0; p < nParty; ++p) {
            if (party[p].downedValid || party[p].downedRevivable) {
                interesting = true;
                break;
            }
            if (ActLooksLikeFall(party[p].act)) { interesting = true; break; }
        }
    }
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS && !interesting; ++i) {
        const Runtime::PartyCombatMember& m = fresh.member[i];
        if (m.recordValid && m.bodyValid && !m.actionValid) interesting = true;
    }
    if (!interesting) return;

    uint64_t sig = 1469598103934665603ULL;
    sig ^= (uint64_t)(uintptr_t)d.reason;
    sig *= 1099511628211ULL;
    sig ^= (uint64_t)d.pawnBody;
    sig *= 1099511628211ULL;
    sig ^= (uint64_t)(int)(d.approachM * 10.0f);
    sig *= 1099511628211ULL;
    sig ^= (uint64_t)d.monstersOfKind;
    if (sig == lastSignature && now - lastMs < kFallenDiagRepeatMs) return;
    lastSignature = sig;
    lastMs = now;

    logFile << "Monster Director: situation BLOCKED name="
            << MonsterAI::TacticalSituationName(d.situation)
            << " reason="
            << d.reason
            << " radius=" << s_fallenGuardRadius << "m"
            // Отрицательное число = «нечего было измерять» (у добивания радиус
            // игрока не участвует вовсе) — печатаем n/a, чтобы «-1m» не читалось
            // как настоящая дистанция.
            << " approach="
            << (d.approachM >= 0.0f ? d.approachM : -1.0f) << "m"
            << " nearestMob="
            << (d.nearestKindM >= 0.0f ? d.nearestKindM : -1.0f) << "m"
            << " monstersOfKind=" << d.monstersOfKind << "/" << d.monstersTotal
            << " pawn="
            << (d.pawnSlot >= 0 && d.pawnSlot < Runtime::PARTY_COMBAT_SLOTS
                ? Runtime::PartyCombatSlotName(d.pawnSlot) : "none")
            << " pawnAct=" << (d.pawnAct ? d.pawnAct : "?")
            << " pawnDowned=" << (d.pawnDowned ? 1 : 0)
            << " pawnPosValid=" << (d.pawnPosValid ? 1 : 0)
            << " arisenPosValid=" << (d.arisenPosValid ? 1 : 0)
            << std::hex << " pawnBody=0x" << d.pawnBody << std::dec
            << " rawPos{pawn=" << (int)d.pawnX << "," << (int)d.pawnY << ","
            << (int)d.pawnZ << " arisen=" << (int)d.arisenX << ","
            << (int)d.arisenY << "," << (int)d.arisenZ << "}"
            << " units=cm" << std::endl;

    // Что видно про партию в этот такт. Когда причина «пешки не видно» или акт
    // не совпал с нашим списком — это единственный способ узнать настоящее имя
    // акта падения.
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = fresh.member[i];
        logFile << "Monster Director: situation BLOCKED-member slot="
                << Runtime::PartyCombatSlotName(i)
                << " rec=" << (m.recordValid ? 1 : 0)
                << " body=" << (m.bodyValid ? 1 : 0)
                << " pos=" << (m.positionValid ? 1 : 0)
                << " action=" << (m.actionValid ? 1 : 0)
                << " downed=" << (m.downedValid ? 1 : 0)
                << " revivable=" << (m.downedRevivable ? 1 : 0)
                << " act=" << (m.actionValid ? m.liveAct : "?")
                << std::endl;
    }
}

// 85.33: КТО КУДА ИДЁТ. Математика распределения — ровно слова владельца:
// «те, кто ближе к тому или иному событию, — для них это событие важнее».
//
// Для каждой особи считаем расстояние до «якоря» каждого одновременного
// события (якорь — тот член партии, к которому событие тянет: лежащая пешка у
// добивания, игрок у встречи, слышащий у рога) и отдаём особь БЛИЖАЙШЕМУ
// событию своего вида. Дальше внутри события список сортируется по близости и
// режется лимитом владельца (responderMax): ближе — значит успеет первым.
//
// Если событие одно, функция не вызывается вовсе: главный приказ идёт прежним
// путём, без единой новой ветки.
static void ComputeParallelSplit(const MonsterAI::TacticalScan& scan,
                                 const MonsterAI::TacticalPartyActor* party,
                                 int nParty)
{
    const bool hadParallel = s_parallel.active;
    memset(&s_parallel, 0, sizeof(s_parallel));
    s_parallel.targetSlot = -1;

    const MonsterAI::TacticalAltEvent* alt = (scan.altCount > 0) ? &scan.alts[0] : 0;
    if (!alt) {
        // Деление кончилось. Один раз сообщаем — иначе непонятно, почему вторая
        // задача пропала из лога.
        if (hadParallel) {
            logFile << "Monster Director: situation SPLIT clear reason=no-second-event"
                    << std::endl;
            s_lastSplitLogSignature = 0;
        }
        return;
    }

    // Якоря обеих сторон.
    const MonsterAI::TacticalPartyActor* anchors[2] = { 0, 0 };
    for (int p = 0; p < nParty; ++p) {
        if (party[p].body == scan.match.targetBody) anchors[0] = &party[p];
        if (party[p].body == alt->targetBody) anchors[1] = &party[p];
    }
    if (!anchors[0] || !anchors[0]->positionValid) return;
    if (!anchors[1] || !anchors[1]->positionValid) return;

    const char* kinds[2] = { scan.match.responderKind, alt->responderKind };
    float primaryD[kMaxViews];
    float secondD[kMaxViews];
    int   nPrimary = 0;
    int   nSecond = 0;
    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        if (!v.body || v.dead || !v.positionValid) continue;
        float d[2] = { -1.0f, -1.0f };
        bool eligible[2] = { false, false };
        for (int e = 0; e < 2; ++e) {
            if (!kinds[e] || strcmp(v.kind, kinds[e]) != 0) continue;
            const float dx = v.x - anchors[e]->x;
            const float dy = v.y - anchors[e]->y;
            const float dz = v.z - anchors[e]->z;
            d[e] = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
            eligible[e] = true;
        }
        if (!eligible[0] && !eligible[1]) continue;
        // Ближе — значит этот приказ и получит.
        int owner = 0;
        if (eligible[0] && eligible[1]) owner = (d[1] < d[0]) ? 1 : 0;
        else owner = eligible[1] ? 1 : 0;
        if (owner == 0) {
            if (nPrimary < kMaxViews) { s_parallel.primary[nPrimary] = v.body; primaryD[nPrimary] = d[0]; ++nPrimary; }
        } else {
            if (nSecond < kMaxViews) { s_parallel.responders[nSecond] = v.body; secondD[nSecond] = d[1]; ++nSecond; }
        }
    }

    // Внутри события — по близости; лимит владельца режет хвост.
    for (int pass = 0; pass < 2; ++pass) {
        uintptr_t* list = (pass == 0) ? s_parallel.primary : s_parallel.responders;
        float*     dist = (pass == 0) ? primaryD : secondD;
        int        n    = (pass == 0) ? nPrimary : nSecond;
        for (int i = 1; i < n; ++i) {
            const uintptr_t b = list[i];
            const float     dd = dist[i];
            int j = i - 1;
            while (j >= 0 && dist[j] > dd) {
                list[j + 1] = list[j];
                dist[j + 1] = dist[j];
                --j;
            }
            list[j + 1] = b;
            dist[j + 1] = dd;
        }
        int keep = n;
        if (s_policyResponderMax > 0 && s_policyResponderMax < keep)
            keep = s_policyResponderMax;
        if (keep > kMaxPolicyWolves) keep = kMaxPolicyWolves;
        if (pass == 0) nPrimary = keep; else nSecond = keep;
    }

    s_parallel.nPrimary = nPrimary;
    s_parallel.nResponder = nSecond;
    s_parallel.active = (nSecond > 0);
    s_parallel.situation = alt->situation;
    s_parallel.targetSlot = alt->targetSlot;
    s_parallel.targetBody = alt->targetBody;
    s_parallel.responderKind = alt->responderKind;
    s_parallel.response = alt->response;
    s_parallel.urgency = alt->urgency;
    s_parallel.score = alt->score;
    s_parallel.pairDistanceM = alt->pairDistanceM;
    s_parallel.signature = 1u;

    uint32_t sig = 2166136261u;
    sig = (sig ^ (uint32_t)scan.situation) * 16777619u;
    sig = (sig ^ (uint32_t)alt->situation) * 16777619u;
    sig = (sig ^ (uint32_t)nPrimary) * 16777619u;
    sig = (sig ^ (uint32_t)nSecond) * 16777619u;
    sig = (sig ^ (uint32_t)(alt->targetBody & 0xFFFFu)) * 16777619u;
    // Ключ входит в подпись: строка обязана появиться заново, когда владелец
    // включил исполнение. Иначе он включит ключ и не увидит в логе ничего.
    sig = (sig ^ (s_parallelOrders ? 1u : 2u)) * 16777619u;
    const DWORD nowMs = GetTickCount();
    if (sig != s_lastSplitLogSignature || nowMs - s_lastSplitLogMs >= 3000) {
        s_lastSplitLogSignature = sig;
        s_lastSplitLogMs = nowMs;
        logFile << "Monster Director: situation SPLIT primary="
                << TacticalSituationName(scan.situation)
                << "(" << nPrimary << " of " << (nPrimary + nSecond) << ")"
                << " secondary=" << TacticalSituationName(alt->situation)
                << "(" << nSecond << ")"
                << " secondaryTarget="
                << Runtime::PartyCombatSlotName(alt->targetSlot)
                << " secondaryDist=" << alt->pairDistanceM << "m"
                << " rule=closest-to-its-own-event"
                << " actuate=" << (s_parallelOrders ? 1 : 0)
                << (s_parallelOrders ? ""
                    : " (лог; ключ parallelOrders = 1 включает исполнение)")
                << std::endl;
    }
}

static void UpdateTacticalSituations(DWORD now)
{
    Runtime::PartyCombatSnapshot fresh;
    memset(&fresh, 0, sizeof(fresh));
    if (!Runtime::ReadPartyCombatSnapshot(&fresh)) {
        TacticalRelease("party-snapshot-unavailable", false, true, now);
        s_tactical.partialLogged = false;
        s_tactical.partialSignature = 0;
        memset(&s_cueParty, 0, sizeof(s_cueParty));
        return;
    }
    s_cueParty = fresh;

    // Observation and writes share the same occupied-exact admission. Do not
    // report a tactical event as proven when a required party/body identity
    // is incomplete or ambiguous, even though the downstream actuator
    // repeats this gate. Empty hired slots are not a missing party.
    const char* identityReason = 0;
    if (!ExactPartyIdentity(fresh, &identityReason)) {
        TacticalRelease(identityReason ? identityReason
                                       : "identity-snapshot-unavailable",
                        false, true, now);
        s_tactical.partialLogged = false;
        s_tactical.partialSignature = 0;
        return;
    }
    const uint64_t eventTopology = TacticalEventTopology(fresh);

    TacticalPartyActor party[Runtime::PARTY_COMBAT_SLOTS];
    int nParty = 0;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = fresh.member[i];
        if (!m.recordValid || !m.bodyValid || !m.body || !m.actionValid) continue;
        TacticalPartyActor& a = party[nParty++];
        a.slot = i;
        a.body = m.body;
        a.act = m.liveAct;
        a.positionValid = m.positionValid;
        a.x = m.x; a.y = m.y; a.z = m.z;
        a.vocation = m.vocation;
        // 85.29: вердикт наблюдателя PartyStatus. Имена актов падения уже один
        // раз подвели (в списке кандидатов не было реального акта поля), поэтому
        // состояние берём оттуда, где оно подтверждено живым логом.
        a.downedValid = m.downedValid;
        a.downedRevivable = m.downedRevivable;
        // 85.30: вид падения — разные адресаты (добивать пешку / встречать игрока).
        a.downedAwake = m.downedAwake;
    }

    TacticalMonsterActor monsters[kMaxViews];
    int nMonster = 0;
    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        if (!v.body || v.dead || !v.act[0]) continue;
        TacticalMonsterActor& a = monsters[nMonster++];
        a.body = v.body;
        a.kind = v.kind;
        a.act = v.act;
        a.positionValid = v.positionValid;
        a.x = v.x; a.y = v.y; a.z = v.z;
    }

    TacticalScan scan;
    ScanTacticalSituations(party, nParty, monsters, nMonster, &scan);
    s_tactical.fallen = scan.fallen;
    s_tactical.finish = scan.finish;
    // 85.33: одновременные события и деление пачки. Считается всегда (лог), а
    // исполняется только по ключу — проверяемому механизму ничего не грозит.
    ComputeParallelSplit(scan, party, nParty);
    const bool anyEvidence = scan.targetCandidates > 0
                          || scan.evidenceCandidates > 0;

    // 85.29: встреча у тела больше не молчит. Если лежащая пешка есть, а
    // события нет — печатаем причину и числа, не чаще одной строки на
    // изменение состояния. Дважды подряд механизм молчал в поле, и оба раза по
    // логу нельзя было понять, чего именно не хватило.
    ReportFallenGuardBlocker(scan, fresh, party, nParty, now);

    // Strict unique spatial admission is paid once. While the same exact pair
    // remains on the same table recipe, unrelated wolves and noisy coordinates
    // cannot steal/release it. Missing identity/topology still fails hard.
    if (s_tactical.active) {
        if (s_tactical.topologySignature != eventTopology) {
            TacticalRelease("party-topology-changed", false, true, now);
        } else if (s_tactical.targetSlot < 0
                   || s_tactical.targetSlot >= Runtime::PARTY_COMBAT_SLOTS
                   || !fresh.member[s_tactical.targetSlot].actionValid) {
            TacticalRelease("holder-action-state-unavailable", false, true, now);
        } else {
            TacticalContinuation continuation;
            InspectTacticalContinuation(s_tactical.situation,
                                        s_tactical.targetBody,
                                        s_tactical.victimBody,
                                        s_tactical.responderKind,
                                        party, nParty, monsters, nMonster,
                                        &continuation);
            if (continuation.targetActionMatched
                && continuation.evidenceKindMatched
                && continuation.evidenceActionMatched) {
                if (continuation.distanceValid)
                    s_tactical.pairDistanceM = continuation.distanceM;
                s_tactical.partialLogged = false;
                s_tactical.partialSignature = 0;
                if (now - s_tactical.sinceMs >= s_tactical.maxLeaseMs) {
                    logFile << "Monster Director: situation TIMEOUT name="
                            << TacticalSituationName(s_tactical.situation)
                            << " response=" << TacticalResponseName(s_tactical.response)
                            << " urgency=" << s_tactical.urgency
                            << " target="
                            << Runtime::PartyCombatSlotName(s_tactical.targetSlot)
                            << " victim=0x" << std::hex << s_tactical.victimBody
                            << std::dec << " max=" << s_tactical.maxLeaseMs
                            << "ms no-rearm-until-clear" << std::endl;
                    TacticalRelease("hard-timeout", true, true, now);
                }
                return;
            }

            if (!continuation.targetBodyPresent)
                TacticalRelease("holder-identity-lost", false, true, now);
            else if (!continuation.targetActionMatched)
                TacticalRelease("holder-action-ended", false, false, now);
            else if (!continuation.evidenceBodyPresent)
                TacticalRelease("victim-topology-lost", false, true, now);
            else if (!continuation.evidenceKindMatched)
                TacticalRelease("victim-species-changed", false, true, now);
            else
                TacticalRelease("victim-action-ended", false, false, now);
        }
    }

    if (!scan.matched) {
        // Timeout blocks only the same continuously correlated event. If
        // either side or spatial overlap clears, a later pair is a new event.
        if (s_tactical.timeoutBlocked
            && (scan.targetCandidates == 0 || scan.evidenceCandidates == 0
                || scan.pairCandidates == 0))
            s_tactical.timeoutBlocked = false;
        if (!anyEvidence) {
            s_tactical.partialLogged = false;
            s_tactical.partialSignature = 0;
            return;
        }
        const uint64_t partialSignature = TacticalPartialSignature(scan);
        if (!s_tactical.partialLogged
            || s_tactical.partialSignature != partialSignature) {
            const char* partialReason = "pair-not-unique";
            if (scan.targetCandidates == 0) partialReason = "holder-action-absent";
            else if (scan.targetCandidates > 1) partialReason = "holders-ambiguous";
            else if (scan.pairCandidates == 0) partialReason = "no-spatial-pair";
            else if (scan.pairCandidates > 1) partialReason = "pairs-ambiguous";
            logFile << "Monster Director: situation PARTIAL name=" << scan.name
                    << " response=" << TacticalResponseName(scan.response)
                    << " reason=" << partialReason
                    << " holders=" << scan.targetCandidates
                    << " victims=" << scan.evidenceCandidates
                    << " pairs=" << scan.pairCandidates
                    << " positionRejected=" << scan.positionRejected
                    << " holder="
                    << (scan.firstTargetSlot >= 0
                        ? Runtime::PartyCombatSlotName(scan.firstTargetSlot) : "none")
                    << " holderAct=" << (scan.firstTargetAct ? scan.firstTargetAct : "?")
                    << " victim=0x" << std::hex << scan.firstEvidenceBody << std::dec
                    << " victimAct=" << (scan.firstEvidenceAct
                                          ? scan.firstEvidenceAct : "?")
                    << " nearest=" << scan.nearestDistanceM << "m"
                    << " no-write" << std::endl;
            s_tactical.partialLogged = true;
            s_tactical.partialSignature = partialSignature;
        }
        return;
    }

    const TacticalMatch& m = scan.match;
    const bool samePair = s_tactical.situation == m.situation
                       && s_tactical.response == m.response
                       && s_tactical.targetSlot == m.targetSlot
                       && s_tactical.targetBody == m.targetBody
                       && s_tactical.victimBody == m.evidenceBody
                       && s_tactical.topologySignature == eventTopology;

    if (s_tactical.timeoutBlocked && samePair) return;
    if (s_tactical.timeoutBlocked && !samePair)
        s_tactical.timeoutBlocked = false;

    // 85.32: событие выбрано ПО ВЕСУ (ранг × близость), а не по старшинству.
    // Владелец должен видеть это решение целиком: кто победил, кого обошёл и с
    // какими числами. Печатаем только когда выбор ДЕЙСТВИТЕЛЬНО сменил старшего.
    if (scan.outrankedSituation != TACTICAL_SITUATION_NONE) {
        uint64_t sig = (uint64_t)scan.situation;
        sig = sig * 1099511628211ULL + (uint64_t)scan.outrankedSituation;
        sig = sig * 1099511628211ULL
            + (uint64_t)(int)(scan.chosenScore * 10.0f);
        sig = sig * 1099511628211ULL
            + (uint64_t)(int)(scan.outrankedScore * 10.0f);
        if (sig != s_lastWeightLineSignature
            || now - s_lastWeightLineMs >= kFallenDiagRepeatMs) {
            s_lastWeightLineSignature = sig;
            s_lastWeightLineMs = now;
            logFile << "Monster Director: situation BY-WEIGHT name="
                    << TacticalSituationName(scan.situation)
                    << " score=" << scan.chosenScore
                    << " distance=" << scan.chosenDistanceM << "m"
                    << " outranked="
                    << TacticalSituationName(scan.outrankedSituation)
                    << " itsScore=" << scan.outrankedScore
                    << " itsDistance=" << scan.outrankedDistanceM << "m"
                    << " reason=closer-to-its-own-event" << std::endl;
        }
    }
    if (!s_tactical.active)
        TacticalEnter(m, now, eventTopology);
}

static uint64_t HashAdd(uint64_t h, uint64_t value)
{
    h ^= value;
    return h * 1099511628211ULL;
}

static uint64_t PartyTopologySignature(const Runtime::PartyCombatSnapshot& p)
{
    // Build 002 treated level/maxHP/core/loadout changes as party composition.
    // A real level-up therefore reset the hold. Build 003+ hashes topology only:
    // slot presence, record address and stable record index. Current HP, max HP,
    // level, stats, skills, body POINTER and position cannot reset tactical memory.
    //
    // 84.23: occupancy (on-field vs rifted) IS topology. A pawn leaving for
    // the Rift or returning is a new encounter-local party. Hash the boolean,
    // never the body address (zone-load flicker of the pointer itself).
    //
    // Exact occupant identity is still unvalidated. That is acceptable for an
    // encounter-local observer: party hiring normally occurs outside a live
    // wolf encounter, and the mark is cleared when the pack disappears.
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = p.member[i];
        h = HashAdd(h, (uint64_t)(m.recordValid ? 1 : 0));
        if (!m.recordValid) continue;
        h = HashAdd(h, (uint64_t)m.slot);
        h = HashAdd(h, (uint64_t)m.pawnRecordIdx);
        h = HashAdd(h, (uint64_t)m.record);
        h = HashAdd(h, (uint64_t)((m.bodyValid && m.body) ? 1 : 0));
    }
    return h;
}

static void ResetDecisionMemory(const char* reason)
{
    s_mark = -1;
    s_runner = -1;
    s_markSince = 0;
    s_mode = RECOMMEND_NONE;
    lstrcpynA(s_reason, reason ? reason : "reset", sizeof(s_reason));
}

static void LogPartyRaw(const char* event)
{
    logFile << "MD: PARTY " << (event ? event : "snapshot")
            << " policy=MOMENT-HP records=" << s_party.recordCount
            << " body/position=NATIVE-MAP-UNVALIDATED/ignored"
            << " core/loadout/skills/vocation/status/downed=ignored"
            << std::endl;

    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = s_party.member[i];
        char line[640];
        sprintf_s(line,
            "MD: raw %-8s rec=%d hpValid=%d hp=%.1f maxHp=%.1f(diag-only) "
            "body=%d pos=%d action=%d nativeIdentity=UNVALIDATED "
            "CORE/UNVALIDATED str=%.1f def=%.1f mag=%.1f mdef=%.1f "
            "loadoutTotals=UNKNOWN allNonHpInputs=ignored topologyOnlyReset=1",
            Runtime::PartyCombatSlotName(i), m.recordValid ? 1 : 0,
            m.hpValid ? 1 : 0, m.currentHp, m.maxHp,
            m.bodyValid ? 1 : 0, m.positionValid ? 1 : 0,
            m.actionValid ? 1 : 0, m.strength, m.defense,
            m.magick, m.magickDefense);
        logFile << line << std::endl;
    }
}

static void ScoreParty()
{
    memset(s_score, 0, sizeof(s_score));
    s_wolfCount = 0;
    s_hobCount = 0;
    s_saurCount = 0;
    for (int i = 0; i < s_nView; ++i) {
        if (IsWolf(s_view[i])) ++s_wolfCount;
        if (IsHob(s_view[i])) ++s_hobCount;
        if (IsSaur(s_view[i])) ++s_saurCount;
    }

    float highestHp = 0.0f;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = s_party.member[i];
        TargetScore& q = s_score[i];
        // On-field only. Запись в Разломе жива, но охотить её нельзя:
        // тела нет. Downed на земле — bodyValid, слот остаётся в скоре.
        q.hpValid = m.recordValid && m.hpValid && m.currentHp > 0.0f
                 && m.bodyValid && m.body;
        if (q.hpValid && m.currentHp > highestHp)
            highestHp = m.currentHp;
    }

    if (highestHp <= 0.0f) return;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        const Runtime::PartyCombatMember& m = s_party.member[i];
        TargetScore& q = s_score[i];
        if (!q.hpValid) continue;

        // Both values are derived from ABSOLUTE current HP. maxHp is absent.
        q.lowAbsoluteHp = Clamp01(1.0f - m.currentHp / highestHp);
        q.huntScore = highestHp / m.currentHp;
        q.valid = true;
    }
}

static void ClearPriorityOrder()
{
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) s_order[i] = -1;
}

static void BuildPriorityOrder()
{
    ClearPriorityOrder();
    bool used[Runtime::PARTY_COMBAT_SLOTS] = {};
    for (int rank = 0; rank < Runtime::PARTY_COMBAT_SLOTS; ++rank) {
        int best = -1;
        for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
            if (used[i] || !s_score[i].valid) continue;
            if (best < 0 || s_score[i].huntScore > s_score[best].huntScore)
                best = i;
        }
        if (best < 0) break;
        used[best] = true;
        s_order[rank] = best;
    }
}

static int PriorityRankOf(int slot)
{
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i)
        if (s_order[i] == slot) return i + 1;
    return 0;
}

static const char* PriorityCode(int rank)
{
    if (rank < 0 || rank >= Runtime::PARTY_COMBAT_SLOTS) return "-";
    const int slot = s_order[rank];
    if (slot == Runtime::PARTY_ARISEN) return "A";
    if (slot == Runtime::PARTY_MAIN) return "M";
    if (slot == Runtime::PARTY_HIRED1) return "H1";
    if (slot == Runtime::PARTY_HIRED2) return "H2";
    return "-";
}

static int BestScoreExcept(int except)
{
    int best = -1;
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
        if (i == except || !s_score[i].valid) continue;
        if (best < 0 || s_score[i].huntScore > s_score[best].huntScore)
            best = i;
    }
    return best;
}

static float IsolationFor(int mark, int runner)
{
    if (mark < 0 || !s_score[mark].valid) return 0.0f;
    if (runner < 0 || !s_score[runner].valid) return 0.0f;
    const float r = s_score[runner].huntScore;
    if (r <= 0.0001f) return 0.0f;
    // Algebraically runnerCurrentHp / markCurrentHp - 1. A negative result
    // is meaningful: hysteresis is holding a mark that is no longer raw #1.
    return (s_score[mark].huntScore - r) / r;
}

static float DepthFor(int mark)
{
    if (mark < 0 || !s_score[mark].valid) return 0.0f;
    // huntScore already is highestCurrentHp / markCurrentHp. Reuse it rather
    // than inventing another score or another memory read.
    const float depth = s_score[mark].huntScore - 1.0f;
    return depth > 0.0f ? depth : 0.0f;
}

static const char* HpText(int slot, char* out, int cap)
{
    if (!out || cap <= 0) return "?";
    if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS
        || !s_score[slot].valid) {
        lstrcpynA(out, "?", cap);
        return out;
    }
    sprintf_s(out, cap, "%.1f", s_party.member[slot].currentHp);
    return out;
}

static void LogScoreLine(int slot, const char* label)
{
    if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS) {
        logFile << "MD: " << label << "=none" << std::endl;
        return;
    }

    const Runtime::PartyCombatMember& m = s_party.member[slot];
    const TargetScore& q = s_score[slot];
    char line[640];
    sprintf_s(line,
        "MD: %-6s %-8s policy=MOMENT-HP rank=%d eligible=%d rec=%d hpValid=%d "
        "hpAbs=%.1f score=%.3f lowHpNorm=%.3f maxHp=%.1f(diag-only) "
        "body=%d pos=%d(ignored) CORE/UNVALIDATED def=%.1f mdef=%.1f(ignored) "
        "nativeNamed=UNVALIDATED actuator=%s observerOnly=%d writes=%d",
        label, Runtime::PartyCombatSlotName(slot), PriorityRankOf(slot),
        q.valid ? 1 : 0, m.recordValid ? 1 : 0, m.hpValid ? 1 : 0, m.currentHp,
        q.huntScore, q.lowAbsoluteHp, m.maxHp, m.bodyValid ? 1 : 0,
        m.positionValid ? 1 : 0, m.defense, m.magickDefense,
        s_actuatorEnabled ? "ON" : "OFF", s_actuatorEnabled ? 0 : 1,
        s_gameplayWrites);
    logFile << line << std::endl;
}

static void LogDecision(const char* event, DWORD now, bool withScores)
{
    const float isolation = IsolationFor(s_mark, s_runner);
    const float depth = DepthFor(s_mark);
    char markHp[32], runnerHp[32];
    char line[1180];
    sprintf_s(line,
        "MD: %s policy=MOMENT-HP mark=%s runner=%s rawPriority=[%s>%s>%s>%s] "
        "markHp=%s runnerHp=%s isolation=%+.1f%% targetDepth=%+.1f%% "
        "focusIntent=%s reason=%s "
        "hold=%lu/%lu ms decision=%lu ms switchMargin=20%% isolationBias=20%% isolationFocus=100%% "
        "wolves=%d hobs=%d saurs=%d hp=[A:%.1f,M:%.1f,H1:%.1f,H2:%.1f] "
        "eligible=[%d,%d,%d,%d] body/position/native=UNVALIDATED/ignored "
        "DEF/ATK/maxHP%%/skills/vocation/status/downed=ignored "
        "focus/tempo=synchronized-if-gated actuator=%s observerOnly=%d writes=%d",
        event ? event : "DECISION",
        s_mark >= 0 ? Runtime::PartyCombatSlotName(s_mark) : "none",
        s_runner >= 0 ? Runtime::PartyCombatSlotName(s_runner) : "none",
        PriorityCode(0), PriorityCode(1), PriorityCode(2), PriorityCode(3),
        HpText(s_mark, markHp, sizeof(markHp)),
        HpText(s_runner, runnerHp, sizeof(runnerHp)),
        isolation * 100.0f, depth * 100.0f, ModeName(s_mode), s_reason,
        (unsigned long)(s_markSince ? now - s_markSince : 0),
        (unsigned long)kMinHoldMs, (unsigned long)kDecisionMs,
        s_wolfCount, s_hobCount, s_saurCount,
        s_party.member[0].currentHp, s_party.member[1].currentHp,
        s_party.member[2].currentHp, s_party.member[3].currentHp,
        s_score[0].valid ? 1 : 0, s_score[1].valid ? 1 : 0,
        s_score[2].valid ? 1 : 0, s_score[3].valid ? 1 : 0,
        s_actuatorEnabled ? "ON" : "OFF", s_actuatorEnabled ? 0 : 1,
        s_gameplayWrites);
    logFile << line << std::endl;
    if (withScores) {
        LogScoreLine(s_mark, "mark");
        LogScoreLine(s_runner, "runner");
    }
    s_lastLog = now;
    s_lastLoggedMode = s_mode;
}

static void UpdateStatus(DWORD now)
{
    const char* actuator = s_actuatorEnabled ? "ON" : "OFF";
    const char* situation = s_tactical.active
        ? TacticalSituationName(s_tactical.situation)
        : (s_tactical.timeoutBlocked ? "timeout-blocked" : "-");
    if (!strcmp(s_reason, "party-unavailable")) {
        sprintf_s(s_status,
            "Monster Director: PackMark+tactics | party records unavailable | "
            "situation %s | actuator %s | policy %s | writes %d",
            situation, actuator, s_policyStatus, s_gameplayWrites);
        return;
    }
    if (s_wolfCount <= 0 && s_hobCount <= 0 && s_saurCount <= 0) {
        sprintf_s(s_status,
            "Monster Director: PackMark+tactics | no pack (wolf/hob) | "
            "situation %s | actuator %s | policy %s | writes %d",
            situation, actuator, s_policyStatus, s_gameplayWrites);
        return;
    }
    if (s_mark < 0) {
        sprintf_s(s_status,
            "Monster Director: PackMark+tactics | no eligible positive-current-HP record | "
            "%d wolves %d hobs %d saurs | situation %s | actuator %s | policy %s | writes %d",
            s_wolfCount, s_hobCount, s_saurCount, situation, actuator, s_policyStatus, s_gameplayWrites);
        return;
    }

    char runnerHp[32];
    const float isolation = IsolationFor(s_mark, s_runner) * 100.0f;
    const float depth = DepthFor(s_mark) * 100.0f;
    sprintf_s(s_status,
        "Monster Director: PackMark+tactics | PackMark %s HP %.1f | runner %s HP %s | "
        "isolation %+.1f%% | depth %+.1f%% | focus %s (%s) | "
        "raw %s>%s>%s>%s | hold %.1f/2.5 s | situation %s | "
        "actuator %s | policy %s | writes %d",
        Runtime::PartyCombatSlotName(s_mark), s_party.member[s_mark].currentHp,
        s_runner >= 0 ? Runtime::PartyCombatSlotName(s_runner) : "none",
        HpText(s_runner, runnerHp, sizeof(runnerHp)), isolation, depth,
        ModeName(s_mode), s_reason,
        PriorityCode(0), PriorityCode(1), PriorityCode(2), PriorityCode(3),
        s_markSince ? (now - s_markSince) / 1000.0f : 0.0f,
        situation, actuator, s_policyStatus, s_gameplayWrites);
}

static void Decide(DWORD now)
{
    if (!Runtime::ReadPartyCombatSnapshot(&s_party)) {
        memset(&s_party, 0, sizeof(s_party));
        memset(s_score, 0, sizeof(s_score));
        ClearPriorityOrder();
        ResetDecisionMemory("party-unavailable");
        s_wolfCount = 0;
        s_hobCount = 0;
        s_saurCount = 0;
        UpdateStatus(now);
        return;
    }

    const uint64_t signature = PartyTopologySignature(s_party);
    if (!s_havePartySignature || signature != s_partySignature) {
        s_partySignature = signature;
        s_havePartySignature = true;
        ResetDecisionMemory("party-topology-reset");
        LogPartyRaw("record-topology-change");
    }

    ScoreParty();
    BuildPriorityOrder();
    if (s_wolfCount <= 0 && s_hobCount <= 0 && s_saurCount <= 0) {
        if (s_mark >= 0) {
            ResetDecisionMemory("wolf-pack-gone");
            LogDecision("CLEAR", now, false);
        }
        UpdateStatus(now);
        return;
    }

    const int rawBest = s_order[0];
    const int oldMark = s_mark;
    if (rawBest < 0) {
        ResetDecisionMemory("no-valid-hp-record");
    } else if (s_mark < 0 || !s_score[s_mark].valid) {
        s_mark = rawBest;
        s_markSince = now;
        lstrcpynA(s_reason, oldMark < 0 ? "initial-lowest-hp" : "current-hp-invalid",
                  sizeof(s_reason));
    } else if (rawBest == s_mark) {
        lstrcpynA(s_reason, "lowest-hp-stable", sizeof(s_reason));
    } else if (now - s_markSince < kMinHoldMs) {
        lstrcpynA(s_reason, "minimum-hold", sizeof(s_reason));
    } else {
        const float current = s_score[s_mark].huntScore;
        const float challenger = s_score[rawBest].huntScore;
        if (challenger >= current * kSwitchMargin) {
            s_mark = rawBest;
            s_markSince = now;
            lstrcpynA(s_reason, "challenger-plus-20pct", sizeof(s_reason));
        } else {
            lstrcpynA(s_reason, "switch-margin-hold", sizeof(s_reason));
        }
    }

    s_runner = BestScoreExcept(s_mark);
    const float isolation = IsolationFor(s_mark, s_runner);
    if (s_mark >= 0 && s_runner >= 0 && isolation >= kFocusIsolation)
        s_mode = RECOMMEND_FOCUS;
    else if (s_mark >= 0 && s_runner >= 0 && isolation >= kBiasIsolation)
        s_mode = RECOMMEND_BIAS;
    else
        s_mode = RECOMMEND_NONE;
    // This is a focus-opportunity band only. DepthFor(s_mark) is a separate
    // continuous output; neither signal has a consumer in this observer build.

    const bool markChanged = oldMark != s_mark;
    const bool modeChanged = s_mode != s_lastLoggedMode;
    if (markChanged) {
        LogDecision(s_mark < 0 ? "CLEAR" : (oldMark < 0 ? "SELECT" : "SWITCH"),
                    now, true);
    } else if (modeChanged) {
        LogDecision("MODE", now, true);
    }

    UpdateStatus(now);
}

static bool InactiveControlReason(const char* reason)
{
    return reason && (!strcmp(reason, "actuator-off")
                   || !strcmp(reason, "actuator-disabled")
                   || !strcmp(reason, "director-disabled")
                   || !strcmp(reason, "shutdown")
                   || !strcmp(reason, "waiting-for-intent"));
}

static void SetPolicyStatus(const char* reason, bool engaged,
                            const char* mobilization = 0)
{
    if (!reason) reason = "unknown";
    const bool changed = strcmp(s_policyStatus, reason) != 0
                      || s_policyEngaged != engaged;

    if (engaged && s_inactiveSafetyLatched) {
        logFile << "Monster Director: policy RECOVERED priorFailClosed="
                << (s_inactiveSafetyReason[0] ? s_inactiveSafetyReason : "unknown")
                << " coalesced=" << s_inactiveSafetySuppressed
                << " target=" << (s_policyTarget >= 0
                                    ? Runtime::PartyCombatSlotName(s_policyTarget)
                                    : "none")
                << " responders=" << s_nResponderWolf
                << " tempoOwned=" << s_nOwnedWolf
                << " writes=" << s_gameplayWrites << std::endl;
        s_inactiveSafetyLatched = false;
        s_inactiveSafetyReason[0] = 0;
        s_inactiveSafetySuppressed = 0;
    }
    if (engaged) s_inactiveResetLatched = false;

    s_policyEngaged = engaged;
    lstrcpynA(s_policyStatus, reason, sizeof(s_policyStatus));
    if (changed) {
        logFile << "Monster Director: policy " << (engaged ? "ENGAGED" : "RELEASED")
                << " reason=" << reason
                << " target=" << (s_policyTarget >= 0
                                    ? Runtime::PartyCombatSlotName(s_policyTarget)
                                    : "none")
                << " targetBody=0x" << std::hex << s_policyTargetBody << std::dec
                << " situation=" << TacticalSituationName(s_policySituation)
                << " response=" << TacticalResponseName(s_policyResponse)
                << " urgency=" << s_policyUrgency
                << " excluded=0x" << std::hex << s_policyExcludedBody << std::dec
                << " responders=" << s_nResponderWolf
                << " tempoOwned=" << s_nOwnedWolf
                << " mobilization=" << (mobilization ? mobilization
                                                       : (engaged ? "HOLD" : "NONE"));
        if (s_nOwnedWolf > 0) {
            logFile << " endpoints{L0=" << s_policyL0Lo << ".." << s_policyL0Hi
                    << ",A0=" << s_policyA0Lo << ".." << s_policyA0Hi
                    << ",L1=" << s_policyL1Lo << ".." << s_policyL1Hi
                    << ",A1=" << s_policyA1Lo << ".." << s_policyA1Hi << "}";
        }
        logFile << " writes=" << s_gameplayWrites << std::endl;
    }
}

static void ClearPolicyOwnershipState()
{
    memset(s_ownedWolf, 0, sizeof(s_ownedWolf));
    s_nOwnedWolf = 0;
    // 85.34: списки второго приказа чистятся вместе с первым — состояние приказа
    // не переживает его самого (оболочки снимет ReleaseSecondaryMobilization).
    memset(s_secOwned, 0, sizeof(s_secOwned));
    s_nSecOwned = 0;
    memset(s_responderWolf, 0, sizeof(s_responderWolf));
    s_nResponderWolf = 0;
    s_policyTarget = -1;
    s_policyTargetBody = 0;
    s_policySituation = TACTICAL_SITUATION_NONE;
    s_policyResponse = TACTICAL_RESPONSE_NONE;
    s_policyUrgency = 0.0f;
    s_policyL0Lo = s_policyL0Hi = 0.0f;
    s_policyA0Lo = s_policyA0Hi = 0.0f;
    s_policyL1Lo = s_policyL1Hi = 0.0f;
    s_policyA1Lo = s_policyA1Hi = 0.0f;
    s_policyExcludedBody = 0;
    s_policyEventTopology = 0;
}

static void ReleasePolicy(const char* reason, bool hardReset = false)
{
    if (!reason) reason = "unknown";
    const bool hadPolicyState = s_policyEngaged || s_nOwnedWolf > 0
                             || s_nResponderWolf > 0 || s_policyTarget >= 0;

    // No actuator ownership means no gameplay transition. Keep the current UI
    // reason, but coalesce ordinary NONE/BIAS churn. Unsafe cleanup executes
    // once per inactive episode, then remains latched until a real engagement
    // recovers. This also clears rows already decaying after an earlier normal
    // completion without repeatedly calling the actuator.
    if (!hadPolicyState) {
        if (hardReset && !s_inactiveResetLatched) {
            Runtime::Tempo::HardResetAllDirectorMobilization();
            Runtime::Aggro::DirectorFocusSet(
                -1, 0, 0, Runtime::Aggro::DIRECTOR_RESPONSE_NONE);
            s_inactiveResetLatched = true;
        }

        if (hardReset && !InactiveControlReason(reason)) {
            if (!s_inactiveSafetyLatched) {
                s_inactiveSafetyLatched = true;
                lstrcpynA(s_inactiveSafetyReason, reason,
                          sizeof(s_inactiveSafetyReason));
                s_inactiveSafetySuppressed = 0;
                logFile << "Monster Director: policy FAIL-CLOSED reason=" << reason
                        << " active=0 responders=0 tempoOwned=0"
                        << " mobilization=HARD-RESET-ONCE"
                        << " writes=" << s_gameplayWrites << std::endl;
            } else {
                ++s_inactiveSafetySuppressed;
            }
        }

        s_policyEngaged = false;
        lstrcpynA(s_policyStatus, reason, sizeof(s_policyStatus));
        ClearPolicyOwnershipState();
        return;
    }

    // Ordinary evidence completion releases only current owners into Tempo's
    // bounded decay. Unsafe release clears every Director-owned row, including
    // rows already decaying from a prior command. Generic overrides are never
    // touched here.
    if (hardReset) {
        Runtime::Tempo::HardResetAllDirectorMobilization();
        s_inactiveResetLatched = true;
    } else {
        for (int i = 0; i < s_nOwnedWolf; ++i)
            if (s_ownedWolf[i])
                Runtime::Tempo::ReleaseDirectorMobilization(s_ownedWolf[i]);
        // A later unsafe transition still has to clear these decaying rows.
        s_inactiveResetLatched = false;
    }
    Runtime::Aggro::DirectorFocusSet(-1, 0, 0,
                                      Runtime::Aggro::DIRECTOR_RESPONSE_NONE);
    // Второй приказ не живёт дольше первого: он выдавался как его дополнение.
    Runtime::Aggro::DirectorSecondaryClear(reason ? reason : "release");
    ReleaseSecondaryMobilization(reason);
    SetPolicyStatus(reason, false, hardReset ? "HARD-RESET" : "DECAY");
    ClearPolicyOwnershipState();
}

static const char* SnapshotSlotFailure(int slot, bool recordMissing)
{
    static const char* kRecord[Runtime::PARTY_COMBAT_SLOTS] = {
        "identity-Arisen-snapshot-record-unavailable",
        "identity-MainPawn-snapshot-record-unavailable",
        "identity-Hired1-snapshot-record-unavailable",
        "identity-Hired2-snapshot-record-unavailable"
    };
    static const char* kBody[Runtime::PARTY_COMBAT_SLOTS] = {
        "identity-Arisen-snapshot-body-unresolved",
        "identity-MainPawn-snapshot-body-unresolved",
        "identity-Hired1-snapshot-body-unresolved",
        "identity-Hired2-snapshot-body-unresolved"
    };
    if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS)
        return "identity-invalid-slot";
    return recordMissing ? kRecord[slot] : kBody[slot];
}

static bool ExactPartyIdentity(const Runtime::PartyCombatSnapshot& snapshot,
                               const char** reasonOut)
{
    uintptr_t seen[Runtime::PARTY_COMBAT_SLOTS] = {};
    int nSeen = 0;
    for (int slot = 0; slot < Runtime::PARTY_COMBAT_SLOTS; ++slot) {
        // Ask Aggro's independently resolved fixed-slot bridge first so the
        // automatic policy log names the unavailable slot, not merely
        // occupied-exact. Empty hired record-unavailable is not a missing
        // party: skip, do not force exact4.
        uintptr_t resolved = 0;
        const char* bridge = Runtime::Aggro::ResolveMemberBodyStatus(slot,
                                                                     &resolved);
        if (!bridge) {
            if (reasonOut) *reasonOut = "identity-bridge-error";
            return false;
        }
        const bool skipEmptyHired =
            (slot == Runtime::PARTY_HIRED1 || slot == Runtime::PARTY_HIRED2)
            && strstr(bridge, "-record-unavailable") != 0;
        // 84.23: запись жива, тел 0 — пешка в Разломе (таймер / обрыв /
        // камень не трогали). Это не дыра identity. Аризен так не скипается.
        const bool skipRifted =
            (slot == Runtime::PARTY_MAIN
             || slot == Runtime::PARTY_HIRED1
             || slot == Runtime::PARTY_HIRED2)
            && strstr(bridge, "-absent") != 0;
        if (skipEmptyHired || skipRifted) continue;
        if (!strstr(bridge, "-exact")) {
            if (reasonOut) *reasonOut = bridge;
            return false;
        }

        const Runtime::PartyCombatMember& m = snapshot.member[slot];
        if (!m.recordValid) {
            if (reasonOut) *reasonOut = SnapshotSlotFailure(slot, true);
            return false;
        }
        if (!m.bodyValid || !m.body) {
            if (reasonOut) *reasonOut = SnapshotSlotFailure(slot, false);
            return false;
        }
        if (resolved != m.body) {
            if (reasonOut) *reasonOut = "identity-slot-body-mismatch";
            return false;
        }
        for (int k = 0; k < nSeen; ++k) {
            if (seen[k] == m.body) {
                if (reasonOut) *reasonOut = "identity-body-not-unique";
                return false;
            }
        }
        seen[nSeen++] = m.body;
    }
    if (reasonOut) *reasonOut = "identity-occupied-exact";
    return true;
}

static const char* PolicyResponderKind(int situation)
{
    if (s_tactical.active && s_tactical.responderKind[0])
        return s_tactical.responderKind;
    if (situation == TACTICAL_SITUATION_GOBLIN_GRAB_ALERT
        || situation == TACTICAL_SITUATION_GOB_HORN_ALERT)
        return "uEm0100";
    if (situation == TACTICAL_SITUATION_HOB_GRAB_ALERT
        || situation == TACTICAL_SITUATION_HOB_HORN_ALERT)
        return "uEm0101";
    if (situation == TACTICAL_SITUATION_WOLF_HOWL_ALERT)
        return "uEm0200";
    if (situation == TACTICAL_SITUATION_SAURIAN_HOWL_ALERT)
        return "uEm0400";
    if (situation == TACTICAL_SITUATION_NONE) {
        if (s_wolfCount > 0) return "uEm0200";
        if (s_hobCount > 0) return "uEm0101";
        if (s_saurCount > 0) return "uEm0400";
    }
    return "uEm0200";
}

// Исполнители приказа. Возвращает их число (0 = никого).
//
// Проблема, которую решает отбор (85.23): порядок обхода s_view — это порядок
// памяти, а не близость. Когда [monsterAI] responderMax ограничивает число
// исполнителей, брать «первые попавшиеся» бессмысленно: смысл приказа
// «выручай собрата» — он у кого-то перед глазами. Поэтому при известном очаге
// (refPos: жертва события, иначе помеченная пешка) список сортируется по
// расстоянию, и лимит режет хвост. Нет очага — порядок прежний, лимит всё
// равно соблюдается, просто выбор произвольный.
static int CollectEligibleResponders(uintptr_t* out, int cap,
                                     uintptr_t excludedBody, const char* kind,
                                     const float* refPos, int want,
                                     const char** reasonOut)
{
    const bool goblin = kind && !strcmp(kind, "uEm0100");
    const bool hob = kind && !strcmp(kind, "uEm0101");
    const bool saur = kind && !strcmp(kind, "uEm0400");
    uintptr_t cand[kMaxViews];
    float     candD[kMaxViews];
    int n = 0;
    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        if (!v.body || v.dead || v.body == excludedBody) continue;
        if (!kind || strcmp(v.kind, kind) != 0) continue;
        for (int k = 0; k < n; ++k) {
            if (cand[k] == v.body) {
                if (reasonOut)
                    *reasonOut = goblin ? "goblin-duplicate-body"
                               : hob ? "hob-duplicate-body"
                               : saur ? "saurian-duplicate-body"
                               : "wolf-pack-duplicate-body";
                return -1;
            }
        }
        cand[n] = v.body;
        candD[n] = -1.0f;
        if (refPos && v.positionValid) {
            const float dx = v.x - refPos[0];
            const float dy = v.y - refPos[1];
            const float dz = v.z - refPos[2];
            candD[n] = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        ++n;
    }
    if (!n) {
        if (reasonOut)
            *reasonOut = goblin ? "goblin-no-free-responder"
                       : hob ? "hob-no-free-responder"
                       : saur ? "saurian-no-free-responder"
                       : "wolf-pack-lost";
        return 0;
    }
    if (refPos) {
        // Вставка: список короткий (<= kMaxViews). Тела без позиции уезжают
        // в хвост — их честнее потерять, чем поставить вперёд ближних.
        for (int i = 1; i < n; ++i) {
            const uintptr_t b = cand[i];
            const float     d = candD[i];
            int j = i - 1;
            while (j >= 0 && ((candD[j] < 0.0f && d >= 0.0f)
                              || (d >= 0.0f && candD[j] > d))) {
                cand[j + 1] = cand[j];
                candD[j + 1] = candD[j];
                --j;
            }
            cand[j + 1] = b;
            candD[j + 1] = d;
        }
    }
    if (n > cap) n = cap;                        // жёсткий предел таблицы Tempo
    if (want > 0 && want < n) n = want;          // лимит владельца
    for (int i = 0; i < n; ++i) out[i] = cand[i];
    if (reasonOut)
        *reasonOut = goblin ? "goblin-eligible"
                   : hob ? "hob-eligible"
                   : saur ? "saurian-eligible"
                   : "wolf-pack-eligible";
    return n;
}

// Очаг приказа: жертва события (монстр, и он есть в нашем обзоре), иначе
// помеченная пешка. Нужен только для ранжирования исполнителей.
static bool PolicyReferencePos(uintptr_t targetBody, uintptr_t victimBody,
                               const Runtime::PartyCombatSnapshot& party,
                               float* out)
{
    if (victimBody) {
        for (int i = 0; i < s_nView; ++i) {
            const MonsterView& v = s_view[i];
            if (v.body != victimBody || !v.positionValid) continue;
            out[0] = v.x; out[1] = v.y; out[2] = v.z;
            return true;
        }
    }
    if (targetBody) {
        for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i) {
            const Runtime::PartyCombatMember& m = party.member[i];
            if (!m.bodyValid || m.body != targetBody || !m.positionValid) continue;
            out[0] = m.x; out[1] = m.y; out[2] = m.z;
            return true;
        }
    }
    return false;
}


static bool SamePack(const uintptr_t* pack, int n,
                         const uintptr_t* owned, int nOwned)
{
    if (n != nOwned) return false;
    for (int i = 0; i < n; ++i) {
        bool found = false;
        for (int k = 0; k < nOwned; ++k)
            if (pack[i] == owned[k]) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

static bool ResolvePolicyIntent(int* targetSlot, uintptr_t* targetBody,
                                uintptr_t* excludedBody, int* situation,
                                int* response, float* urgency,
                                const char** engageReason,
                                const Runtime::PartyCombatSnapshot** snapshot)
{
    if (targetSlot) *targetSlot = -1;
    if (targetBody) *targetBody = 0;
    if (excludedBody) *excludedBody = 0;
    if (situation) *situation = TACTICAL_SITUATION_NONE;
    if (response) *response = TACTICAL_RESPONSE_NONE;
    if (urgency) *urgency = 0.0f;
    if (engageReason) *engageReason = "decision-none";
    if (snapshot) *snapshot = &s_party;

    // Fast tactical interrupts outrank the slow strategic PackMark but never
    // overwrite it. A higher-priority ALARM may replace an active ALERT on the
    // next 150 ms scan; direct ALARM admission does not require prior ALERT.
    if (s_tactical.active) {
        const int slot = s_tactical.targetSlot;
        if (slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS) {
            if (engageReason) *engageReason = "situation-target-invalid";
            return false;
        }
        if (s_tactical.response != TACTICAL_RESPONSE_ALERT
            && s_tactical.response != TACTICAL_RESPONSE_ALARM) {
            if (engageReason) *engageReason = "situation-response-invalid";
            return false;
        }
        if (!(s_tactical.urgency == s_tactical.urgency)
            || s_tactical.urgency <= 0.0f || s_tactical.urgency > 1.0f) {
            if (engageReason) *engageReason = "situation-urgency-invalid";
            return false;
        }
        const Runtime::PartyCombatMember& m = s_cueParty.member[slot];
        if (!m.recordValid || !m.bodyValid || !m.body
            || m.body != s_tactical.targetBody) {
            if (engageReason) *engageReason = "situation-holder-identity-lost";
            return false;
        }
        if (targetSlot) *targetSlot = slot;
        if (targetBody) *targetBody = m.body;
        if (excludedBody)
            *excludedBody = s_tactical.excludeVictim ? s_tactical.victimBody : 0;
        if (situation) *situation = s_tactical.situation;
        if (response) *response = s_tactical.response;
        if (urgency) *urgency = s_tactical.urgency;
        if (engageReason)
            *engageReason = TacticalSituationPolicyReason(s_tactical.situation);
        if (snapshot) *snapshot = &s_cueParty;
        return true;
    }

    if (s_mode != RECOMMEND_FOCUS || s_mark < 0
        || s_mark >= Runtime::PARTY_COMBAT_SLOTS) {
        if (engageReason)
            *engageReason = s_mode == RECOMMEND_BIAS ? "decision-bias"
                                                     : "decision-none";
        return false;
    }
    if (!s_party.member[s_mark].body) {
        if (engageReason) *engageReason = "target-body-unavailable";
        return false;
    }
    if (targetSlot) *targetSlot = s_mark;
    if (targetBody) *targetBody = s_party.member[s_mark].body;
    if (response) *response = TACTICAL_RESPONSE_ALARM;
    if (urgency) *urgency = kEmergencyUrgency;
    if (engageReason) *engageReason = "focus-window-synchronized";
    return true;
}

static bool OrdinaryIntentCompletion(const char* reason)
{
    return reason && (!strcmp(reason, "decision-none")
                   || !strcmp(reason, "decision-bias"));
}

static void IncludeReceipt(const Runtime::Tempo::DirectorMobilizationReceipt& r,
                           bool first)
{
    if (first) {
        s_policyL0Lo = s_policyL0Hi = r.stableLoco;
        s_policyA0Lo = s_policyA0Hi = r.stableAnim;
        s_policyL1Lo = s_policyL1Hi = r.rageLoco;
        s_policyA1Lo = s_policyA1Hi = r.rageAnim;
        return;
    }
    if (r.stableLoco < s_policyL0Lo) s_policyL0Lo = r.stableLoco;
    if (r.stableLoco > s_policyL0Hi) s_policyL0Hi = r.stableLoco;
    if (r.stableAnim < s_policyA0Lo) s_policyA0Lo = r.stableAnim;
    if (r.stableAnim > s_policyA0Hi) s_policyA0Hi = r.stableAnim;
    if (r.rageLoco < s_policyL1Lo) s_policyL1Lo = r.rageLoco;
    if (r.rageLoco > s_policyL1Hi) s_policyL1Hi = r.rageLoco;
    if (r.rageAnim < s_policyA1Lo) s_policyA1Lo = r.rageAnim;
    if (r.rageAnim > s_policyA1Hi) s_policyA1Hi = r.rageAnim;
}

static void ApplyPolicies()
{
    if (s_policyHardResetPending) {
        char pending[sizeof(s_policyHardResetReason)];
        lstrcpynA(pending, s_policyHardResetReason, sizeof(pending));
        s_policyHardResetPending = false;
        s_policyHardResetReason[0] = 0;
        ReleasePolicy(pending[0] ? pending : "tactical-unsafe-release", true);
    }
    if (!s_actuatorEnabled) {
        ReleasePolicy("actuator-off", true);
        return;
    }
    if (!s_enabled) {
        ReleasePolicy("director-disabled", true);
        return;
    }

    // 85.33: ВТОРОЙ ПРИКАЗ — независимо от судьбы первого. Есть второе событие —
    // есть приказ; событие пропало — приказ снят. Так деление не зависит от того,
    // успел ли главный приказ собрать своих исполнителей (иначе в сцене «все
    // ближние ушли на второй приказ» главный отказался бы и увёл деление с собой).
    // Делится только внимание: аренды темпа у второго приказа нет, поэтому он не
    // спорит с первым за энвелопы и не может испортить его учёт.
    if (s_parallelOrders && s_parallel.active && s_parallel.targetBody
        && s_parallel.nResponder > 0) {
        const int secAggro = s_parallel.response == TACTICAL_RESPONSE_ALERT
                           ? Runtime::Aggro::DIRECTOR_RESPONSE_ALERT
                           : Runtime::Aggro::DIRECTOR_RESPONSE_ALARM;
        if (Runtime::Aggro::DirectorSecondarySet(
                s_parallel.targetSlot, s_parallel.targetBody, 0, secAggro,
                s_parallel.responderKind, s_parallel.responders,
                s_parallel.nResponder)) {
            ++s_gameplayWrites;
        }

        // 85.34: ВТОРОЙ ПРИКАЗ ПОЛУЧАЕТ ТОТ ЖЕ АДРЕНАЛИН. Владелец: «события
        // быстрые, некогда зевать — реагировать надо резко». Оболочка живёт по
        // телу и не складывается, поэтому пересечения с первым приказом нет по
        // построению; списки исполнителей уже разделены.
        const SpeciesCard* secCard = FindSpeciesCard(s_parallel.responderKind);
        const bool secTempo = secCard && secCard->tempoRage;
        if (secTempo) {
            const char* tempoReason = 0;
            if (!Runtime::Tempo::DirectorReady(&tempoReason)) {
                static bool loggedNotReady = false;
                if (!loggedNotReady) {
                    loggedNotReady = true;
                    logFile << "Monster Director: secondary tempo NOT granted reason="
                            << (tempoReason ? tempoReason : "tempo-not-ready")
                            << " (attention still issued)" << std::endl;
                }
            } else {
                // Сначала отпускаем тех, кто выбыл из набора: иначе они
                // остались бы разогнанными до конца TTL.
                for (int i = 0; i < s_nSecOwned; ++i) {
                    bool still = false;
                    for (int k = 0; k < s_parallel.nResponder; ++k)
                        if (s_secOwned[i] == s_parallel.responders[k]) { still = true; break; }
                    if (!still)
                        Runtime::Tempo::ReleaseDirectorMobilization(s_secOwned[i]);
                }
                int kept = 0;
                for (int k = 0; k < s_parallel.nResponder; ++k) {
                    const uintptr_t b = s_parallel.responders[k];
                    Runtime::Tempo::DirectorMobilizationReceipt receipt;
                    const char* admitReason = 0;
                    // Отказ здесь НЕ роняет приказ: внимание важнее разгона, и
                    // падать из-за переполнения таблицы было бы хуже, чем
                    // разогнать не всех (главный приказ по-прежнему fail-closed).
                    if (!Runtime::Tempo::AdmitDirectorMobilization(
                            b, s_parallel.responderKind, s_parallel.urgency,
                            kPolicyTtlMs, &receipt, &admitReason)) {
                        static uint32_t secAdmitFails = 0;
                        if (++secAdmitFails <= 3)
                            logFile << "Monster Director: secondary mobilize skipped 0x"
                                    << std::hex << b << std::dec << " reason="
                                    << (admitReason ? admitReason : "admit-failed")
                                    << std::endl;
                        continue;
                    }
                    ++s_gameplayWrites;
                    s_secOwned[kept++] = b;
                }
                s_nSecOwned = kept;
                // Одна строка на смену набора: видно, что ВТОРОЙ приказ получил
                // тот же разгон, что и главный (темп + адреналин атаки).
                static uint32_t lastMobSig = 0;
                const uint32_t mobSig = 2166136261u ^ (uint32_t)kept * 16777619u
                                      ^ (uint32_t)(int)(s_parallel.urgency * 100.0f);
                if (mobSig != lastMobSig) {
                    lastMobSig = mobSig;
                    logFile << "Monster Director: secondary mobilized n=" << kept
                            << " urgency=" << s_parallel.urgency
                            << " (темп и адреналин атаки как у главного приказа)"
                            << std::endl;
                }
            }
        } else {
            ReleaseSecondaryMobilization("species-without-tempo");
        }
    } else {
        Runtime::Aggro::DirectorSecondaryClear("no-second-order");
        ReleaseSecondaryMobilization("no-second-order");
    }

    int targetSlot = -1;
    uintptr_t targetBody = 0;
    uintptr_t excludedBody = 0;
    int situation = TACTICAL_SITUATION_NONE;
    int response = TACTICAL_RESPONSE_NONE;
    float urgency = 0.0f;
    const char* engageReason = 0;
    const Runtime::PartyCombatSnapshot* identitySnapshot = &s_party;
    if (!ResolvePolicyIntent(&targetSlot, &targetBody, &excludedBody,
                             &situation, &response, &urgency, &engageReason,
                             &identitySnapshot)) {
        const char* reason = engageReason ? engageReason : "decision-none";
        ReleasePolicy(reason, !OrdinaryIntentCompletion(reason));
        return;
    }
    if (!(urgency == urgency) || urgency <= 0.0f || urgency > 1.0f) {
        ReleasePolicy("policy-urgency-invalid", true);
        return;
    }
    const uint64_t eventTopology = situation != TACTICAL_SITUATION_NONE
                                 ? s_tactical.topologySignature : 0;

    const char* reason = 0;
    if (!identitySnapshot || !ExactPartyIdentity(*identitySnapshot, &reason)) {
        ReleasePolicy(reason ? reason : "identity-snapshot-unavailable", true);
        return;
    }

    const char* responderKind = PolicyResponderKind(situation);
    const SpeciesCard* card = FindSpeciesCard(responderKind);
    if (!card || !card->aggroWrite) {
        ReleasePolicy("species-aggro-write-denied", true);
        return;
    }

    uintptr_t responders[kMaxPolicyWolves] = {};
    float incident[3] = {};
    const float* refPos = PolicyReferencePos(targetBody, excludedBody,
                                            *identitySnapshot, incident)
                        ? incident : 0;
    int nResponder = CollectEligibleResponders(
        responders, kMaxPolicyWolves, excludedBody, responderKind, refPos,
        s_policyResponderMax, &reason);
    // 85.33: при включённом делении главный приказ берёт ТОЛЬКО свою часть —
    // иначе одни и те же особи получили бы два приказа сразу, а вторая задача
    // осталась бы без исполнителей. Доля посчитана по близости к якорю события.
    if (s_parallelOrders && s_parallel.active) {
        nResponder = s_parallel.nPrimary;
        for (int i = 0; i < nResponder; ++i) responders[i] = s_parallel.primary[i];
        reason = "split-share";
    }
    if (nResponder <= 0) {
        const char* none = !strcmp(responderKind, "uEm0100")
                         ? "goblin-no-free-responder"
                         : (!strcmp(responderKind, "uEm0101")
                            ? "hob-pack-no-free-responder"
                            : (!strcmp(responderKind, "uEm0400")
                               ? "saurian-pack-no-free-responder"
                               : "wolf-pack-no-free-responder"));
        ReleasePolicy(excludedBody && nResponder == 0 ? none : reason, true);
        return;
    }

    const bool wantTempo = card->tempoRage;
    if (wantTempo && !Runtime::Tempo::DirectorReady(&reason)) {
        ReleasePolicy(reason, true);
        return;
    }

    if (s_policyEngaged) {
        const bool unsafeTopology = s_policyTarget != targetSlot
                                 || s_policyTargetBody != targetBody
                                 || s_policyExcludedBody != excludedBody
                                 || !SamePack(responders, nResponder,
                                              s_responderWolf,
                                              s_nResponderWolf)
                                 || (s_policySituation != TACTICAL_SITUATION_NONE
                                     && situation != TACTICAL_SITUATION_NONE
                                     && s_policyEventTopology != eventTopology);
        const bool commandTransition = s_policySituation != situation
                                    || s_policyResponse != response
                                    || s_policyEventTopology != eventTopology;
        if (unsafeTopology)
            ReleasePolicy("policy-topology-changed", true);
        else if (commandTransition)
            ReleasePolicy("policy-command-transition", false);
    }

    if (!s_policyEngaged) {
        s_policyTarget = targetSlot;
        s_policyTargetBody = targetBody;
        s_policySituation = situation;
        s_policyResponse = response;
        s_policyUrgency = urgency;
        s_policyExcludedBody = excludedBody;
        s_policyEventTopology = eventTopology;
        for (int i = 0; i < nResponder; ++i)
            s_responderWolf[s_nResponderWolf++] = responders[i];

        if (wantTempo) {
            for (int i = 0; i < nResponder; ++i) {
                Runtime::Tempo::DirectorMobilizationReceipt receipt;
                const char* tempoReason = 0;
                if (!Runtime::Tempo::AdmitDirectorMobilization(
                        responders[i], responderKind, urgency, kPolicyTtlMs,
                        &receipt, &tempoReason)) {
                    ReleasePolicy(tempoReason ? tempoReason
                                              : "tempo-mobilization-admit-failed",
                                  true);
                    return;
                }
                IncludeReceipt(receipt, s_nOwnedWolf == 0);
                s_ownedWolf[s_nOwnedWolf++] = responders[i];
                ++s_gameplayWrites;
            }
        }
    } else {
        if (urgency > s_policyUrgency) s_policyUrgency = urgency;
        // Repeated and overlapping orders refresh/maximize the one existing
        // per-body envelope; no factors are multiplied and no endpoint moves.
        if (wantTempo) {
            for (int i = 0; i < s_nOwnedWolf; ++i) {
                Runtime::Tempo::DirectorMobilizationReceipt receipt;
                const char* tempoReason = 0;
                if (!Runtime::Tempo::AdmitDirectorMobilization(
                        s_ownedWolf[i], responderKind, urgency, kPolicyTtlMs,
                        &receipt, &tempoReason)) {
                    ReleasePolicy(tempoReason ? tempoReason
                                              : "tempo-mobilization-refresh-failed",
                                  true);
                    return;
                }
                ++s_gameplayWrites;
            }
        }
    }

    const int aggroResponse = response == TACTICAL_RESPONSE_ALERT
                            ? Runtime::Aggro::DIRECTOR_RESPONSE_ALERT
                            : Runtime::Aggro::DIRECTOR_RESPONSE_ALARM;
    // Список исполнителей уходит в агро ТОЛЬКО когда лимит включён (85.23):
    // при responderMax=0 поведение обязано остаться прежним (весь вид), иначе
    // особь вне нашего обзора перестала бы получать приказ.
    // 85.33: при АКТИВНОМ ДЕЛЕНИИ список уходит в агро ВСЕГДА, даже когда лимит
    // владельца выключен (responderMax = 0). Иначе главный приказ взял бы «весь
    // вид» и получил то, что уже отдано второму приказу. Это нашла фикстура:
    // при responderMax = 0 главный забирал всех, и деление не значило ничего.
    const bool splitActive = s_parallelOrders && s_parallel.active;
    const uintptr_t* gang = (s_policyResponderMax > 0 || splitActive)
                          ? responders : 0;
    const int nGang = (s_policyResponderMax > 0 || splitActive) ? nResponder : 0;
    if (!Runtime::Aggro::DirectorFocusSet(targetSlot, targetBody, excludedBody,
                                           aggroResponse, responderKind,
                                           gang, nGang)) {
        ReleasePolicy("aggro-focus-rejected", true);
        return;
    }
    ++s_gameplayWrites;
    SetPolicyStatus(engageReason, true, "HOLD");
}

static void ResetRuntimeState(const char* reason)
{
    s_nView = 0;
    s_nSeen = 0;
    s_lastTick = 0;
    s_lastDecision = 0;
    s_lastLog = 0;
    s_lastLoggedMode = -1;
    s_havePartySignature = false;
    s_partySignature = 0;
    s_wolfCount = 0;
    s_hobCount = 0;
    s_saurCount = 0;
    memset(&s_party, 0, sizeof(s_party));
    memset(&s_cueParty, 0, sizeof(s_cueParty));
    memset(&s_tactical, 0, sizeof(s_tactical));
    s_tactical.targetSlot = -1;
    s_tactical.pairDistanceM = -1.0f;
    s_policyHardResetPending = false;
    s_policyHardResetReason[0] = 0;
    s_inactiveResetLatched = false;
    s_inactiveSafetyLatched = false;
    s_inactiveSafetyReason[0] = 0;
    s_inactiveSafetySuppressed = 0;
    memset(s_score, 0, sizeof(s_score));
    ClearPriorityOrder();
    ResetDecisionMemory(reason);
}

void Init()
{
    s_enabled = config.getBool("monsterAI", "enabled", false);
    s_actuatorEnabled = config.getBool("monsterAI", "wolfActuator", false);
    // 85.23: 0 = все подходящие (прежнее поведение), N = N ближайших к очагу.
    s_policyResponderMax = config.getInt("monsterAI", "responderMax", 0);
    if (s_policyResponderMax < 0) s_policyResponderMax = 0;
    s_chantNearest = config.getBool("monsterAI", "chantNearest", true);
    // 85.27: радиус встречи у тела павшей пешки. Ключа в ini может не быть —
    // тогда он допишется сам со значением по умолчанию (как chantNearest).
    // 0 = механизм выключен полностью.
    s_fallenGuardRadius = config.getFloat("monsterAI", "fallenGuardRadius", 10.0f);
    if (!(s_fallenGuardRadius == s_fallenGuardRadius) || s_fallenGuardRadius < 0.0f)
        s_fallenGuardRadius = 10.0f;   // NaN/мусор из ini — не оставляем без защиты
    MonsterAI::SetFallenGuardRadius(s_fallenGuardRadius);
    s_pawnFinish = config.getBool("monsterAI", "pawnFinish", true);
    // 85.33: параллельные приказы. По умолчанию ВЫКЛЮЧЕНО: сначала владелец
    // смотрит в логе деление (SPLIT), потом включает этот ключ без пересборки.
    s_parallelOrders = config.getBool("monsterAI", "parallelOrders", false);
    MonsterAI::SetPawnFinishEnabled(s_pawnFinish);
    MonsterAI::SetNearestPairFallback(s_chantNearest);
    if (s_policyResponderMax > kMaxPolicyWolves)
        s_policyResponderMax = kMaxPolicyWolves;
    s_gameplayWrites = 0;
    s_policyEngaged = false;
    Runtime::Tempo::HardResetAllDirectorMobilization();
    memset(s_ownedWolf, 0, sizeof(s_ownedWolf));
    memset(s_responderWolf, 0, sizeof(s_responderWolf));
    s_nOwnedWolf = 0;
    s_nResponderWolf = 0;
    s_policyTarget = -1;
    s_policyTargetBody = 0;
    s_policySituation = TACTICAL_SITUATION_NONE;
    s_policyResponse = TACTICAL_RESPONSE_NONE;
    s_policyUrgency = 0.0f;
    s_policyL0Lo = s_policyL0Hi = 0.0f;
    s_policyA0Lo = s_policyA0Hi = 0.0f;
    s_policyL1Lo = s_policyL1Hi = 0.0f;
    s_policyA1Lo = s_policyA1Hi = 0.0f;
    s_policyExcludedBody = 0;
    s_policyEventTopology = 0;
    lstrcpynA(s_policyStatus, s_actuatorEnabled ? "waiting-for-intent"
                                                : "actuator-off",
              sizeof(s_policyStatus));
    ResetRuntimeState("waiting");
    Runtime::Aggro::SetObserverDemand(s_enabled);
    // 84.21: rage-профили видов — из карточек (единый источник правды).
    // 85.36: сами числа вида можно править в ddda_ai_overhaul.ini, секция
    // [species.<kind>] — без пересборки. Ключа нет = число карточки, поэтому
    // старый ini ведёт себя как раньше. Небезопасное число (ниже базового
    // диапазона) поднимается в SpeciesTuning и печатается в лог: молчаливый
    // отказ приказа разбирать в поле дороже, чем строка в логе.
    {
        float baseLocoMin = 1.0f, baseLocoMax = 1.0f;
        float baseAnimMin = 1.0f, baseAnimMax = 1.0f;
        Runtime::Tempo::GetRange(&baseLocoMin, &baseLocoMax);
        Runtime::Tempo::GetAnimRange(&baseAnimMin, &baseAnimMax);

        IniSpeciesReader reader;
        for (int i = 0; i < SpeciesCardCount(); ++i) {
            const SpeciesCard* card = &kSpeciesCards[i];
            if (!card->tempoRage) continue;

            const MonsterAI::SpeciesTempoNumbers n = MonsterAI::SpeciesTempoFromIni(
                reader, *card, baseLocoMin, baseLocoMax, baseAnimMin, baseAnimMax);

            if (!n.rageEnabled) {
                logFile << "Monster Director: species " << card->kind
                        << " tempoRage=0 (ini): no order boost for this species"
                        << std::endl;
                continue;
            }

            Runtime::Tempo::RegisterRageProfile(card->kind, n.rageLocoMin,
                                                n.rageLocoMax,
                                                n.rageAnimMin, n.rageAnimMax);

            char l[320];
            sprintf_s(l, "Monster Director: species %s rage ceilings run %.2f..%.2f"
                         " swing %.2f..%.2f (base run %.2f..%.2f swing %.2f..%.2f)",
                      card->kind, n.rageLocoMin, n.rageLocoMax,
                      n.rageAnimMin, n.rageAnimMax,
                      baseLocoMin, baseLocoMax, baseAnimMin, baseAnimMax);
            logFile << l << std::endl;
            if (n.sanitized)
                logFile << "Monster Director: species " << card->kind
                        << " numbers raised to safe: " << n.note << std::endl;
        }
    }

    // 85.40: РАНГИ ОСОБЕЙ. Читаем здесь (директор уже держит адаптер к
    // ddda_ai_overhaul.ini) и отдаём в рантайм-модуль темпа, откуда их берёт
    // тюнер при выдаче размера и статов. Вид допускается списком в коде
    // (сегодня гоблин), ini может только выключить: [species.<kind>] ranks = off.
    // Числа — секция [ranks], встроенные значения пилота в MonsterTempo.cpp.
    {
        IniSpeciesReader reader;
        int nOn = 0;
        for (int i = 0; i < SpeciesCardCount(); ++i) {
            const SpeciesCard* card = &kSpeciesCards[i];
            const Runtime::Tempo::RanksNumbers ln =
                Runtime::Tempo::RanksFromIni(reader, card->kind);
            Runtime::Tempo::RegisterRanks(card->kind, ln);
            if (!ln.enabled) continue;
            ++nOn;
            // 85.42: печатаем и ВЕСА — без них из лога не видно, какая доля
            // пачки задумана на каждый ранг (в 85.41 именно это мешало понять,
            // почему в пачке оказалось три мини-босса).
            char l[420];
            sprintf_s(l, "Monster Director: ranks %s ON  novice w%.2f %.2f..%.2f x%.2f"
                         " | soldier w%.2f %.2f..%.2f x%.2f | veteran w%.2f %.2f..%.2f x%.2f"
                         " | elite w%.2f %.2f..%.2f x%.2f | miniboss w%.2f %.2f..%.2f x%.2f",
                      card->kind,
                      ln.step[0].weight, ln.step[0].sizeMin, ln.step[0].sizeMax, ln.step[0].atk,
                      ln.step[1].weight, ln.step[1].sizeMin, ln.step[1].sizeMax, ln.step[1].atk,
                      ln.step[2].weight, ln.step[2].sizeMin, ln.step[2].sizeMax, ln.step[2].atk,
                      ln.step[3].weight, ln.step[3].sizeMin, ln.step[3].sizeMax, ln.step[3].atk,
                      ln.step[4].weight, ln.step[4].sizeMin, ln.step[4].sizeMax, ln.step[4].atk);
            logFile << l << std::endl;
        }
        if (nOn == 0)
            logFile << "Monster Director: ranks off (no species allowed/enabled)"
                    << std::endl;
    }
    lstrcpynA(s_status, s_enabled
        ? "Monster Director: PackMark+tactics armed"
        : "Monster Director: disabled", sizeof(s_status));
    logFile << "Monster Director: " << (s_enabled ? "enabled" : "disabled")
            << " Build012 integrated urgency + mobilization;"
            << " decision=500ms; situationScan=150ms; hold=2500ms;"
            << " grabAlert=GrabStart/750ms/pin-only-Aggro;"
            << " goblinGrab=GrabStart|Hagaijime/4000ms/pin+goblin-fakehit+std-rush-no-suppress+empty-card-wake+live-gate(f8&1,fC45);"
            << " hobPack=uEm0101 PackMark+HOB-GRAB/pin+goblin-family-card/2FA0-28C;"
            << " saurPack=uEm0400 PackMark-only/no-grab/pin+saurian-head;"
            << " groundAlarm=Hagaijime4Feet/4000ms/independent;"
            << " liftAlarm=literal-lift/2500ms/separate;"
            << " allAdmittedUrgency=1.0; Tempo=immutable-uEm0200-endpoints/1400ms-decay;"
            << " switchMargin=20%; focusIntent=NONE/BIAS/FOCUS-WINDOW;"
            << " isolation=20%/100%; depth=highestHP/markHP-1;"
            << " tacticsActuator=" << (s_actuatorEnabled ? "ON" : "OFF")
            << " chantNearest=" << (s_chantNearest ? 1 : 0)
            << " pawnFinish=" << (s_pawnFinish ? 1 : 0)
            << "(0=off; awake knocked-down pawn is TARGET, not the player)"
            << " fallenGuardRadius=" << s_fallenGuardRadius
            << "(0=off; FALLEN-GUARD situation -> Arisen meet)"
            << " responderMax=" << s_policyResponderMax << "(0=all)"
            << " occupied-on-field+rifted-skip+same-kind+unique-spatial-admission;"
            << " observerOnly=" << (s_actuatorEnabled ? 0 : 1)
            << " writes=0" << std::endl;
    PackObserveInit();
}

void Shutdown()
{
    PackObserveShutdown();
    ReleasePolicy("shutdown", true);
    s_actuatorEnabled = false;
    s_enabled = false;
    Runtime::Aggro::SetObserverDemand(false);
    ResetRuntimeState("shutdown");
    lstrcpynA(s_status, "Monster Director: disabled", sizeof(s_status));
}

void OnWorldUnload()
{
    const DWORD now = GetTickCount();
    TacticalRelease("world-unload", false, true, now);
    ReleasePolicy("world-unload", true);
    ResetRuntimeState("world-unload");
}

void Tick()
{
    if (!s_enabled) return;

    const DWORD now = GetTickCount();
    if (s_lastTick && now - s_lastTick < kTickMs) return;
    s_lastTick = now;

    UpdateViews(now);
    UpdateTacticalSituations(now);
    if (!s_lastDecision || now - s_lastDecision >= kDecisionMs) {
        s_lastDecision = now;
        Decide(now);
    }
    ApplyPolicies();
    UpdateStatus(now);
}

bool Enabled() { return s_enabled; }

void SetEnabled(bool on)
{
    if (s_enabled == on) return;
    if (!on) {
        ReleasePolicy("director-disabled", true);
    }
    s_enabled = on;
    Runtime::Aggro::SetObserverDemand(on);
    ResetRuntimeState(on ? "enabled-reset" : "disabled");
    lstrcpynA(s_status, on
        ? "Monster Director: PackMark+tactics armed"
        : "Monster Director: disabled", sizeof(s_status));
    logFile << "Monster Director: " << (on ? "ON" : "OFF")
            << " (Build012 PackMark+restraint+urgency-envelope; tactical memory reset; actuator="
            << (s_actuatorEnabled ? "ON" : "OFF")
            << " observerOnly=" << (s_actuatorEnabled ? 0 : 1)
            << " writes=" << s_gameplayWrites << ")" << std::endl;
}

void SetActuatorEnabled(bool on)
{
    if (s_actuatorEnabled == on) return;
    if (!on) ReleasePolicy("actuator-disabled", true);
    s_actuatorEnabled = on;
    if (on) {
        s_gameplayWrites = 0;
        // A consent/control boundary begins a fresh diagnostic episode. The
        // first operational failure after re-enable must be visible again.
        s_inactiveResetLatched = false;
        s_inactiveSafetyLatched = false;
        s_inactiveSafetyReason[0] = 0;
        s_inactiveSafetySuppressed = 0;
        SetPolicyStatus("waiting-for-intent", false);
    } else {
        SetPolicyStatus("actuator-off", false);
    }
    logFile << "Monster Director: tactics actuator " << (on ? "ON" : "OFF")
            << " (same consent switch; uEm0200 pack + uEm0100 grab + uEm0101 pack/grab + uEm0400 pack;"
            << " GrabStart ALERT + ground/lift ALARM + PackMark;"
            << " occupied-exact; every admitted order urgency=1.0;"
            << " immutable rage endpoints + automatic decay)" << std::endl;
}

bool ActuatorEnabled() { return s_actuatorEnabled; }
bool PolicyEngaged() { return s_policyEngaged; }
const char* PolicyStatus() { return s_policyStatus; }

int ViewCount() { return s_nView; }

const MonsterView* ViewAt(int i)
{
    if (i < 0 || i >= s_nView) return 0;
    return &s_view[i];
}

const char* Status() { return s_status; }

bool HuntTelemetryAt(int slot, HuntTelemetry* out)
{
    if (!out || slot < 0 || slot >= Runtime::PARTY_COMBAT_SLOTS) return false;
    memset(out, 0, sizeof(*out));
    const Runtime::PartyCombatMember& m = s_party.member[slot];
    const TargetScore& q = s_score[slot];

    out->slot = slot;
    out->priorityRank = PriorityRankOf(slot);
    out->recordValid = m.recordValid;
    out->hpValid = m.hpValid;
    out->scoreValid = q.valid;
    out->bodyValid = m.bodyValid;
    out->positionValid = m.positionValid;
    out->coreStatsValid = m.statsValid;
    out->currentHp = m.currentHp;
    out->maxHp = m.maxHp;
    out->coreStrength = m.strength;
    out->coreDefense = m.defense;
    out->coreMagick = m.magick;
    out->coreMagickDefense = m.magickDefense;
    out->lowAbsoluteHp = q.lowAbsoluteHp;
    out->huntScore = q.huntScore;
    out->nearWolfCount = s_wolfCount;
    return m.recordValid;
}

int PackMarkSlot() { return s_mark; }
int RunnerUpSlot() { return s_runner; }
int PrioritySlot(int rank)
{
    if (rank < 0 || rank >= Runtime::PARTY_COMBAT_SLOTS) return -1;
    return s_order[rank];
}
int Recommendation() { return s_mode; }
const char* RecommendationName() { return ModeName(s_mode); }
float TargetIsolationRatio() { return IsolationFor(s_mark, s_runner); }
float TargetDepthRatio() { return DepthFor(s_mark); }
int ScoredWolfCount() { return s_wolfCount; }
int GameplayWriteCount() { return s_gameplayWrites; }

uint32_t HoldRemainingMs()
{
    if (s_mark < 0 || !s_markSince) return 0;
    const DWORD elapsed = GetTickCount() - s_markSince;
    return elapsed >= kMinHoldMs ? 0 : (uint32_t)(kMinHoldMs - elapsed);
}

void DumpSnapshot()
{
    const DWORD now = GetTickCount();
    UpdateViews(now);
    if (!Runtime::ReadPartyCombatSnapshot(&s_party)) {
        memset(&s_party, 0, sizeof(s_party));
        memset(s_score, 0, sizeof(s_score));
        ClearPriorityOrder();
        s_wolfCount = 0;
        s_hobCount = 0;
        s_saurCount = 0;
    } else {
        ScoreParty();
        BuildPriorityOrder();
    }

    logFile << "Monster Director: ===== manual Build012 urgency+mobilization snapshot ====="
            << " enabled=" << (s_enabled ? 1 : 0)
            << " actuator=" << (s_actuatorEnabled ? 1 : 0)
            << " observerOnly=" << (s_actuatorEnabled ? 0 : 1)
            << " writes=" << s_gameplayWrites << " enemies=" << s_nView
            << " wolves=" << s_wolfCount
            << " hobs=" << s_hobCount
            << " saurs=" << s_saurCount
            << " situation=" << (s_tactical.active
                                  ? TacticalSituationName(s_tactical.situation) : "-")
            << " response=" << (s_tactical.active
                                 ? TacticalResponseName(s_tactical.response) : "NONE")
            << " urgency=" << s_policyUrgency
            << " responders=" << s_nResponderWolf
            << " tempoOwned=" << s_nOwnedWolf
            << " tempoTracks=" << Runtime::Tempo::DirectorMobilizationCount()
            << " endpoints{L0=" << s_policyL0Lo << ".." << s_policyL0Hi
            << ",A0=" << s_policyA0Lo << ".." << s_policyA0Hi
            << ",L1=" << s_policyL1Lo << ".." << s_policyL1Hi
            << ",A1=" << s_policyA1Lo << ".." << s_policyA1Hi << "}"
            << " policy=" << s_policyStatus
            << " strategicBody/position/native=ignored"
            << " tacticalStrictAdmission+stickyExactContinuation"
            << std::endl;
    LogPartyRaw("manual");
    for (int i = 0; i < Runtime::PARTY_COMBAT_SLOTS; ++i)
        LogScoreLine(i, "party");
    LogDecision("MANUAL", now, false);

    for (int i = 0; i < s_nView; ++i) {
        const MonsterView& v = s_view[i];
        char line[300];
        sprintf_s(line,
            "MD: enemy %s @%p act=%s attacking=%d distArisen=%.2f "
            "loco=%.3f atk=%.3f wolfObserved=%d strategicSpatial=ignored tacticalPairSpatial=observed",
            v.kind, (void*)v.body, v.act[0] ? v.act : "?",
            v.attacking ? 1 : 0, v.distM, v.locoFactor, v.atkFactor,
            IsWolf(v) ? 1 : 0);
        logFile << line << std::endl;
    }
    PackObserveDump();
    // 84.16 dual-observe: goblin card dump + party status/downed snapshot.
    Runtime::Aggro::CardReconDump();
    Runtime::PartyStatus::DumpSnapshot();
    logFile << "Monster Director: ===== end snapshot =====" << std::endl;
}

} // namespace MonsterAI

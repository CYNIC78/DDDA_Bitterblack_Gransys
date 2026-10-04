#include "stdafx.h"
#include "NexusDoctrine.h"
#include "NexusPolicy.h"
#include "GuardianDoctrine.h" // единая карточка базового стека/ранга каждой пешки
#include "DoctrineAnnounce.h"          // 85.63: окно тишины на объявление цели
#include "../runtime/LogMemSession.h" // 85.63: сводка — в полевой пакет
#include "PawnPersona.h"
#include "PartyRescueProtocol.h"
#include "PawnAI_Common.h"
#include "runtime/Runtime.h"
#include "runtime/MemProbe.h"
#include "runtime/MonsterTempo.h"
#include "../CombatBus.h"
#include <math.h>
#include <stdio.h>

namespace PawnAI {
namespace Nexus {

using namespace Runtime;
using Runtime::Mem::Rd;
using Runtime::Mem::RdPtr;
using Runtime::Mem::WrSafe;
using Runtime::Mem::NameOfLiveObjectSafe;

static bool s_enabled = true;
static bool s_probeLog = true; // [pawnAI] nexusProbeLog (events only)

// 85.63: СЧЁТЧИКИ СЕССИИ — вне состояния слота (Nexus::Shutdown() его обнуляет,
// а сводка печатается после выгрузки модулей). Как у Guardian: сколько замеров
// начато/завершено и сколько объявлений цели свернуло окно тишины.
static int s_probesStarted   = 0;
static int s_probesCompleted = 0;
static int s_targetAnnounces = 0;
static int s_targetMuted     = 0;

// У каждого исполнителя своя привязка, latch и последняя цель. Роль
// выбирается его собственным стеком; две Nexus могут охранять одну пешку.
struct NexusState {
    bool active;
    int partnerSlot;
    const char* partnerRole;
    float pawnPartnerDist;
    int threatsInZone;
    uintptr_t targetThreatBody;
    char targetThreatKind[32];
    bool criticalThreat;
    uintptr_t lastLoggedTarget;
    uintptr_t probeBody;       // one fixed enemy for the whole 2.5s window
    DWORD probeStartMs, probeNextMs;
    float probePawnEnemyM, probePartnerEnemyM;
    // 85.63: СОСТОЯНИЕ НА СТАРТЕ (как у Guardian): замер печатает оба конца —
    // было -> стало — одной строкой, поэтому старт надо запомнить, а не печатать.
    int   probeCode;
    int   probePartnerSlot;
    char  probeAct[48];
    AnnounceThrottle announce; // 85.63: окно тишины на объявление цели
    NexusPolicy::Assignment assignment;
    uintptr_t actorBody, actorRecord;
    DWORD lastEncounterMs;
    bool encounterSeen;
    bool emergencyLatch[4];
    uintptr_t candidateBody[4], candidateRecord[4];
    NexusState() : active(false), partnerSlot(-1), partnerRole("none"),
        pawnPartnerDist(1e9f), threatsInZone(0), targetThreatBody(0),
        criticalThreat(false), lastLoggedTarget(0), probeBody(0),
        probeStartMs(0), probeNextMs(0), probePawnEnemyM(0),
        probePartnerEnemyM(0), probeCode(-1), probePartnerSlot(-1), actorBody(0),
        actorRecord(0), lastEncounterMs(0), encounterSeen(false) {
        memset(targetThreatKind, 0, sizeof(targetThreatKind));
        memset(probeAct, 0, sizeof(probeAct));
        memset(emergencyLatch, 0, sizeof(emergencyLatch));
        memset(candidateBody, 0, sizeof(candidateBody));
        memset(candidateRecord, 0, sizeof(candidateRecord));
    }
};
static NexusState s_state[4]; // индексы PARTY_MAIN..PARTY_HIRED2

static float Dist3D(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Actor/anchor identity is compared only; cached pointers are never dereferenced.
static void ResetAssignment(NexusState& st)
{
    st.assignment.Reset();
    st.actorBody = st.actorRecord = 0;
    st.lastEncounterMs = 0;
    st.encounterSeen = false;
    memset(st.emergencyLatch, 0, sizeof(st.emergencyLatch));
    memset(st.candidateBody, 0, sizeof(st.candidateBody));
    memset(st.candidateRecord, 0, sizeof(st.candidateRecord));
}

static bool MeleeExecutor(const Runtime::PartyCombatMember& m)
{
    return m.recordValid && m.bodyValid && m.positionValid &&
        m.hpValid && m.currentHp > 0 && !m.downedHint &&
        (m.vocation == VOC_FIGHTER || m.vocation == VOC_WARRIOR || m.vocation == VOC_STRIDER);
}

static int SelectAnchorPartner(int mySlot, NexusState& st, const Runtime::PartyCombatSnapshot& party,
                               const bool eligibleNexus[4], const bool eligibleGuardian[4],
                               const char** outRole)
{
    const Runtime::PartyCombatMember& actor = party.member[mySlot];
    if (st.actorBody != actor.body || st.actorRecord != actor.record) {
        ResetAssignment(st);
        st.actorBody = actor.body; st.actorRecord = actor.record;
    }
    NexusPolicy::Candidate c[4] = {};
    const WorldReport world = CombatBus::Instance().LastWorld();
    bool encounter = false;
    for (int slot = 1; slot <= 3; ++slot) {
        const Runtime::PartyCombatMember& m = party.member[slot];
        if (slot == mySlot || !m.recordValid || !m.bodyValid || !m.positionValid) {
            st.emergencyLatch[slot] = false; continue;
        }
        if (st.candidateBody[slot] != m.body || st.candidateRecord[slot] != m.record) {
            st.emergencyLatch[slot] = false;
            st.candidateBody[slot] = m.body; st.candidateRecord[slot] = m.record;
        }
        c[slot].valid = !(eligibleGuardian[slot] || eligibleNexus[slot]);
        c[slot].body = m.body; c[slot].record = m.record;
        bool nearbyThreat = false;
        for (int i = 0; i < world.count; ++i) {
            const WorldPresence& u = world.units[i];
            if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind) ||
                strstr(u.actName, "Die") || strstr(u.actName, "Dead")) continue;
            float d = Dist3D(m.x, m.y, m.z, u.x, u.y, u.z) / 100.f;
            if (d <= 18.f && u.inCombatAction) encounter = true;
            if (d <= 6.f && u.inCombatAction) nearbyThreat = true;
        }
        // Low HP alone is not an emergency. Three seconds of clear state
        // release the temporary anchor in the pure policy.
        if ((eligibleGuardian[slot] || eligibleNexus[slot]) || !m.hpValid || m.currentHp <= 0 || !nearbyThreat) st.emergencyLatch[slot] = false;
        else {
            float ratio = m.currentHp / m.maxHp;
            if (ratio < .30f) st.emergencyLatch[slot] = true;
            else if (ratio >= .45f) st.emergencyLatch[slot] = false;
        }
        c[slot].emergency = st.emergencyLatch[slot];
        if (!(eligibleGuardian[slot] || eligibleNexus[slot]) && m.hpValid && m.currentHp > 0 && !m.downedHint) {
            c[slot].score = m.vocation == VOC_MAGE ? 10 :
                (m.vocation == VOC_SORCERER || m.vocation == VOC_FIGHTER ||
                 m.vocation == VOC_WARRIOR) ? 30 : 20;
        }
    }
    DWORD now = MsNow();
    if (encounter) { st.lastEncounterMs = now; st.encounterSeen = true; }
    else if (st.encounterSeen && DWORD(now - st.lastEncounterMs) >= 8000) {
        st.assignment.Reset(); st.encounterSeen = false;
    }
    if (!st.encounterSeen) { *outRole = "waiting for encounter"; return -1; }
    int chosen = st.assignment.Select(c, now);
    *outRole = chosen < 0 ? "no eligible partner" :
               st.assignment.temporary.slot >= 1 ? "Emergency Cover" : "Assigned Partner";
    return chosen;
}

void Shutdown()
{
    for (int slot = Runtime::PARTY_MAIN; slot <= Runtime::PARTY_HIRED2; ++slot)
        s_state[slot] = NexusState();
}

// 85.63: СВОДКА ДЛЯ ПОЛЕВОГО ПАКЕТА. Читает только счётчики уровня файла —
// то есть её можно звать и после Shutdown() (он обнуляет состояние слотов, а
// счётчики сессии не трогает).
void SessionSummary()
{
    char l[220];
    sprintf_s(l, "NexusDoctrine: session summary probes=%d completed=%d unpaired=%d"
                 " announces=%d muted=%d",
              s_probesStarted, s_probesCompleted,
              s_probesStarted - s_probesCompleted,
              s_targetAnnounces, s_targetMuted);
    LogMem::SessionNote(l);
}

void Init()
{
    Shutdown();
    s_enabled = config.getBool("pawnAI", "nexusEnabled", true);
    s_probeLog = config.getBool("pawnAI", "nexusProbeLog", true);
    logFile << "NexusDoctrine: initialized (per-pawn partner assignments)" << std::endl;
}

Status GetStatusFor(int slot)
{
    Status out = {};
    out.enabled = s_enabled;
    out.nexusSlot = slot;
    out.partnerSlot = -1;
    out.partnerRole = "none";
    out.pawnPartnerDist = 1e9f;
    if (slot < Runtime::PARTY_MAIN || slot > Runtime::PARTY_HIRED2) return out;
    const NexusState& st = s_state[slot];
    out.active = st.active;
    out.partnerRole = st.partnerRole;
    out.partnerSlot = st.partnerSlot;
    out.pawnPartnerDist = st.pawnPartnerDist;
    out.threatsInZone = st.threatsInZone;
    out.targetThreatBody = st.targetThreatBody;
    lstrcpynA(out.targetThreatKind, st.targetThreatKind, sizeof(out.targetThreatKind));
    out.criticalThreat = st.criticalThreat;
    return out;
}

Status GetStatus()
{
    // Совместимость со старой панелью: первый активный, затем первый
    // назначенный. Полный список доступен через GetStatusFor(slot).
    for (int slot = Runtime::PARTY_MAIN; slot <= Runtime::PARTY_HIRED2; ++slot)
        if (s_state[slot].active) return GetStatusFor(slot);
    for (int slot = Runtime::PARTY_MAIN; slot <= Runtime::PARTY_HIRED2; ++slot)
        if (s_state[slot].partnerSlot >= Runtime::PARTY_MAIN) return GetStatusFor(slot);
    return GetStatusFor(-1);
}

void SetEnabled(bool on)
{
    s_enabled = on;
    if (!on) Shutdown();
}

// One start + one result per window per Nexus, never a per-tick log.
// WorldReport is the only source of enemy coordinates: stale pointers are
// never dereferenced. Disappearance is printed as "gone", not distance 0.
static void NexusProbeResult(int slot, NexusState& st,
                             const Runtime::PartyCombatSnapshot& party)
{
    if (!st.probeBody || DWORD(MsNow() - st.probeStartMs) < 2500) return;
    const Runtime::PartyCombatMember& actor = party.member[slot];
    const WorldReport w = CombatBus::Instance().LastWorld();
    const WorldPresence* threat = 0;
    for (int i = 0; i < w.count; ++i)
        if (w.units[i].ptr == st.probeBody) { threat = &w.units[i]; break; }
    char act[48] = {};
    Runtime::ReadLiveAct(actor.body, act, sizeof(act));
    int32_t code = -1;
    Runtime::PawnPriorityCodeFor(actor.body, &code);
    uintptr_t current = 0;
    const bool targetOk = RdPtr((void*)(actor.body + 0x2EB8), &current);
    float pawnEnemy = -1.0f, partnerEnemy = -1.0f;
    if (threat && actor.positionValid)
        pawnEnemy = Dist3D(actor.x, actor.y, actor.z,
                           threat->x, threat->y, threat->z) / 100.0f;
    if (threat && st.partnerSlot >= Runtime::PARTY_MAIN &&
        st.partnerSlot <= Runtime::PARTY_HIRED2 &&
        party.member[st.partnerSlot].positionValid) {
        const Runtime::PartyCombatMember& partner = party.member[st.partnerSlot];
        partnerEnemy = Dist3D(partner.x, partner.y, partner.z,
                              threat->x, threat->y, threat->z) / 100.0f;
    }
    ++s_probesCompleted;
    if (s_probeLog) {
        // 85.63: одна строка на замер, оба конца в ней. Партнёр берётся из
        // записи старта: за окно назначение могло смениться, и подставлять
        // текущего было бы подменой наблюдения.
        char line[400];
        sprintf_s(line, "NexusDoctrine: [%s] PROBE enemy 0x%08X partner %s %s"
                        " P-E %.1f->%.1fm A-E %.1f->%.1fm code %d->%d act %s->%s"
                        " target %s0x%08X",
                  Runtime::PartyCombatSlotName(slot), (unsigned)st.probeBody,
                  (st.probePartnerSlot >= Runtime::PARTY_MAIN &&
                   st.probePartnerSlot <= Runtime::PARTY_HIRED2)
                       ? Runtime::PartyCombatSlotName(st.probePartnerSlot) : "none",
                  threat ? "seen" : "gone", st.probePawnEnemyM, pawnEnemy,
                  st.probePartnerEnemyM, partnerEnemy,
                  st.probeCode, code, st.probeAct[0] ? st.probeAct : "?",
                  act[0] ? act : "?", targetOk ? "" : "unreadable/",
                  (unsigned)current);
        logFile << line << std::endl;
    }
    st.probeBody = 0;
    st.probeNextMs = MsNow();
}

static void TickOne(int slot, NexusState& st, const Runtime::PartyCombatSnapshot& party,
                    const bool eligibleNexus[4], const bool eligibleGuardian[4])
{
    st.active = false;
    st.threatsInZone = 0;
    st.criticalThreat = false;
    st.targetThreatBody = 0;
    st.targetThreatKind[0] = 0;
    const Runtime::PartyCombatMember& actor = party.member[slot];
    NexusProbeResult(slot, st, party);
    const uintptr_t nexusBody = actor.body;
    const char* nexusRoleName = Runtime::PartyCombatSlotName(slot);
    const char* selectedRole = "none";
    int partnerSlot = SelectAnchorPartner(slot, st, party, eligibleNexus,
                                          eligibleGuardian, &selectedRole);
    if (partnerSlot < Runtime::PARTY_MAIN || partnerSlot > Runtime::PARTY_HIRED2) {
        if (st.partnerRole != selectedRole &&
            !strcmp(selectedRole, "no eligible partner"))
            logFile << "NexusDoctrine: [" << nexusRoleName
                    << "] no eligible partner (Guardian/Nexus excluded)" << std::endl;
        st.active = false;
        st.targetThreatBody = st.lastLoggedTarget = 0;
        st.targetThreatKind[0] = 0;
        st.partnerSlot = -1;
        st.partnerRole = selectedRole; // waiting for encounter / no eligible partner
        return;
    }
    if (st.partnerSlot != partnerSlot || st.partnerRole != selectedRole) {
        logFile << "NexusDoctrine: ASSIGN actor=" << slot
                << " primary=" << st.assignment.primary.slot
                << " temporary=" << st.assignment.temporary.slot
                << " effective=" << partnerSlot << " role=" << selectedRole << std::endl;
    }
    st.partnerSlot = partnerSlot;
    st.partnerRole = selectedRole;

    const Runtime::PartyCombatMember& partner = party.member[partnerSlot];
    const uintptr_t partnerBody = partner.body;
    if (!partnerBody) return;
    float ax = 0, ay = 0, az = 0;
    float px = 0, py = 0, pz = 0;
    if (!Rd((const void*)(partnerBody + 0x40), &ax, 4) ||
        !Rd((const void*)(partnerBody + 0x44), &ay, 4) ||
        !Rd((const void*)(partnerBody + 0x48), &az, 4)) return;
    if (!Rd((const void*)(nexusBody + 0x40), &px, 4) ||
        !Rd((const void*)(nexusBody + 0x44), &py, 4) ||
        !Rd((const void*)(nexusBody + 0x48), &pz, 4)) return;
    st.pawnPartnerDist = Dist3D(px, py, pz, ax, ay, az) / 100.0f;

    // 3. Сканируем угрозы вокруг партнера (двухуровневый периметр: Melee 6м, Preempt 12м)
    const WorldReport w = CombatBus::Instance().LastWorld();
    int inZone = 0;
    float minThreatDist = 1e9f;
    uintptr_t bestThreatBody = 0;
    char bestThreatKind[32] = {};
    bool criticalThreat = false;

    for (int i = 0; i < w.count; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
        if (strstr(u.actName, "Die") || strstr(u.actName, "Dead")) continue;

        float d = Dist3D(ax, ay, az, u.x, u.y, u.z) / 100.0f; // метры до партнера

        const bool inCriticalMelee = (d <= 6.0f);
        const bool inPreemptZone   = (d <= 12.0f && u.inCombatAction);

        if (inCriticalMelee || inPreemptZone) {
            ++inZone;
            if (NexusPolicy::BetterThreat(inCriticalMelee, d, u.ptr,
                                          criticalThreat, minThreatDist, bestThreatBody)) {
                minThreatDist = d;
                bestThreatBody = u.ptr;
                lstrcpynA(bestThreatKind, u.kind, sizeof(bestThreatKind));
                criticalThreat = inCriticalMelee;
            }
        }
    }

    st.threatsInZone = inZone;
    st.criticalThreat = criticalThreat;

    // 4. Реализация перехвата и удержания строя
    const bool withinLeash = (st.pawnPartnerDist <= 18.0f);

    if (inZone > 0 && bestThreatBody && withinLeash && !PawnAI::Rescue::IsActive()) {
        st.active = true;
        st.targetThreatBody = bestThreatBody;
        lstrcpynA(st.targetThreatKind, bestThreatKind, sizeof(st.targetThreatKind));

        // Направляем боевую цель планировщика (uCmc+0x2EB8) и взгляд (+0x14E0) на угрозу
        uintptr_t readback = 0;
        if (!Runtime::Mem::RegionOk(nexusBody + 0x2EB8, sizeof(uintptr_t)) ||
            !WrSafe((void*)(nexusBody + 0x2EB8), &st.targetThreatBody, sizeof(uintptr_t)) ||
            !Rd((void*)(nexusBody + 0x2EB8), &readback, sizeof(readback)) ||
            readback != st.targetThreatBody) {
            st.active = false;
            return;
        }
        // One probe per >=8s, even when the chosen enemy switches each tick.
        // The first eligible signal fixes the enemy body for comparison.
        const DWORD probeNow = MsNow();
        if (s_probeLog && !st.probeBody &&
            (!st.probeNextMs || DWORD(probeNow - st.probeNextMs) >= 8000)) {
            for (int i = 0; i < w.count; ++i) if (w.units[i].ptr == bestThreatBody) {
                const WorldPresence& t = w.units[i];
                st.probeBody = bestThreatBody;
                st.probeStartMs = probeNow;
                st.probePawnEnemyM = Dist3D(px, py, pz, t.x, t.y, t.z) / 100.0f;
                st.probePartnerEnemyM = Dist3D(ax, ay, az, t.x, t.y, t.z) / 100.0f;
                // 85.63: НА СТАРТЕ МОЛЧИМ — запоминаем состояние. Измерение
                // печатает одну строку на финише окна (см. NexusProbeResult).
                Runtime::ReadLiveAct(nexusBody, st.probeAct, sizeof(st.probeAct));
                Runtime::PawnPriorityCodeFor(nexusBody, &st.probeCode);
                st.probePartnerSlot = partnerSlot;
                ++s_probesStarted;
                break;
            }
        }
        // Leave gaze to the engine: avoid a second, non-atomic write.

        // Даем скоростной рывок для перехвата
        // No shared tempo override: Nexus cannot safely own/clear another module's entry.

        if (st.lastLoggedTarget != st.targetThreatBody) {
            st.lastLoggedTarget = st.targetThreatBody;
            // 85.63: качели между двумя телами больше не печатаются (см.
            // DoctrineAnnounce.h). Цель ставится как и раньше — молчит строка.
            if (st.announce.Allow(st.targetThreatBody, MsNow())) {
                ++s_targetAnnounces;
                char l[256];
                sprintf_s(l, "NexusDoctrine: [%s] PROACTIVE TARGET -> %s 0x%08X (%s) dist=%.1fm (pawn-partner=%.1fm, partner: %s [%s])",
                          nexusRoleName, criticalThreat ? "CRITICAL-MELEE" : "PREEMPT-INTERCEPT",
                          (unsigned)st.targetThreatBody, st.targetThreatKind, minThreatDist,
                          st.pawnPartnerDist, Runtime::PartyCombatSlotName(partnerSlot), st.partnerRole);
                logFile << l << std::endl;
            } else {
                ++s_targetMuted;
            }
        }
    } else {
        if (st.active) {
            // No shared tempo entry to clear.
            st.active = false;
        }
        st.targetThreatBody = 0;
        st.targetThreatKind[0] = 0;
        st.lastLoggedTarget = 0;
    }
}

void Tick()
{
    if (!s_enabled) return;
    Runtime::PartyCombatSnapshot party;
    if (!Runtime::ReadPartyCombatSnapshot(&party)) { Shutdown(); return; }
    bool eligibleNexus[4] = {}, eligibleGuardian[4] = {};
    for (int slot = Runtime::PARTY_MAIN; slot <= Runtime::PARTY_HIRED2; ++slot) {
        const Runtime::PartyCombatMember& m = party.member[slot];
        PawnRoleCard role;
        if (!GuardianPawnCard(slot, &role, 0) || !role.valid ||
            role.body != m.body || !m.recordValid || !m.bodyValid) continue;
        eligibleGuardian[slot] = role.eligible;
        eligibleNexus[slot] =
            role.persona == Persona::PERSONA_NEXUS &&
            Persona::Eligible(role.personaRank, role.personaValue,
                              g_guardianMinRank, g_guardianMinIncl);
    }
    for (int slot = Runtime::PARTY_MAIN; slot <= Runtime::PARTY_HIRED2; ++slot) {
        if (eligibleNexus[slot] && MeleeExecutor(party.member[slot]))
            TickOne(slot, s_state[slot], party,
                                         eligibleNexus, eligibleGuardian);
        else if (s_state[slot].actorBody || s_state[slot].partnerSlot >= 0)
            s_state[slot] = NexusState();
    }
}

} // namespace Nexus
} // namespace PawnAI

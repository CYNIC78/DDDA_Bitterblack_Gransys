#include "stdafx.h"
#include "NexusDoctrine.h"
#include "NexusPolicy.h"
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

static bool      s_enabled = true;
static bool      s_active = false;
static int       s_nexusSlot = -1;
static int       s_partnerSlot = -1;
static const char* s_partnerRole = "none";
static float     s_pawnPartnerDist = 1e9f;
static int       s_threatsInZone = 0;
static uintptr_t s_targetThreatBody = 0;
static char      s_targetThreatKind[32] = {};
static bool      s_criticalThreat = false;
static uintptr_t s_lastLoggedTarget = 0;

static float Dist3D(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Actor/anchor identity is compared only; cached pointers are never dereferenced.
static NexusPolicy::Assignment s_assignment;
static uintptr_t s_actorBody = 0, s_actorRecord = 0;
static DWORD s_lastEncounterMs = 0;
static bool s_encounterSeen = false;
static bool s_emergencyLatch[4] = {};
static uintptr_t s_candidateBody[4] = {}, s_candidateRecord[4] = {};

static void ResetAssignment()
{
    s_assignment.Reset();
    s_actorBody = s_actorRecord = 0;
    s_lastEncounterMs = 0;
    s_encounterSeen = false;
    memset(s_emergencyLatch, 0, sizeof(s_emergencyLatch));
    memset(s_candidateBody, 0, sizeof(s_candidateBody));
    memset(s_candidateRecord, 0, sizeof(s_candidateRecord));
}

static bool MeleeExecutor(const Runtime::PartyCombatMember& m)
{
    return m.recordValid && m.bodyValid && m.positionValid &&
        m.hpValid && m.currentHp > 0 && !m.downedHint &&
        (m.vocation == VOC_FIGHTER || m.vocation == VOC_WARRIOR || m.vocation == VOC_STRIDER);
}

static int SelectAnchorPartner(int mySlot, const Runtime::PartyCombatSnapshot& party, const char** outRole)
{
    const Runtime::PartyCombatMember& actor = party.member[mySlot];
    if (s_actorBody != actor.body || s_actorRecord != actor.record) {
        ResetAssignment();
        s_actorBody = actor.body; s_actorRecord = actor.record;
    }
    NexusPolicy::Candidate c[4] = {};
    const WorldReport world = CombatBus::Instance().LastWorld();
    bool encounter = false;
    int guardianSlot = -1;
    for (int slot = 1; slot <= 3; ++slot) {
        const Runtime::PartyCombatMember& m = party.member[slot];
        if (!m.recordValid || !m.bodyValid) continue;
        float incl[I_COUNT]; ReadAllIncl(incl, slot - 1);
        if (incl[I_GUARDIAN] >= 350.f && incl[I_GUARDIAN] >= incl[I_NEXUS]) {
            guardianSlot = slot; break;
        }
    }
    for (int slot = 1; slot <= 3; ++slot) {
        const Runtime::PartyCombatMember& m = party.member[slot];
        if (slot == mySlot || !m.recordValid || !m.bodyValid || !m.positionValid) {
            s_emergencyLatch[slot] = false; continue;
        }
        if (s_candidateBody[slot] != m.body || s_candidateRecord[slot] != m.record) {
            s_emergencyLatch[slot] = false;
            s_candidateBody[slot] = m.body; s_candidateRecord[slot] = m.record;
        }
        c[slot].valid = true; c[slot].body = m.body; c[slot].record = m.record;
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
        if (!m.hpValid || m.currentHp <= 0 || !nearbyThreat) s_emergencyLatch[slot] = false;
        else {
            float ratio = m.currentHp / m.maxHp;
            if (ratio < .30f) s_emergencyLatch[slot] = true;
            else if (ratio >= .45f) s_emergencyLatch[slot] = false;
        }
        c[slot].emergency = s_emergencyLatch[slot];
        if (slot != guardianSlot && m.hpValid && m.currentHp > 0 && !m.downedHint) {
            c[slot].score = m.vocation == VOC_MAGE ? 10 :
                (m.vocation == VOC_SORCERER || m.vocation == VOC_FIGHTER ||
                 m.vocation == VOC_WARRIOR) ? 30 : 20;
        }
    }
    DWORD now = MsNow();
    if (encounter) { s_lastEncounterMs = now; s_encounterSeen = true; }
    else if (s_encounterSeen && DWORD(now - s_lastEncounterMs) >= 8000) {
        s_assignment.Reset(); s_encounterSeen = false;
    }
    if (!s_encounterSeen) { *outRole = "waiting for encounter"; return -1; }
    int chosen = s_assignment.Select(c, now);
    *outRole = s_assignment.temporary.slot >= 1 ? "Emergency Cover" : "Assigned Partner";
    return chosen;
}

void Init()
{
    ResetAssignment();
    s_active = false;
    s_nexusSlot = -1;
    s_partnerSlot = -1;
    s_partnerRole = "none";
    s_pawnPartnerDist = 1e9f;
    s_threatsInZone = 0;
    s_targetThreatBody = 0;
    s_targetThreatKind[0] = 0;
    s_criticalThreat = false;
    s_lastLoggedTarget = 0;
    s_enabled = config.getBool("pawnAI", "nexusEnabled", true);

    logFile << "NexusDoctrine: initialized (ally partner bodyguard & assault wingman active)" << std::endl;
}

void Shutdown()
{
    ResetAssignment();
    s_pawnPartnerDist = 1e9f;
    s_active = false;
    s_nexusSlot = s_partnerSlot = -1;
    s_partnerRole = "none";
    s_targetThreatBody = s_lastLoggedTarget = 0;
    s_targetThreatKind[0] = 0;
    s_threatsInZone = 0;
    s_criticalThreat = false;
}

Status GetStatus()
{
    Status st;
    st.enabled = s_enabled;
    st.active = s_active;
    st.partnerRole = s_partnerRole;
    st.nexusSlot = s_nexusSlot;
    st.partnerSlot = s_partnerSlot;
    st.pawnPartnerDist = s_pawnPartnerDist;
    st.threatsInZone = s_threatsInZone;
    st.targetThreatBody = s_targetThreatBody;
    lstrcpynA(st.targetThreatKind, s_targetThreatKind, sizeof(st.targetThreatKind));
    st.criticalThreat = s_criticalThreat;
    return st;
}

void SetEnabled(bool on)
{
    s_enabled = on;
    if (!on) Shutdown();
}

void Tick()
{
    if (!s_enabled) return;
    s_active = false;
    s_threatsInZone = 0;
    s_criticalThreat = false;
    s_targetThreatBody = 0;
    s_targetThreatKind[0] = 0;

    Runtime::PartyCombatSnapshot party;
    if (!Runtime::ReadPartyCombatSnapshot(&party)) {
        Shutdown();
        return;
    }

    // 1. Ищем пешку, у которой активна склонность Nexus
    int foundNexusSlot = -1;
    uintptr_t nexusBody = 0;
    const char* nexusRoleName = "none";

    // Проверяем Главную пешку
    float mainIncl[I_COUNT];
    ReadAllIncl(mainIncl, 0);
    if (mainIncl[I_NEXUS] >= 350.0f && mainIncl[I_NEXUS] > mainIncl[I_GUARDIAN] &&
        MeleeExecutor(party.member[Runtime::PARTY_MAIN])) {
        foundNexusSlot = Runtime::PARTY_MAIN; // 1
        nexusBody = party.member[Runtime::PARTY_MAIN].body;
        nexusRoleName = "MainPawn";
    }

    // Если у Главной пешки Guardian > Nexus, проверяем наёмных пешек
    if (foundNexusSlot < 0) {
        for (int slot = Runtime::PARTY_HIRED1; slot <= Runtime::PARTY_HIRED2; ++slot) {
            const Runtime::PartyCombatMember& M = party.member[slot];
            if (!M.recordValid || !M.body) continue;
            // У наемных пешек проверяем наличие склонности Нексус
            float hIncl[I_COUNT];
            ReadAllIncl(hIncl, slot - 1);
            if (hIncl[I_NEXUS] >= 350.0f && hIncl[I_NEXUS] > hIncl[I_GUARDIAN] && MeleeExecutor(M)) {
                foundNexusSlot = slot;
                nexusBody = M.body;
                nexusRoleName = Runtime::PartyCombatSlotName(slot);
                break;
            }
        }
    }

    if (foundNexusSlot < 0 || !nexusBody) {
        Shutdown();
        s_nexusSlot = -1;
        s_partnerSlot = -1;
        s_partnerRole = "none";
        return;
    }

    s_nexusSlot = foundNexusSlot;

    // 2. Выбираем защищаемого партнера (Anchor Pawn)
    const char* selectedRole = "none";
    int partnerSlot = SelectAnchorPartner(foundNexusSlot, party, &selectedRole);
    if (partnerSlot < 1 || partnerSlot >= Runtime::PARTY_COMBAT_SLOTS) {
        s_active = false;
        s_targetThreatBody = s_lastLoggedTarget = 0;
        s_targetThreatKind[0] = 0;
        s_partnerSlot = -1;
        s_partnerRole = "no allied pawn";
        return;
    }

    if (s_partnerSlot != partnerSlot || s_partnerRole != selectedRole) {
        logFile << "NexusDoctrine: ASSIGN actor=" << foundNexusSlot
                << " primary=" << s_assignment.primary.slot
                << " temporary=" << s_assignment.temporary.slot
                << " effective=" << partnerSlot << " role=" << selectedRole << std::endl;
    }
    s_partnerSlot = partnerSlot;
    s_partnerRole = selectedRole;

    const Runtime::PartyCombatMember& partner = party.member[partnerSlot];
    const uintptr_t partnerBody = partner.body;
    if (!partnerBody) return;

    // Читаем координаты партнера и пешки с Нексусом
    float ax = 0, ay = 0, az = 0;
    float px = 0, py = 0, pz = 0;
    if (!Rd((const void*)(partnerBody + 0x40), &ax, 4) ||
        !Rd((const void*)(partnerBody + 0x44), &ay, 4) ||
        !Rd((const void*)(partnerBody + 0x48), &az, 4))
        return;

    if (!Rd((const void*)(nexusBody + 0x40), &px, 4) ||
        !Rd((const void*)(nexusBody + 0x44), &py, 4) ||
        !Rd((const void*)(nexusBody + 0x48), &pz, 4))
        return;

    s_pawnPartnerDist = Dist3D(px, py, pz, ax, ay, az) / 100.0f; // метры

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

    s_threatsInZone = inZone;
    s_criticalThreat = criticalThreat;

    // 4. Реализация перехвата и удержания строя
    const bool withinLeash = (s_pawnPartnerDist <= 18.0f);

    if (inZone > 0 && bestThreatBody && withinLeash && !PawnAI::Rescue::IsActive()) {
        s_active = true;
        s_targetThreatBody = bestThreatBody;
        lstrcpynA(s_targetThreatKind, bestThreatKind, sizeof(s_targetThreatKind));

        // Направляем боевую цель планировщика (uCmc+0x2EB8) и взгляд (+0x14E0) на угрозу
        uintptr_t readback = 0;
        if (!Runtime::Mem::RegionOk(nexusBody + 0x2EB8, sizeof(uintptr_t)) ||
            !WrSafe((void*)(nexusBody + 0x2EB8), &s_targetThreatBody, sizeof(uintptr_t)) ||
            !Rd((void*)(nexusBody + 0x2EB8), &readback, sizeof(readback)) ||
            readback != s_targetThreatBody) {
            s_active = false;
            return;
        }
        // Leave gaze to the engine: avoid a second, non-atomic write.

        // Даем скоростной рывок для перехвата
        // No shared tempo override: Nexus cannot safely own/clear another module's entry.

        if (s_lastLoggedTarget != s_targetThreatBody) {
            s_lastLoggedTarget = s_targetThreatBody;
            char l[256];
            sprintf_s(l, "NexusDoctrine: [%s] PROACTIVE TARGET -> %s 0x%08X (%s) dist=%.1fm (pawn-partner=%.1fm, partner: %s [%s])",
                      nexusRoleName, criticalThreat ? "CRITICAL-MELEE" : "PREEMPT-INTERCEPT",
                      (unsigned)s_targetThreatBody, s_targetThreatKind, minThreatDist,
                      s_pawnPartnerDist, Runtime::PartyCombatSlotName(partnerSlot), s_partnerRole);
            logFile << l << std::endl;
        }
    } else {
        if (s_active) {
            // No shared tempo entry to clear.
            s_active = false;
        }
        s_targetThreatBody = 0;
        s_targetThreatKind[0] = 0;
        s_lastLoggedTarget = 0;
    }
}

} // namespace Nexus
} // namespace PawnAI

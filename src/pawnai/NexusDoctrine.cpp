#include "stdafx.h"
#include "NexusDoctrine.h"
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

// Выбор лучшего партнера для пешки с Нексусом среди остальных пешек партии
static int SelectAnchorPartner(int mySlot, const Runtime::PartyCombatSnapshot& party, const char** outRole)
{
    int bestPartner = -1;
    *outRole = "none";

    // 1. Проверяем наличие союзника с критическим здоровьем (< 35% HP)
    for (int slot = 1; slot < Runtime::PARTY_COMBAT_SLOTS; ++slot) {
        if (slot == mySlot) continue;
        const Runtime::PartyCombatMember& M = party.member[slot];
        if (!M.recordValid || !M.body || !M.hpValid) continue;
        if (M.currentHp > 0.0f && M.maxHp > 0.0f) {
            float hpRatio = M.currentHp / M.maxHp;
            if (hpRatio < 0.35f) {
                *outRole = "Vulnerable Shield";
                return slot;
            }
        }
    }

    // 2. Ищем кастера в партии (Mage = 3, Sorcerer = 9) для защиты кастов
    for (int slot = 1; slot < Runtime::PARTY_COMBAT_SLOTS; ++slot) {
        if (slot == mySlot) continue;
        const Runtime::PartyCombatMember& M = party.member[slot];
        if (!M.recordValid || !M.body) continue;
        if (M.vocation == VOC_MAGE || M.vocation == VOC_SORCERER) {
            *outRole = "Backline Protector (Caster)";
            return slot;
        }
    }

    // 3. Ищем штурмового напарника (Fighter = 1, Strider = 2, Warrior = 7, Ranger = 8)
    for (int slot = 1; slot < Runtime::PARTY_COMBAT_SLOTS; ++slot) {
        if (slot == mySlot) continue;
        const Runtime::PartyCombatMember& M = party.member[slot];
        if (!M.recordValid || !M.body) continue;
        *outRole = "Assault Pair (Wingman)";
        return slot;
    }

    return bestPartner;
}

void Init()
{
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
    if (s_active && s_nexusSlot >= 1 && s_nexusSlot <= 3) {
        uintptr_t b = 0;
        if (Runtime::PartyRecordInfo(s_nexusSlot - 1, 0, 0, &b) && b) {
            Runtime::Tempo::ClearOverride(b);
        }
    }
    s_active = false;
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

    Runtime::PartyCombatSnapshot party;
    if (!Runtime::ReadPartyCombatSnapshot(&party)) {
        if (s_active) Shutdown();
        return;
    }

    // 1. Ищем пешку, у которой активна склонность Nexus
    int foundNexusSlot = -1;
    uintptr_t nexusBody = 0;
    const char* nexusRoleName = "none";

    // Проверяем Главную пешку
    float mainIncl[I_COUNT];
    ReadAllIncl(mainIncl, 0);
    if (mainIncl[I_NEXUS] >= 350.0f && mainIncl[I_NEXUS] >= mainIncl[I_GUARDIAN]) {
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
            if (hIncl[I_NEXUS] >= 350.0f && hIncl[I_NEXUS] >= hIncl[I_GUARDIAN]) {
                foundNexusSlot = slot;
                nexusBody = M.body;
                nexusRoleName = Runtime::PartyCombatSlotName(slot);
                break;
            }
        }
    }

    if (foundNexusSlot < 0 || !nexusBody) {
        if (s_active) Shutdown();
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
        if (s_active) Shutdown();
        s_partnerSlot = -1;
        s_partnerRole = "no allied pawn";
        return;
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
            if (inCriticalMelee || d < minThreatDist) {
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

    if (inZone > 0 && bestThreatBody && withinLeash) {
        s_active = true;
        s_targetThreatBody = bestThreatBody;
        lstrcpynA(s_targetThreatKind, bestThreatKind, sizeof(s_targetThreatKind));

        // Направляем боевую цель планировщика (uCmc+0x2EB8) и взгляд (+0x14E0) на угрозу
        WrSafe((void*)(nexusBody + 0x2EB8), &s_targetThreatBody, sizeof(uintptr_t));
        WrSafe((void*)(nexusBody + 0x14E0), &s_targetThreatBody, sizeof(uintptr_t));

        // Даем скоростной рывок для перехвата
        Runtime::Tempo::SetOverride(nexusBody, 1.25f, 1.15f, 2500);

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
            Runtime::Tempo::ClearOverride(nexusBody);
            s_active = false;
        }
        s_targetThreatBody = 0;
        s_targetThreatKind[0] = 0;
        s_lastLoggedTarget = 0;
    }
}

} // namespace Nexus
} // namespace PawnAI

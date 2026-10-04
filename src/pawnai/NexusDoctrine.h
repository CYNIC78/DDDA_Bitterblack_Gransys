#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
#include "../runtime/Runtime.h"

/** Nexus: sticky partner + temporary emergency cover.
 * Build 85.08 contract and limitations: docs/NEXUS_DOCTRINE.md.
 * Melee executors only; no movement, gaze or tempo override ownership.
 */

namespace PawnAI {
namespace Nexus {

void Init();
void Shutdown();
void Tick();

struct Status {
    bool        enabled;
    bool        active;
    const char* partnerRole;      // "Backline Protector" / "Assault Wingman" / "Vulnerable Shield"
    int         nexusSlot;        // слот пешки с Нексусом (1..3)
    int         partnerSlot;      // слот защищаемого союзника (1..3)
    float       pawnPartnerDist;  // дистанция между ними (м)
    int         threatsInZone;
    uintptr_t   targetThreatBody;
    char        targetThreatKind[32];
    bool        criticalThreat;
};

Status GetStatus(); // compatibility: first active/assigned Nexus
Status GetStatusFor(int slot); // independent per-pawn status (1..3)
void   SetEnabled(bool on);
// 85.63: сводка сессии (замеры и объявления цели) — одной строкой в полевой
// пакет. Зовётся при выгрузке, до печати пакета.
void   SessionSummary();

} // namespace Nexus
} // namespace PawnAI

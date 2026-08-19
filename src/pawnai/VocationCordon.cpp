// PawnAI::VocationCordon — вокационный кордон Guardian. См. VocationCordon.h.

#include "stdafx.h"
#include "VocationCordon.h"
#include "PawnAI_Common.h"

namespace PawnAI {

void VocationCordon::Init()
{
    enabled            = config.getBool("pawnAI", "vocationCordon", true);
    guardianCapRanged  = config.getFloat("pawnAI", "cordonGuardianCapRanged", 300.0f);
    pioneerFloorRanged = config.getFloat("pawnAI", "cordonPioneerFloorRanged", 650.0f);
    guardianCapHybrid  = config.getFloat("pawnAI", "cordonGuardianCapHybrid", 550.0f);
    pioneerFloorHybrid = config.getFloat("pawnAI", "cordonPioneerFloorHybrid", 550.0f);

    char l[200];
    sprintf_s(l, "VocationCordon: %s, ranged cap %.0f / floor %.0f, hybrid cap %.0f / floor %.0f",
              enabled ? "enabled" : "disabled",
              guardianCapRanged, pioneerFloorRanged,
              guardianCapHybrid, pioneerFloorHybrid);
    logFile << l << std::endl;
}

void VocationCordon::GetDelta(const float* target, float* delta)
{
    lastAction = "idle";
    lastGuardianCap = 0.0f;
    if (!enabled || !target || !delta) return;
    if (!pBase || !*pBase) { lastClassName = "?"; return; }

    const uintptr_t pawnRec = (uintptr_t)(*pBase) + PLAYER_BASE + PAWN_OFFSET;
    const int voc = ReadVocation(pawnRec);
    lastVocation = voc;

    const VocationClass vc = VocationClassOf(voc);
    lastClassName = VocationClassName(vc);

    float cap = 0.0f, floorP = 0.0f;
    switch (vc) {
        case VCL_RANGED:
        case VCL_CASTER:
            cap = guardianCapRanged;  floorP = pioneerFloorRanged;
            lastAction = "full cordon";
            break;
        case VCL_HYBRID:
            cap = guardianCapHybrid;  floorP = pioneerFloorHybrid;
            lastAction = "partial cordon";
            break;
        case VCL_MELEE:
            lastAction = "melee - guardian left alone";
            return;
        default:
            lastAction = "unknown vocation - no change";
            return;
    }

    lastGuardianCap = cap;

    // ПОТОЛОК, А НЕ ВЫЧИТАНИЕ. Дельта считается так, чтобы итог встал
    // ровно на потолок: если игрок и так держит Guardian ниже — молчим.
    const float g = target[I_GUARDIAN];
    if (g > cap) delta[I_GUARDIAN] += (cap - g);

    // ПОЛ для Pioneer — тот же приём с другой стороны. Это грубый рычаг
    // «не липни к якорю»; настоящее позиционирование требует реверса
    // цели Follow.
    const float p = target[I_PIONEER];
    if (p < floorP) delta[I_PIONEER] += (floorP - p);
}

} // namespace PawnAI

#pragma once
#include "PawnAI_Common.h"
#include "../CombatBus.h"
#include "../runtime/Runtime.h"

/**
 * NexusDoctrine — поведенческая доктрина инклинации Nexus.
 *
 * ЗЕРКАЛО GUARDIAN DOCTRINE, но якорем защиты выступает СОЮЗНАЯ ПЕШКА.
 *
 * РОЛИ ПАРТНЁРА (Anchor Selection):
 *   1. Защита тыла / кастов (Backline Protector):
 *      Если в партии есть Маг или Чародей (Mage / Sorcerer), Nexus-пешка
 *      выбирает её своим VIP-якорем, охраняет процесс каста и перехватывает
 *      фланкеров, бегущих сбивать заклинание (PLAYER-CHANT-HARASS).
 *
 *   2. Штурмовая двойка (Assault Pair / Wingman):
 *      Если кастера нет (партия из мечников/лучников), Nexus выбирает
 *      ведущего бойца и действует как напарник в штурмовой двойке:
 *      наступает вместе с ним, атакует общую цель и прикрывает спину.
 *
 *   3. Защита уязвимого (Vulnerable / Low HP Partner):
 *      Если у союзника критически падает здоровье (< 35% HP), Nexus
 *      динамически прикрывает раненого бойца.
 *
 * ДВУХУРОВНЕВЫЙ ПЕРИМЕТР (Two-Tier Bodyguard Perimeter):
 *   - Critical Melee (4–6 м вокруг партнёра): жесткий перехват любой цели в упор.
 *   - Preempt Intercept (6–12 м): упреждающий перехват врагов, нацеленных на партнёра.
 *   - Поводок безопасности (14–18 м от партнёра): возврат в строй.
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

Status GetStatus();
void   SetEnabled(bool on);

} // namespace Nexus
} // namespace PawnAI

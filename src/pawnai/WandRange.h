#pragma once
/**
 * PawnAI::WandRange — эррата дальности посоха у пешки (слой B) + CasterWatch.
 *
 * ЗАЧЕМ. У мага/чародея нет даша и брони. Короткий AI-RangeMax (5–10 м
 * против 5–15 м у лука) заставляет пешку шагать за целью в пак.
 * Референс — расширенный радиус чародея в DDON. Игрока не трогаем.
 *
 * ЧТО ПИШЕМ. Живой cCmc* +0x258: RangeMax 1–10 м -> 15 м, EnableMax
 * -> 20 м. Имена: Wand/Magic/Lightning/Fire/Healing/Cure/Circle/…
 * Не трогаем: IceWalk (ауры Frigor), Bow, Dagger, Slash, StandOff.
 *
 * ВЕРСИЯ 84.56: CasterWatch & Holy Focused Bolt (HFB) Live Tracker:
 * - Real-time tracking of Focused Bolt charges, releases, durations.
 * - Real-time tracking of spell chants, releases, interruptions.
 * - Elemental buff tracking (Holy Affinity/Boon -> HFB, Fire, Ice, Thunder, Dark).
 * - Nuke gating transition logging (GATED vs UNLOCKED).
 */

#include <stdint.h>

namespace PawnAI {
namespace WandRange {

void Init();
void Tick();
void Restore(const char* why);
void Shutdown();

enum ElementType {
    ELEM_NONE = 0,
    ELEM_HOLY,
    ELEM_FIRE,
    ELEM_ICE,
    ELEM_LIGHTNING,
    ELEM_THUNDER = ELEM_LIGHTNING,
    ELEM_DARK
};

struct Status {
    bool        enabled;
    bool        applied;
    int         nResource;
    int         nLive;
    bool        nukeGated;
    uint32_t    boltsCharged;
    uint32_t    boltsFired;
    uint32_t    spellsChanted;
    uint32_t    spellsCompleted;
    ElementType activeBuff;
    uint32_t    buffRemainingSec;
    char        lastCasterEvent[128];
    char        why[80];
};

Status Get();
void   SetEnabled(bool on);
bool   NukeGatingOn();
void   SetNukeGating(bool on);
void   SetPartyElementBuff(ElementType elem, uint32_t durationMs = 90000);

} // namespace WandRange
} // namespace PawnAI

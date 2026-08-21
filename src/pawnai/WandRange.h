#pragma once
/**
 * PawnAI::WandRange — эррата дальности посоха у пешки (слой B).
 *
 * ЗАЧЕМ. У мага/чародея нет даша и брони. Короткий AI-RangeMax (5–10 м
 * против 5–15 м у лука) заставляет пешку шагать за целью в пак.
 * Референс — расширенный радиус чародея в DDON. Игрока не трогаем.
 *
 * ЧТО ПИШЕМ. Живой cCmc* +0x258: RangeMax 1–10 м -> 15 м, EnableMax
 * -> 20 м. Имена: Wand/Magic/Lightning/Fire/Healing/Cure/Circle/…
 * Не трогаем: IceWalk (ауры Frigor), Bow, Dagger, Slash, StandOff.
 *
 * КУДА. Все uCmc. Цепочка как у эрраты: cAICtrl -> iface -> cCmc,
 * плюс PlanCtrl(55) = WpnWandAtk.
 *
 * validate -> write -> readback; откат по флагу, выгрузке, меню.
 */

namespace PawnAI {
namespace WandRange {

void Init();
void Tick();
void Restore(const char* why);
void Shutdown();

struct Status {
    bool  enabled;
    bool  applied;
    int   nResource;
    int   nLive;
    char  why[80];
};
Status Get();
void   SetEnabled(bool on);

} // namespace WandRange
} // namespace PawnAI

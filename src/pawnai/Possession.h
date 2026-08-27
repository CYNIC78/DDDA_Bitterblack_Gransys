#pragma once
/**
 * PawnAI::Possession — primitive слоя E + watch слоя C (SoT §12.1.2).
 *
 * 84.31 poke ≠ apply. 84.34 thiscall id=7. 84.35 без воды.
 * 84.37: xmm timer/p0/p1 только на НАШЕМ Set (как HBuffMods).
 *         Не Drake. Не id≠7. Не revive. Не каталог на диске.
 */
namespace PawnAI {
namespace Possession {

void Init();
void Tick();
void Shutdown();

void RequestApply();
void RequestClear();

struct Status {
    bool  armed;
    bool  applied;
    bool  watching;
    bool  held;
    bool  hookArmed;
    bool  layout;
    bool  recipe;
    bool  customOn;
    bool  targetArisen;
    int   selectedId;
    int   slot;
    int   liveId;
    float liveTimer;
    float liveP0;
    float liveP1;
    int   liveCount;
    float customT;
    float customP0;
    float customP1;
    char  why[96];
};
Status Get();
void   SetArmed(bool on);
void   SetCustom(bool on);
void   SetCustomTimer(float seconds);
void   SetCustomP0(float v);
void   SetCustomP1(float v);
void   SetSelectedId(int id);
void   SetTargetArisen(bool on);

} // namespace Possession
} // namespace PawnAI

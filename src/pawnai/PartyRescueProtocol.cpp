#include "stdafx.h"
#include "PartyRescueProtocol.h"
#include "PawnAI_Common.h"
#include "runtime/Runtime.h"
#include "runtime/MemProbe.h"
#include "runtime/MonsterTempo.h"
#include "../CombatBus.h"
#include <math.h>
#include <stdio.h>

namespace PawnAI {
namespace Rescue {

using Runtime::Mem::Rd;
using Runtime::Mem::WrSafe;

static bool      s_enabled = true;
static bool      s_active = false;
static uintptr_t s_captorBody = 0;
static char      s_captorKind[32] = {};
static char      s_crisisReason[64] = {};
static DWORD     s_crisisStartMs = 0;
static uint32_t  s_crisisCount = 0;

static float Dist3D(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static bool IsArisenVictimState(const char* act)
{
    if (!act || !act[0]) return false;
    return strstr(act, "Restraint") != nullptr
        || strstr(act, "DmgDown") != nullptr
        || strstr(act, "Lifted") != nullptr
        || strstr(act, "TramplePl") != nullptr
        || strstr(act, "Neardeath") != nullptr
        || !strcmp(act, "cPlActDmgRestraint")
        || !strcmp(act, "cPlActRestraint")
        || !strcmp(act, "cPlActDmgDown")
        || !strcmp(act, "cPlActDmgDownDamage")
        || !strcmp(act, "cPlActDmgDownDead")
        || !strcmp(act, "cPlActDwnAtTramplePl");
}

static bool IsMonsterCaptorAct(const char* act)
{
    if (!act || !act[0]) return false;
    // DmgRestraint — это монстр, которого держит пешка. Не путать с захватом Аризена!
    if (strstr(act, "DmgRestraint")) return false;

    return strstr(act, "CatchPl") != nullptr
        || strstr(act, "EatCatch") != nullptr
        || strstr(act, "CarryCatchPl") != nullptr
        || strstr(act, "CatchSuccess") != nullptr
        || strstr(act, "DwnAtTramplePl") != nullptr
        || strstr(act, "DownBite") != nullptr
        || strstr(act, "HangCatch") != nullptr
        || strstr(act, "HpDrainCatchPl") != nullptr
        || strstr(act, "BigDownCatch") != nullptr
        || strstr(act, "EatPl") != nullptr;
}

void Init()
{
    s_active = false;
    s_captorBody = 0;
    s_captorKind[0] = 0;
    s_crisisReason[0] = 0;
    s_crisisStartMs = 0;
    s_crisisCount = 0;
    logFile << "PartyRescueProtocol: initialized (all-pawn emergency rescue ready)" << std::endl;
}

void Shutdown()
{
    if (s_active) {
        // Снимаем оверрайды со всех пешек
        const uintptr_t mainPawn = Runtime::MainPawnBody();
        if (mainPawn) Runtime::Tempo::ClearOverride(mainPawn);
        const int nPawns = Runtime::PawnBodyCount();
        for (int i = 0; i < nPawns; ++i) {
            bool isMain = false;
            uintptr_t b = Runtime::PawnBodyAt(i, &isMain);
            if (b && !isMain) Runtime::Tempo::ClearOverride(b);
        }
        s_active = false;
    }
}

bool IsActive()                { return s_active; }
uintptr_t CurrentCaptorBody()  { return s_captorBody; }
const char* CurrentCaptorKind() { return s_captorKind; }
const char* CurrentCrisisReason() { return s_crisisReason; }
uint32_t CrisisCount()         { return s_crisisCount; }

void Tick()
{
    if (!s_enabled) return;

    const uintptr_t arisen = Runtime::ArisenBody();
    if (!arisen) {
        if (s_active) Shutdown();
        return;
    }

    float ax = 0, ay = 0, az = 0;
    if (!Runtime::GetArisenWorldPos(&ax, &ay, &az)) return;

    char arisenAct[48] = {};
    Runtime::ReadLiveAct(arisen, arisenAct, sizeof(arisenAct));
    const bool arisenInCrisis = IsArisenVictimState(arisenAct);

    // Поиск врага-захватчика среди живых монстров рядом с Аризеном
    uintptr_t foundCaptor = 0;
    char foundCaptorKind[32] = {};
    char foundCrisisReason[64] = {};
    float minCaptorDist = 1e9f;

    const int nEnemies = Runtime::EnemyCount();
    for (int i = 0; i < nEnemies; ++i) {
        const char* kind = nullptr;
        uintptr_t eBody = Runtime::EnemyBodyAt(i, &kind);
        if (!eBody) continue;

        float ex = 0, ey = 0, ez = 0;
        if (!Rd((const void*)(eBody + 0x40), &ex, 4) ||
            !Rd((const void*)(eBody + 0x44), &ey, 4) ||
            !Rd((const void*)(eBody + 0x48), &ez, 4))
            continue;

        float d = Dist3D(ax, ay, az, ex, ey, ez) / 100.0f; // метры
        if (d > 4.5f) continue; // захват происходит вплотную (< 4.5 м)

        char eAct[48] = {};
        Runtime::ReadLiveAct(eBody, eAct, sizeof(eAct));
        const bool monsterCaptor = IsMonsterCaptorAct(eAct);

        if (monsterCaptor || (arisenInCrisis && d < 3.0f)) {
            if (d < minCaptorDist) {
                minCaptorDist = d;
                foundCaptor = eBody;
                lstrcpynA(foundCaptorKind, kind ? kind : "enemy", sizeof(foundCaptorKind));
                if (monsterCaptor)
                    sprintf_s(foundCrisisReason, "captor act %s", eAct[0] ? eAct : "?");
                else
                    sprintf_s(foundCrisisReason, "Arisen in %s", arisenAct[0] ? arisenAct : "?");
            }
        }
    }

    const DWORD now = MsNow();

    if (foundCaptor) {
        // --- ЭКСТРЕННАЯ СИТУАЦИЯ: АРИЗЕН В ЗАХВАТЕ / ПОД УДАРОМ ---
        s_captorBody = foundCaptor;
        lstrcpynA(s_captorKind, foundCaptorKind, sizeof(s_captorKind));
        lstrcpynA(s_crisisReason, foundCrisisReason, sizeof(s_crisisReason));

        if (!s_active) {
            s_active = true;
            s_crisisStartMs = now;
            ++s_crisisCount;
            char l[256];
            sprintf_s(l, "PartyRescueProtocol: EMERGENCY RESCUE ENGAGED -> %s, captor 0x%08X (%s) dist=%.1fm - all pawns intercepting!",
                      s_crisisReason, (unsigned)s_captorBody, s_captorKind, minCaptorDist);
            logFile << l << std::endl;
        }

        // 1. Главная пешка: жесткий таргет + ускорение + снятие штрафов
        const uintptr_t mainPawn = Runtime::MainPawnBody();
        if (mainPawn) {
            WrSafe((void*)(mainPawn + 0x2EB8), &s_captorBody, sizeof(uintptr_t));
            WrSafe((void*)(mainPawn + 0x14E0), &s_captorBody, sizeof(uintptr_t));
            Runtime::Tempo::SetOverride(mainPawn, 1.30f, 1.25f, 3000);
        }

        // 2. Наёмные пешки: всем пиним captorBody в боевую цель и даем спринт!
        const int nPawns = Runtime::PawnBodyCount();
        for (int i = 0; i < nPawns; ++i) {
            bool isMain = false;
            uintptr_t b = Runtime::PawnBodyAt(i, &isMain);
            if (b && !isMain) {
                WrSafe((void*)(b + 0x2EB8), &s_captorBody, sizeof(uintptr_t));
                WrSafe((void*)(b + 0x14E0), &s_captorBody, sizeof(uintptr_t));
                Runtime::Tempo::SetOverride(b, 1.30f, 1.25f, 3000);
            }
        }
    } else if (s_active && !arisenInCrisis) {
        // --- АРИЗЕН ОСВОБОЖДЕН / КРИЗИС ЗАВЕРШЕН ---
        const DWORD dur = s_crisisStartMs ? (now - s_crisisStartMs) : 0;
        char l[220];
        sprintf_s(l, "PartyRescueProtocol: Arisen FREE / RESCUED -> captor 0x%08X neutralized in %ums",
                  (unsigned)s_captorBody, (unsigned)dur);
        logFile << l << std::endl;

        // Снимаем оверрайды темпа с пешек
        const uintptr_t mainPawn = Runtime::MainPawnBody();
        if (mainPawn) Runtime::Tempo::ClearOverride(mainPawn);

        const int nPawns = Runtime::PawnBodyCount();
        for (int i = 0; i < nPawns; ++i) {
            bool isMain = false;
            uintptr_t b = Runtime::PawnBodyAt(i, &isMain);
            if (b && !isMain) Runtime::Tempo::ClearOverride(b);
        }

        s_active = false;
        s_captorBody = 0;
        s_captorKind[0] = 0;
        s_crisisReason[0] = 0;
    }
}

} // namespace Rescue
} // namespace PawnAI

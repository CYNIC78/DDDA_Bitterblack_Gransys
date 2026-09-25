// Проверка блока «Party roles» (85.12) на настоящем imgui.h 1.48.
//
// ЗАЧЕМ ОТДЕЛЬНЫЙ ФАЙЛ, А НЕ ui_pawn_t.cpp. Блок ролей живёт в начале
// RenderPawnAIUI, а проверяемый кусок панели склонностей — ниже по той же
// функции. Расширить старую метку до него нельзя: тогда в проверку попадут
// Possession, Cordon, CombatBus и SEH, двойников которым в фикстуре нет.
// Своя пара меток ([UI-BLOCK-ROLES-BEGIN/END]) вырезает ровно сорок строк.
//
// Блок вырезается из PawnAI.cpp скриптом tools/syntax_check.sh.
#include <windows.h>
#include <stdio.h>
#include "ImGui/imgui.h"

// --- двойники того, что блок ожидает от модуля ----------------------------

namespace Runtime {
    enum PartyCombatSlot { PARTY_ARISEN = 0, PARTY_MAIN = 1,
                           PARTY_HIRED1 = 2, PARTY_HIRED2 = 3 };
    inline const char* PartyCombatSlotName(int) { return "?"; }
}

static const char* VocationName(int) { return "?"; }

// Поля обязаны совпадать с PawnRoleCard / GuardianLiveCard из
// GuardianDoctrine.h. Если там что-то переименуют или изменят тип, проверка
// панели упадёт здесь, а не у тестера на сборке в MSVC.
struct PawnRoleCard {
    int slot; int vocation; uintptr_t body;
    float guardianIncl; float nexusIncl;
    int persona; int personaRank; float personaValue;
    bool eligible; bool valid;
};
struct GuardianLiveCard {
    bool valid;
    float pawnAnchorDist; float pawnEnemyDist; float anchorEnemyDist; float bearing;
    int zoneEngaged; int threatsInZone; uintptr_t target;
    char act[48]; int32_t code; char goal[32];
};

static bool  GuardianPawnCard(int, PawnRoleCard*, GuardianLiveCard*) { return false; }
static const char* GuardianPersonaName(int) { return "-"; }
static int   g_guardianMinRank = 1;
static float g_guardianMinIncl = 350.0f;

void UiRolesBlockTest()
{
#include "ui_roles_block.inc"
}

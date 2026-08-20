// Проверка UI-блока панели пешек на настоящем imgui.h 1.48.
// Блок вырезается из PawnAI.cpp скриптом syntax_check.sh.
#include <windows.h>
#include <stdio.h>
#include "ImGui/imgui.h"

// Двойники того, что блок ожидает от модуля.
enum { I_SCATHER = 0, I_GUARDIAN = 5, I_CHALLENGER = 3, I_COUNT = 10 };
enum InclCat { CAT_USEFUL, CAT_NEUTRAL, CAT_JUNK, CAT_DOCTRINE };
static InclCat GetInclCategory(int) { return CAT_NEUTRAL; }
static bool  g_hiredInclUnlocked = false;
static bool  g_hiredInclShow = false;
static bool  g_hiChanged[4] = {};
static float g_hiBase[4][I_COUNT];
static void  HiredInclRestoreAll() {}
static void  HiredInclRestore(int) {}
static void  HiredInclRestoreAll2() {}
static void  HiredInclCapture(int) {}
static void  HiredInclSelfTestStart(int) {}
static bool  HiredRecordOk(int, int*, int*) { return false; }
static const char* VocationName(int) { return "?"; }
static const char* InclName(int) { return "?"; }
static void  ReadAllIncl(float*, int) {}
namespace Runtime {
    inline unsigned PawnBodyAt(int, bool*) { return 0; }
    inline bool PawnInclinationsLive(unsigned, float*) { return false; }
    inline bool PawnSetInclinationLive(unsigned, int, float) { return false; }
}
static void  WriteAllIncl(const float*, int) {}

void UiPawnBlockTest()
{
#include "ui_pawn_block.inc"
}

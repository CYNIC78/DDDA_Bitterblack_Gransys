// Проверка UI-блока пробы на настоящем imgui.h 1.48.
// Блок вырезается из DevTools.cpp скриптом syntax_check.sh.
#include <windows.h>
#include "ImGui/imgui.h"
#include "../../src/devtools/AnimProbe.h"
void UiBlockTest()
{
#include "ui_block.inc"
}

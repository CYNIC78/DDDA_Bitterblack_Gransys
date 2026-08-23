// Syntax-check Enemy AI panel against the repository's ImGui 1.48 API.
#include "director_stdafx.h"
#include "../../ImGui/imgui.h"
namespace Hooks { void InGameUIAdd(void (*fn)()); }
#include "../../src/EnemyAI.cpp"

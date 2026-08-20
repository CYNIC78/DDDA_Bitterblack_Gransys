#!/bin/sh
# Синтаксическая проверка без Visual Studio.
#
# ЗАЧЕМ. MSVC есть только у тестера, и каждая ошибка компиляции стоит целой
# итерации: сборка, запуск игры, лог. g++ ловит опечатки, несуществующие
# сигнатуры ImGui 1.48 и неразрешённые имена за секунду.
#
# ЧТО ПРОВЕРЯЕТСЯ:
#   1. src/devtools/AnimProbe.cpp целиком (со шимом windows.h);
#   1g. src/pawnai/GuardianDoctrine.cpp (SEH подменён на try/catch);
#   2. src/monsterai/MonsterDirector.cpp (шим ini/лога);
#   3. UI-блок пробы из DevTools.cpp на настоящем imgui.h;
#   4. не-ASCII в строках, попадающих в ImGui (рисуются как '?').
#
# Запуск: sh tools/syntax_check.sh   (из корня репозитория)
set -e
ROOT=$(pwd)
T="$ROOT/tools/tcomp"
GPP="g++ -std=c++11 -fsyntax-only -I$T -I$T/shim -I$ROOT"

echo "== 1/9 AnimProbe.cpp =="
$GPP "$T/animprobe_t.cpp"

echo "== 1b/9 MonsterDirector.cpp =="
$GPP "$T/director_t.cpp"

echo "== 1c/9 PawnHaste.cpp =="
$GPP "$T/pawnhaste_t.cpp"

echo "== 1d/9 VocationCordon.cpp =="
$GPP "$T/cordon_t.cpp"

echo "== 1e/9 DashWatch.cpp =="
$GPP "$T/dashwatch_t.cpp"

echo "== 1f/9 GoapProbe.cpp =="
$GPP "$T/goap_t.cpp"

echo "== 1g/9 GuardianDoctrine.cpp =="
# SEH под g++ нет: __try/__except подменяем на try/catch. Проверяется не
# поведение обработчика, а синтаксис тела — этого и хотим.
$GPP -Isrc "-D__try=try" "-D__except(x)=catch(...)" "$T/guard_t.cpp"

echo "== 2/9 UI block =="
python3 - <<'PY'
p = 'src/devtools/DevTools.cpp'
s = open(p, encoding='utf-8').read()
start = s.index('    // --- \u043e\u0445\u043e\u0442\u0430 \u0437\u0430 \u043c\u043d\u043e\u0436\u0438\u0442\u0435\u043b\u0435\u043c \u0442\u0435\u043c\u043f\u0430 \u0430\u043d\u0438\u043c\u0430\u0446\u0438\u0438 ---')
end = s.index('    ImGui::Spacing();\n\n    // ================= Player + Main Pawn recon', start)
open('tools/tcomp/ui_block.inc', 'w', encoding='utf-8').write(s[start:end])
PY
$GPP "$T/ui_t.cpp"

echo "== 2b/9 UI block (PawnAI.cpp) =="
# ЗАЧЕМ ЕЩЁ ОДИН БЛОК. Панель пешек живёт в PawnAI.cpp и до 75.2 не
# проверялась вовсе: ошибки в вызовах ImGui там ловились только сборкой у
# тестера, то есть ценой итерации. Проверяем тот же кусок тем же способом.
python3 - <<'PY'
p = 'src/PawnAI.cpp'
s = open(p, encoding='utf-8').read()
start = s.index('\n', s.index('[UI-BLOCK-PAWN-BEGIN]')) + 1
end = s.rindex('\n', start, s.index('[UI-BLOCK-PAWN-END]', start))
open('tools/tcomp/ui_pawn_block.inc', 'w', encoding='utf-8').write(s[start:end])
PY
$GPP "$T/ui_pawn_t.cpp"

echo "== 9/9 ASCII in UI strings =="
python3 - <<'PY'
import re, glob, sys
pat = re.compile(r'ImGui::(?:Text|Button|TextColored|TextWrapped|TextDisabled'
                 r'|CollapsingHeader|TreeNode|Checkbox|SliderFloat|RadioButton'
                 r'|Selectable|MenuItem|BeginMenu|LabelText|BulletText'
                 r'|SmallButton|InputFloat|SetTooltip)')
bad = 0
for p in glob.glob('src/**/*.cpp', recursive=True) + glob.glob('*.cpp'):
    for i, l in enumerate(open(p, encoding='utf-8'), 1):
        code = l.split('//')[0]
        if pat.search(code) and any(ord(c) > 127 for c in code):
            print(' ', p, i, code.strip()[:80]); bad += 1
sys.exit(1 if bad else 0)
PY
echo "ALL CLEAN"

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
#   2t. src/runtime/MonsterTempo.cpp (dedicated portable shim);
#   1i2. src/runtime/PartyStatus.cpp (84.16 dual-observe, read-only);
#   3. UI-блок пробы из DevTools.cpp на настоящем imgui.h;
#   4. не-ASCII в строках, попадающих в ImGui (рисуются как '?').
#
# Запуск: sh tools/syntax_check.sh   (из корня репозитория)
set -e
ROOT=$(pwd)
T="$ROOT/tools/tcomp"
GPP="g++ -std=c++11 -fsyntax-only -I$T -I$T/shim -I$ROOT"

echo "== 1/10 AnimProbe.cpp =="
$GPP "$T/animprobe_t.cpp"

echo "== 1b/10 MonsterDirector.cpp =="
$GPP "$T/director_t.cpp"

echo "== 1k/10 PackObserve.cpp =="
$GPP -DDDDA_PACKOBSERVE_PORTABLE "$T/packobserve_t.cpp"

echo "== 1j/10 MonsterTempo.cpp =="
$GPP -DDDDA_TEMPO_PORTABLE_FIXTURE "$ROOT/src/runtime/MonsterTempo.cpp"

echo "== 1c/10 PawnHaste.cpp =="
$GPP "$T/pawnhaste_t.cpp"

echo "== 1d/10 VocationCordon.cpp =="
$GPP "$T/cordon_t.cpp"

echo "== 1e/10 DashWatch.cpp =="
$GPP "$T/dashwatch_t.cpp"

echo "== 1w/10 WandRange.cpp =="
$GPP "$T/wandrange_t.cpp"

echo "== 1h/10 AggroWatch.cpp =="
$GPP "$T/aggro_t.cpp"

echo "== 1i/10 PartyRecon.cpp =="
$GPP "$T/partyrecon_t.cpp"

echo "== 1i2/10 PartyStatus.cpp =="
$GPP "$T/partystatus_t.cpp"

echo "== 1f/10 GoapProbe.cpp =="
$GPP "$T/goap_t.cpp"

echo "== 1g/10 GuardianDoctrine.cpp =="
# SEH под g++ нет: __try/__except подменяем на try/catch. Проверяется не
# поведение обработчика, а синтаксис тела — этого и хотим.
$GPP -Isrc "-D__try=try" "-D__except(x)=catch(...)" "$T/guard_t.cpp"

echo "== 1n/10 NexusDoctrine.cpp =="
$GPP -Isrc "-D__try=try" "-D__except(x)=catch(...)" "$T/nexus_t.cpp"

echo "== 1o/10 OrderWatch.cpp =="
$GPP -Isrc "-D__try=try" "-D__except(x)=catch(...)" "$T/orderwatch_t.cpp"

echo "== 2/10 UI block =="
python3 - <<'PY'
p = 'src/devtools/DevTools.cpp'
s = open(p, encoding='utf-8').read()
start = s.index('    // --- \u043e\u0445\u043e\u0442\u0430 \u0437\u0430 \u043c\u043d\u043e\u0436\u0438\u0442\u0435\u043b\u0435\u043c \u0442\u0435\u043c\u043f\u0430 \u0430\u043d\u0438\u043c\u0430\u0446\u0438\u0438 ---')
end = s.index('    ImGui::Spacing();\n\n    // ================= Player + Main Pawn recon', start)
open('tools/tcomp/ui_block.inc', 'w', encoding='utf-8').write(s[start:end])
PY
$GPP "$T/ui_t.cpp"

echo "== 2b/10 UI block (PawnAI.cpp) =="
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

echo "== 2c/10 EnemyAI.cpp =="
$GPP "$T/enemyai_t.cpp"

echo "== 2d/10 EnemyTuner.cpp =="
$GPP -Isrc "-D__try=try" "-D__except(x)=catch(...)" "$T/enemytuner_t.cpp"

echo "== 9/10 ASCII in UI strings =="
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

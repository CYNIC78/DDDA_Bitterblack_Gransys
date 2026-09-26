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
# То же, но с настоящей компиляцией и линковкой: -fsyntax-only тела не проверяет.
GPPC="g++ -std=c++11 -I$T -I$T/shim -I$ROOT -D__try=try -D__except(x)=catch(...)"

echo "== 1/10 AnimProbe.cpp =="
$GPP "$T/animprobe_t.cpp"

echo "== 1b/10 MonsterDirector.cpp =="
$GPP "$T/director_t.cpp"

echo "== 1k/10 PackObserve.cpp =="
$GPP -DDDDA_PACKOBSERVE_PORTABLE "$T/packobserve_t.cpp"

echo "== 1j/10 MonsterTempo.cpp =="
$GPP -DDDDA_TEMPO_PORTABLE_FIXTURE "$ROOT/src/runtime/MonsterTempo.cpp"

echo "== 1j2/10 WorldScan.cpp (планировщик пешек) =="
# ЗАЧЕМ. 85.12 заменил кэш планировщика с одного тела (два скаляра
# g_plannerBody/g_plannerPtr) на массив из трёх записей. Старые имена
# остались лежать в блоке разгрузки мира — и поехали к тестеру как C2065,
# то есть ценой целой сборки. Проверка ловит ровно это за секунду:
# shim уже есть (tempo_stdafx.h через -DDDDA_TEMPO_PORTABLE_FIXTURE),
# SEH подменяем так же, как для enemytuner_t.
$GPP -DDDDA_TEMPO_PORTABLE_FIXTURE "-D__try=try" "-D__except(x)=catch(...)" \
     "$ROOT/src/runtime/WorldScan.cpp"

echo "== 1c/10 PawnHaste.cpp =="
$GPP "$T/pawnhaste_t.cpp"

echo "== 1d/10 VocationCordon.cpp =="
$GPP "$T/cordon_t.cpp"

echo "== 1p/10 MemProbe.cpp (ворота записи, 85.24) =="
# ЗАЧЕМ. MemProbe — фундамент (чтение/запись чужой памяти) и до 85.24 не
# проверялся: в общем шиме нет PE-заголовков и PAGE_*, поэтому файл не
# собирался. В 85.24 сюда добавлены ворота записи (BlockWritesFor/WritesOpen
# и гейт внутри WrSafe) — ошибиться в фундаменте дороже всего, поэтому шим
# дополнен отдельным memprobe_shim.h, а файл теперь под проверкой.
$GPP "-D__try=try" "-D__except(x)=catch(...)" "$T/memprobe_t.cpp"

echo "== 1q/10 PawnAI core calls (85.24) =="
# ЗАЧЕМ. Смежные шаги проверяют только ВЫРЕЗАННЫЕ UI-блоки PawnAI.cpp, а новый
# продуктовый сброс (ProductWorldUnload) и ворота записи живут в начале файла и
# в проверку не попадали. Проба содержит ровно те вызовы, что делает PawnAI:
# исчезнувшее имя или переехавший namespace она ловит за секунду.
$GPP "$T/pawncore_probe.cpp"

echo "== 1r/10 LogMem.cpp + ЛИНКОВКА (85.25) =="
# ЗАЧЕМ. 25.09.2026 сборка MSVC упала на линковке: SetWorkerThreadId оказался
# определен в АНОНИМНОМ пространстве имён — файл его видел, PawnAI.cpp нет.
# Компиляция при этом проходила, поэтому синтаксическая проверка такое не
# ловит В ПРИНЦИПЕ: объявление в LogMem.h корректно, тела просто нет.
# Здесь две отдельные единицы трансляции, как в жизни: LogMem.cpp даёт
# определения, logmem_call.cpp (ровно вызовы dinput8.cpp и PawnAI.cpp) на них
# ссылается. Отсутствие тела падает за секунду — с той же формулировкой
# undefined reference, что была в студии.
$GPPC -DDDDA_LOGMEM_PORTABLE_FIXTURE -c "$ROOT/src/runtime/LogMem.cpp" -o /tmp/synchk_logmem.o
$GPPC -c "$T/logmem_call.cpp" -o /tmp/synchk_logmem_call.o
g++ /tmp/synchk_logmem.o /tmp/synchk_logmem_call.o -o /tmp/synchk_logmem_probe

echo "== 1s/10 CombatIntel.cpp (85.26, обработчик урона) =="
# ЗАЧЕМ. CombatIntel — кто по кому ударил и проверка «свой или чужой». До сих
# пор файл не проверялся ВООБЩЕ: в нём панель на ImGui, а настоящий ImGui тянет
# DirectX; плюс MinHook намеренно отказывается собираться вне x86; плюс четыре
# ассемблерных переходника MSVC. Ошибку в этом файле владелец ловил бы своей
# сборкой — так уже было с линковкой LogMem. Теперь всё три помехи закрыты
# (imgui.h, MinHook/ и prep_combatintel.py), и файл разбирается целиком.
python3 "$ROOT/tools/tcomp/prep_combatintel.py" /tmp/synchk_combatintel.cpp
$GPP -DDDDA_COMBATINTEL_PORTABLE_FIXTURE -D__stdcall= -I"$ROOT/src" \
     -D__try=try "-D__except(x)=catch(...)" /tmp/synchk_combatintel.cpp

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

echo "== 2b2/10 UI block: roles (PawnAI.cpp, 85.12) =="
# ЗАЧЕМ ЕЩЁ ОДИН БЛОК. Тот же принцип, что у блока склонностей: вызовы
# ImGui из нового куска панели должны ловиться g++ за секунду, а не
# сборкой у тестера. Своя пара меток, потому что блок ролей стоит ВЫШЕ по
# той же функции: расширить старую метку нельзя — в проверку попадут
# Possession, CombatBus и SEH, двойников которым в ui_pawn_t.cpp нет.
python3 - <<'PY'
p = 'src/PawnAI.cpp'
s = open(p, encoding='utf-8').read()
start = s.index('\n', s.index('[UI-BLOCK-ROLES-BEGIN]')) + 1
end = s.rindex('\n', start, s.index('[UI-BLOCK-ROLES-END]', start))
open('tools/tcomp/ui_roles_block.inc','w',encoding='utf-8').write(s[start:end])
PY
$GPP "$T/ui_roles_t.cpp"

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

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

echo "== 2f/10 EntityConfig.cpp (85.34: ключи адреналина) =="
$GPP "$T/entityconfig_t.cpp"

echo "== 2g/10 EntityConfig BEHAVIOR (85.34: наследование и границы) =="
# Не только разбор, но и ПОВЕДЕНИЕ: наследование [default] -> [class.*] ->
# [emXXXX] и зажимы (пол 1.0, потолок 1.6). До 85.34 у слоя не было ни одного
# рантайм-теста, а фикстура заодно нашла дыру в шиме: wsprintfA ничего не
# форматировал, поэтому [em0100] в тестах превращался в пустую секцию.
g++ -std=c++11 -Wall -Wextra -Werror -I"$T" -I"$T/shim" -I"$ROOT" -I"$ROOT/src" \
    "$T/entityconfig_behavior_test.cpp" -o /tmp/synchk_entitycfg
/tmp/synchk_entitycfg

echo "== 2h/10 SpeciesTuning.cpp (85.36: числа вида из ini) =="
# ЗАЧЕМ. Модуль читает [species.uEmXXXX] и ЗАЖИМАЕТ небезопасные числа. Он
# собирался только студий у владельца; здесь проверяется сборкой.
$GPP "$T/species_tuning_t.cpp"

echo "== 2i/10 SpeciesTuning BEHAVIOR (85.36: зажимы и наследование ключа) =="
# Не разбор, а ПОВЕДЕНИЕ: отсутствие ключа = число карточки, мусор -> карточка,
# перевёрнутый диапазон, пределы движка и главное — ГАРАНТИЯ ПРИКАЗА (потолок не
# ниже базового диапазона, иначе admit отобьёт тело молча).
# Модуль включается в фикстуру как есть, поэтому второй раз его компилировать
# нельзя: будет multiple definition (урок 85.36).
g++ -std=c++11 -Wall -Wextra -Werror -I"$T" -I"$T/shim" -I"$ROOT" -I"$ROOT/src" \
    "$T/species_tuning_test.cpp" -o /tmp/synchk_species
/tmp/synchk_species

echo "== 2i2/10 Ranks BEHAVIOR (85.40/85.42: ранги вместо коридора) =="
# Проверяем ПОВЕДЕНИЕ лестницы: флаг вида (нет ключа -> ступеней нет),
# встроенные числа пилота, починка мусора в [ladder], детерминизм по адресу
# тела и РАЗБРОС: рядовых большинство, элита и мини-босс редки, но живые.
# Флаги GC обязательны: фикстура тянет MonsterTempo.cpp, где есть ссылки на
# рантайм-данные, которых в песочнице нет (урок фикстуры мобилизации).
g++ -std=c++11 -Wall -Wextra -Werror -I"$T" -I"$T/shim" -I"$ROOT" -I"$ROOT/src" \
    -D__try=try -D__except\(x\)=catch\(...\) -ffunction-sections -fdata-sections \
    "$T/ranks_test.cpp" -Wl,--gc-sections -o /tmp/synchk_ranks
/tmp/synchk_ranks

echo "== 2i3/10 FileBase BEHAVIOR (85.60: база вида из файлов игры) =="
# ЗАЧЕМ. С 85.60 база боевых статов берётся из таблицы, сгенерированной из файлов
# игры, и умножается. Ошибка в таблице меняет бой у всех видов сразу, а владелец
# увидит только «стало легко/тяжело» — без игры это не отладить. Поэтому проверяем
# таблицу здесь: числа гоблина совпадают с полем, крупные бойцы не отсеяны,
# «иммунные» маркеры и нули отсеяны, дублей emId нет (дубль = неоднозначный вид).
g++ -std=c++11 -Wall -Wextra -Werror -I"$ROOT" \
    "$T/filebase_test.cpp" -o /tmp/synchk_filebase
/tmp/synchk_filebase

echo "== 2j/10 Все .cpp проекта включают stdafx.h (C1010) =="
# ЗАЧЕМ. 85.37 уехал с ошибкой C1010: новый файл SpeciesTuning.cpp не включал
# "stdafx.h", а в студии включены предкомпилированные заголовки. g++ этого НЕ
# ловит: там stdafx подменён шимом и без него всё собирается. Владелец заплатил
# итерацией. Теперь проверяем сам проект: каждый ClCompile без NotUsing обязан
# где-то включать "stdafx.h" — не обязательно первой строкой (перед ним у части
# файлов стоит комментарий, MSVC это допускает), но ОБЯЗАН.
python3 - <<'PYCHK'
import re, io, os, sys
proj = 'ddda-ai-overhaul.vcxproj'
s = io.open(proj, encoding='utf-8').read()
blocks = re.findall(r'<ClCompile Include="([^"]+)"(?:\s*/>|>(.*?)</ClCompile>)', s, re.S)
bad = []
for path, inner in blocks:
    if 'NotUsing' in (inner or ''):
        continue
    p = path.replace('\\', '/')
    if not os.path.exists(p):
        bad.append(p + ': файла из проекта нет на диске'); continue
    txt = io.open(p, encoding='utf-8', errors='replace').read()
    if '#include "stdafx.h"' not in txt:
        bad.append(p + ': нет #include "stdafx.h" -> у владельца будет C1010')
for b in bad:
    print(' ', b)
sys.exit(1 if bad else 0)
PYCHK

echo "== 2c/10 EnemyAI.cpp =="
$GPP "$T/enemyai_t.cpp"

echo "== 2d/10 EnemyTuner.cpp =="
$GPP -Isrc "-D__try=try" "-D__except(x)=catch(...)" "$T/enemytuner_t.cpp"

echo "== 10/10 структурные проверки (85.34 + 85.36) =="
# ЗАЧЕМ. У EnemyTuner нет рантайм-фикстуры: он пишет в живые тела игры, и в
# песочнице таких тел нет. Но у него есть ровно одна фраза, которую нельзя
# потерять при рефакторинге: всплеск адреналина входит в ТУ ЖЕ формулу, что и
# обычные множители. Потеряется — тесты этого не заметят, а владелец заметит
# молчанием фичи в поле. Поэтому проверяем форму кода явно.
python3 - <<'PYCHK'
import sys
src = open('src/EnemyTuner.cpp', encoding='utf-8').read()
bad = []
# 1) адреналin умножается в обеих силах (физическая и магическая);
if '* adrAtk' not in src:  bad.append('нет множителя adrAtk в силе физ. атаки')
if '* adrMAtk' not in src: bad.append('нет множителя adrMAtk в силе маг. атаки')
# 2) уровень берётся у Tempo, а пик — у карточки вида;
if 'DirectorAdrenalineLevelFor' not in src: bad.append('нет запроса уровня у Tempo')
if 'adrenalineAtk' not in src:              bad.append('нет пика вида (adrenalineAtk)')
# 3) всплеск включает блок боевых статов (иначе он не применится, когда все
#    множители ini равны 1.0 — самый частый случай);
if 'rec0->haveCombat || spikeLive' not in src: bad.append('всплеск не входит в условие блока')
# 4) пол 1.0 (ваниль — нижний порог) и потолок вида;
if 'if (v < 1.0f) return 1.0f;' not in src: bad.append('нет пола всплеска')
if 'kAdrenalineMax' not in src:             bad.append('нет потолка всплеска')
for b in bad: print(' ', b)

# 5) Поставленный ddda_entities.ini: проверяем ФАЙЛ, который уезжает владельцу.
#    Числа тут — решение по балансу, они меняются; держим не числа, а правило:
#    ванильная сила удара остаётся нижним порогом, даже для худшего ролла особи
#    (ролл симметричный 0.9..1.1, поэтому множитель обязан быть >= 1.111...).
import re as _re
ini = open('ddda_entities.ini', encoding='utf-8').read()
def _sec(name):
    m = _re.search(r'^\[' + name + r'\](.*?)(?=^\[|\Z)', ini, _re.S | _re.M)
    return m.group(1) if m else ''
def _val(sec, key):
    m = _re.search(r'^' + key + r'\s*=\s*([0-9.]+)', sec, _re.M)
    return float(m.group(1)) if m else None
d = _sec('default')
for key in ('attackMult', 'magickAttackMult'):
    v = _val(d, key)
    if v is None: bad.append('в [default] нет ' + key); continue
    if v * 0.9 < 1.0 - 1e-6:
        bad.append('%s=%s: худший ролл особи уводит удар НИЖЕ ваниллы' % (key, v))
if _val(d, 'adrenalineAtk') is None:     bad.append('в [default] нет adrenalineAtk')
if _val(d, 'adrenalineMagick') is None:  bad.append('в [default] нет adrenalineMagick')
if _val(_sec('class.boss'), 'attackMult') != 1.0:
    bad.append('у боссов не зафиксирована ванильная сила удара')

# 6) Числа вида (85.36): их теперь читает MonsterDirector из ini, а модуль
#    SpeciesTuning зажимает. Проверяем ФОРМУ: зажим ниже базы, зажим пределов
#    движка и строгий зазор — то, без чего приказ молча отобьётся на каждом
#    теле, а в поле это выглядит как «монстры перестали реагировать».
st = open('src/monsterai/SpeciesTuning.cpp', encoding='utf-8').read()
if 'p.lo < baseLo' not in st or 'p.hi < baseHi' not in st:
    bad.append('SpeciesTuning: нет зажима "потолок не ниже базового диапазона"')
if 'kMinGap' not in st:
    bad.append('SpeciesTuning: нет строгого зазора (низ == верх не регистрируется)')
if 'kLocoClampMax' not in st or 'kAnimClampMax' not in st:
    bad.append('SpeciesTuning: нет пределов движка')
if '"tempoRage"' not in st:
    bad.append('SpeciesTuning: нет ключа-выключателя вида')
md = open('src/monsterai/MonsterDirector.cpp', encoding='utf-8').read()
if 'SpeciesTempoFromIni' not in md:
    bad.append('MonsterDirector: не читает числа вида из ini')
if 'GetRange(&baseLocoMin' not in md or 'GetAnimRange(&baseAnimMin' not in md:
    bad.append('MonsterDirector: не берёт базовый диапазон у Tempo')
oa = open('ddda_ai_overhaul.ini', encoding='utf-8').read()
for kind in ('uEm0200', 'uEm0100', 'uEm0101', 'uEm0400'):
    if ('[species.%s]' % kind) not in oa:
        bad.append('в ddda_ai_overhaul.ini нет секции [species.%s]' % kind)

for b in bad: print(' ', b)
sys.exit(1 if bad else 0)
PYCHK

echo "== 10b/10 Ранги: ступень выдаётся один раз (85.56) =="
# ЗАЧЕМ. Живое чтение [ranks] безопасно РОВНО ПОТОМУ, что ступень замораживается
# при выдаче. Если кто-нибудь вернёт ролл в боевой путь (или в строку лога),
# сборка соберётся, фикстуры пройдут, а в поле живого монстра начнёт переобувать
# прямо в бою. Это не ловится поведенческим тестом — только формой кода.
python3 - <<'PYCHK'
import sys
et = open('src/EnemyTuner.cpp', encoding='utf-8').read()
po = open('src/monsterai/PackObserve.cpp', encoding='utf-8').read()
mt = open('src/runtime/MonsterTempo.cpp', encoding='utf-8').read()
md = open('src/monsterai/MonsterDirector.cpp', encoding='utf-8').read()
bad = []
if 'EnsureRankIssued' not in et:
    bad.append('EnemyTuner: нет выдачи ступени (EnsureRankIssued)')
n = et.count('Runtime::Tempo::RankPickFor')
if n != 1:
    bad.append('EnemyTuner: ролл зовётся из %d мест, а должен из одного (выдача)' % n)
if 'RankPickFor' in po:
    bad.append('PackObserve: ранг ПЕРЕСЧИТЫВАЕТСЯ вместо чтения выданного')
if 'RankIssuedFor' not in po:
    bad.append('PackObserve: не спрашивает выданную ступень у тюнера')
if 'gen * 2654435761u' not in mt:
    bad.append('MonsterTempo: поколение жильца не входит в хеш')
if 'rec0->gen = ++s_bodyGenSeq;' not in et:
    bad.append('EnemyTuner: смена жильца не даёт нового поколения')
if 'rankStep = -1' not in et:
    bad.append('EnemyTuner: ступень не сбрасывается в -1 (ноль - законная ступень)')
if 'RanksWatchTick' not in md:
    bad.append('MonsterDirector: нет сторожа живого чтения [ranks]')
# 85.57: наборы пачек по месту. Ключевое, что нельзя потерять при рефакторинге:
# НАБОР СПРАШИВАЕТСЯ ТОЛЬКО В ВЫДАЧЕ СТУПЕНИ. Если его начнут спрашивать в бою
# (в наблюдателе или в строке лога), место начнёт «переигрывать» набор у уже
# живущей особи — сборка соберётся, тесты пройдут, а в поле поедет бой.
if 'PackSetsFromIni' not in md:
    bad.append('MonsterDirector: не читает [packs] и не печатает наборы')
if 'PackSetForCell' not in et:
    bad.append('EnemyTuner: не спрашивает набор у места')
if 'PackSetForCell' in po or 'PackSetForCell' in md:
    bad.append('набор спрашивают НЕ из выдачи ступени (место переиграет живую особь)')
if 'CellMinibossCount' not in et or 'NoteCellMiniboss' not in et:
    bad.append('EnemyTuner: нет предела «один мини-босс на место»')
if 'RankQuery q' not in et:
    bad.append('EnemyTuner: ролл зовут не через запрос с местом')
if 'PackSetsEnabled' not in mt:
    bad.append('MonsterTempo: нет переключателя наборов')
if 'pack memory reset' not in mt:
    bad.append('MonsterTempo: память мест не чистится на разгрузке мира')
# 85.58: книга по огню. Смерть с ДЕЙСТВИЕМ-ВЫХОДОМ из огня (DmgBurnEnd) — это
# «пережил огонь, умер от другого», а не «сгорел». В поле 85.57 без этой
# проверки ветеран, вышедший из огня и убитый пешками, был посчитан сгоревшим.
if 'stillOnFire' not in po or '!ActIsBurnEnd(m.act)' not in po:
    bad.append('PackObserve: книга по огню снова считает выход из огня смертью в огне')
# 85.58: «stale» только по КОНКРЕТНОМУ чужому классу. В поле 85.57 освобождённые
# тела давали имя базового класса (MtObject) и прятали верный ранг.
if 'pools=unverified' not in po or 'const bool concrete' not in po:
    bad.append('PackObserve: stale срабатывает не только на конкретный чужой класс')
if 'NotePackSetBody' not in et:
    bad.append('EnemyTuner: тела не считаются по наборам (сводка будет пустой)')
if 'PackSetSummary' not in mt or 'pack set summary' not in mt:
    bad.append('MonsterTempo: нет сводки по наборам за сессию')
# 85.59: база вида говорит о себе. Разница баз между сессиями (250 против 126.2)
# выяснилась только потому, что мы пошли в файлы игры; в коде же эвристика
# «восстановления ванили» молчала. Проверяем, что она печатает сырьё, метку
# восстановления и сверку с файлом — иначе следующий такой случай опять повиснет.
if 'combat base %s raw atk' not in et:
    bad.append('EnemyTuner: нет линии базы вида (эвристика снова решает молча)')
if 'RECOVERED-atk' not in et or 'RECOVERED-def' not in et:
    bad.append('EnemyTuner: восстановление ванили не помечается в логе')
# 85.60: база вида берётся из файлов игры, а не угадывается.
if 'FindSpeciesFileBase' not in et or 'SpeciesFileBaseSane' not in et:
    bad.append('EnemyTuner: нет пути «база из файлов игры» (владелец решил: берём из файлов)')
if 'source=FILE' not in et and 'useFileBase ? "FILE"' not in et:
    bad.append('EnemyTuner: источник базы не подписывается в логе')
if 'RAW MISMATCH' not in et:
    bad.append('EnemyTuner: расхождение чтения с файлом не печатается')
# 85.61: в строке расхождения должны быть улики, иначе вопрос «почему память
# показывает не то, что в файле» опять повиснет: hp из того же блока, путь
# поиска блока и «что мы записали бы сейчас».
if 'param=%s(+0x%04X)' not in et or 'ourWriteWouldBe' not in et or 'paramSrc' not in et:
    bad.append('EnemyTuner: в RAW MISMATCH нет улик (hp/путь поиска/наша запись)')
# 85.61: поиск блока карточки — ОДНА дорога. Он был скопирован в трёх местах
# (Leash, боевые статы, Sanctuary), и правка в одной копии молча не попадала
# в другие. Если копий снова станет три — вернётся та же ловушка.
if et.count('CharParamBase(') != 4:   # 1 определение + 3 вызова
    bad.append('EnemyTuner: поиск блока карточки снова размножился (ожидается одна функция + три вызова)')
if 'fileAtk' in et:
    bad.append('EnemyTuner: остался старый однобокий эталон fileAtk (только гоблин)')
fbase_h = open('src/runtime/EnemyFileBase.h', encoding='utf-8').read()
if '// em0100' not in fbase_h or '{ 0x0064,' not in fbase_h:
    bad.append('EnemyFileBase.h: таблица баз повреждена (нет гоблина)')
if '{ 0x00C8,' not in fbase_h or '{ 0x0258,' not in fbase_h:
    bad.append('EnemyFileBase.h: в таблице нет волка (em0200) или харпии (em0600)')
if 'SpeciesFileBaseSane' not in fbase_h:
    bad.append('EnemyFileBase.h: нет отсева небоевых чисел (нули/иммунные тысячи)')
if 'recoveredAtk' not in et:
    bad.append('EnemyTuner: флаг восстановления пропал')

mth = open('src/runtime/MonsterTempo.h', encoding='utf-8').read()
if 'kPackSets = 12' not in mth:
    bad.append('MonsterTempo.h: слотов наборов не 12 (владельцу обещано 12)')
for ini_path in ('ddda_ai_overhaul.ini', 'ddda_ai_overhaul.default.ini'):
    ini_txt = open(ini_path, encoding='utf-8').read()
    if 'set11' not in ini_txt:
        bad.append('в ' + ini_path + ' не описаны свободные слоты до set11')
    if '[packs]' not in ini_txt:
        bad.append('в ' + ini_path + ' нет секции [packs]')
if 'config.Path()' not in md:
    bad.append('MonsterDirector: сторож следит не за тем файлом, что читает мод')
for b in bad: print(' ', b)
sys.exit(1 if bad else 0)
PYCHK

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

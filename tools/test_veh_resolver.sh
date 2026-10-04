#!/usr/bin/env bash
# Опознание площадок [VEH] по карте линковки — 20-й тест сетки (85.69).
#
# ПОВОД. После поля 85.68 в логе снова стояли две площадки в нашем модуле, а
# назвать их было нечем: карта линковки, сгенерированная сборкой, — единственный
# способ, и процедура её разбора до сих пор жила абзацем в доке про краш 25.09.
# Теперь это инструмент tools/veh_sites.py, и у него есть контракт:
#   * смещение из лога («DINPUT8.dll+0xA2BC7») превращается в имя функции;
#   * если смещение раньше первого ориентира карты — честное «—», а не выдумка;
#   * инструмент печатает дату карты — карта от другой сборки обязана быть видна.
#
# ЗАЧЕМ ОТДЕЛЬНЫЙ ТЕСТ. Инструмент сам по себе безобиден, но он — часть
# процедуры разбора сбоев: соврёт он, и следующий разбор уйдёт не туда. Фикстура
# маленькая и синтетическая: карта на три символа, лог на две строки.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/tiny.map" <<'MAP'
 dinput8

 Timestamp is 6ab6e5aa (Sat Sep 26 00:20:42 2026)

 Preferred load address is 10000000

  Address         Publics by Value              Rva+Base       Lib:Object

 0001:00001000       ?Alpha@@YAXXZ               10001000 f   Foo.obj
 0001:00002000       ?Beta@@YAXXZ                10002000 f   Bar.obj
 0001:00009000       ?Gamma@@YAXXZ               10009000 f   Baz.obj
MAP

cat > "$WORK/tiny.log" <<'LOG'
EnemyTuner: что-то своё
[VEH] NEW SITE READ(0) at 0x6C262BC7 in DINPUT8.dll+0x1A2C | data 0x05000014 other | thread 10960 (наш рабочий поток)
[VEH] dump #1: faults=5 sites=1 (дальше такие дампы только считаются, итог в конце сессии)
[VEH] NEW SITE READ(1) at 0x6C202300 in DINPUT8.dll+0x2300 | data 0x4 null-ish | thread 1
LogMem: session fault-handling summary faults=9278 dumps=14 sites=2
LOG

out1="$(python3 "$ROOT/tools/veh_sites.py" --map "$WORK/tiny.map" "$WORK/tiny.log")"
echo "$out1"

echo "$out1" | grep -q "?Alpha@@YAXXZ (+0xA2C) · Foo.obj" \
    || { echo "ПЛОХО: смещение из лога не превратилось в имя функции"; exit 1; }
echo "$out1" | grep -q "?Beta@@YAXXZ (+0x300) · Bar.obj" \
    || { echo "ПЛОХО: второе смещение не опознано"; exit 1; }
echo "$out1" | grep -q "faults=9278" \
    || { echo "ПЛОХО: итог сессии не показан"; exit 1; }
echo "$out1" | grep -q "Timestamp" \
    || { echo "ПЛОХО: дата карты не напечатана (её и надо сверять со сборкой)"; exit 1; }

out2="$(python3 "$ROOT/tools/veh_sites.py" --map "$WORK/tiny.map" 0x800 0x9ABC)"
echo "$out2"
echo "$out2" | grep -q "0x800  →  —" \
    || { echo "ПЛОХО: смещение до первого ориентира не признано неопознанным"; exit 1; }
echo "$out2" | grep -q "?Gamma@@YAXXZ (+0xABC) · Baz.obj" \
    || { echo "ПЛОХО: ручной режим не опознал смещение"; exit 1; }

echo "veh resolver: PASS (лог и ручной режим, отказ честный, дата карты видна)"

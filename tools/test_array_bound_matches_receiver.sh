#!/usr/bin/env bash
# 86.09: граница цикла записи обязана быть ЁМКОСТЬЮ ПРИЁМНИКА.
#
# Почему контракт. В 86.08 я поднял kMaxAct с 32 до 80 и вместе с ним — все
# вхождения литерала 32 в переборе актёров. Одно из них оказалось не размером
# списка, а ёмкостью приёмника в ДРУГОМ модуле: `w.count < 32` ограничивало
# запись в `WorldPresence units[32]` из CombatBus.h. Подняв его до 80, я
# получил запись за конец структуры WorldReport — жёсткий краш игры на втором
# бою, когда список актёров перевалил за 32.
#
# Ошибка была в том, что все тридцать-два выглядели одинаково. Контракт
# фиксирует правило: там, где пишем в чужой массив фиксированного размера,
# граница берётся через sizeof этого массива, а не общей константой.
set -euo pipefail
cd "$(dirname "$0")/.."
WS=src/runtime/WorldScan.cpp
CB=src/CombatBus.h
[ -f "$WS" ] && [ -f "$CB" ] || { echo "НЕТ ФАЙЛОВ"; exit 1; }

fail=0

# 1) Прямой запрет: граница записи в w.units не должна быть kMaxAct.
if grep -q "w\.count < kMaxAct" "$WS"; then
    echo "ПРОВАЛ: граница записи в w.units снова kMaxAct"
    echo "  w.units — это WorldPresence units[32] из CombatBus.h, а не список актёров."
    echo "  Так был устроен краш 86.08."
    fail=1
fi

# 2) Ёмкость обязана выводиться из самого массива.
if ! grep -q "sizeof(w\.units) / sizeof(w\.units\[0\])" "$WS"; then
    echo "ПРОВАЛ: ёмкость w.units не выводится через sizeof"
    fail=1
fi

# 3) Ёмкость приёмника известна и конечна.
CAP=$(grep -oE "WorldPresence units\[[0-9]+\]" "$CB" | grep -oE "[0-9]+")
[ -n "${CAP:-}" ] || { echo "ПРОВАЛ: в CombatBus.h не найден WorldPresence units[N]"; exit 1; }

[ "$fail" -eq 0 ] || exit 1
echo "  граница записи в снимок мира: sizeof-ёмкость (units[$CAP]), не kMaxAct"

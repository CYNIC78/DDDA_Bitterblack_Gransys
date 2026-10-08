#!/usr/bin/env bash
# 86.06: каждый путь отказа записи обязан ставить слот на охлаждение.
#
# Почему контракт, а не тест. Охладитель — статические функции внутри
# AggroWatch.cpp; снаружи их не достать. А вот само правило «отказ = взвод
# охлаждения» проверяется структурно, и именно его нарушение в 86.03 дало
# unsafeSkips=929: взвод стоял только в гейте формы, а PinAnomaly зовётся
# ещё из wake и фейк-хита.
set -euo pipefail
cd "$(dirname "$0")/.."
F=src/runtime/AggroWatch.cpp
[ -f "$F" ] || { echo "НЕТ ФАЙЛА $F"; exit 1; }

fail=0
# Каждая строка PinAnomaly( должна иметь ShapeRetryArm( в трёх строках выше.
while IFS= read -r line; do
    n=${line%%:*}
    from=$(( n - 3 )); [ "$from" -lt 1 ] && from=1
    if ! sed -n "${from},${n}p" "$F" | grep -q "ShapeRetryArm("; then
        echo "ОТКАЗ БЕЗ ОХЛАЖДЕНИЯ: $F:$n"
        echo "  $(sed -n "${n}p" "$F" | sed 's/^ *//')"
        fail=1
    fi
done < <(grep -n "PinAnomaly(" "$F" | grep -v "static void PinAnomaly(")
[ "$fail" -eq 0 ] || { echo "ПРОВАЛ: есть отказы без охлаждения"; exit 1; }

# Окно: должно переживать разрыв между приказами.
W=$(grep -oE "kShapeRetryCooldownMs = [0-9]+" "$F" | grep -oE "[0-9]+")
if [ "${W:-0}" -lt 2000 ]; then
    echo "ПРОВАЛ: окно охлаждения ${W:-?} мс — короче разрыва между приказами"
    echo "  86.05 показал: при 250 мс каждый новый приказ заново пробует"
    echo "  все мёртвые слоты, unsafeSkips=929 при writes=932."
    exit 1
fi

echo "  охлаждение отказов записи: $(grep -c "ShapeRetryArm(" "$F") взводов / $(grep -c "ShapeRetryBlocked(" "$F") гейтов, окно ${W} мс"

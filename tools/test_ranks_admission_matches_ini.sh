#!/usr/bin/env bash
# 86.06: вид, допущенный кодом под лестницу рангов, не должен быть выключен
# в отгружаемом эталоне.
#
# Почему контракт. Хобгоблина внесли в RanksSpeciesAllowed в 85.86, а ключ
# [species.uEm0101] ranks в эталоне так и остался "off" с подписью «подключатся
# следующим». Шесть сборок эталон противоречил коду: по эталону хоб не получал
# ни одной ступени. Всплыло только потому, что живой ini владельца несёт ключ
# включённым — то есть сломана была именно свежая установка.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=src/runtime/MonsterTempo.cpp
INI=ddda_ai_overhaul.default.ini
[ -f "$SRC" ] && [ -f "$INI" ] || { echo "НЕТ ФАЙЛОВ"; exit 1; }

# Виды из списка допуска.
ALLOWED=$(awk '/^bool RanksSpeciesAllowed/,/^}/' "$SRC" | grep -oE '"uEm[0-9]+"' | tr -d '"')
[ -n "$ALLOWED" ] || { echo "ПРОВАЛ: список допуска пуст"; exit 1; }

fail=0
for k in $ALLOWED; do
    v=$(awk -v sec="[species.$k]" '
            $0==sec {insec=1; next}
            insec && /^\[/ {insec=0}
            insec && $1=="ranks" {print $3; exit}
        ' "$INI")
    if [ -z "$v" ]; then
        echo "  $k: ключа ranks нет в эталоне — действует умолчание кода (on), ок"
        continue
    fi
    if [ "$v" = "off" ]; then
        echo "ПРОВАЛ: $k допущен кодом под лестницу, но в эталоне ranks = off"
        echo "  По такому эталону вид не получит ни одной ступени."
        fail=1
    else
        echo "  $k: ranks = $v"
    fi
done
[ "$fail" -eq 0 ] || exit 1
echo "  лестница рангов: допуск кода и эталон согласованы"

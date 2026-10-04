#!/usr/bin/env bash
# 84.37 Possession: xmm inject on our Set only.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="$ROOT/src/BuildTag.h"
README="$ROOT/README.md"   # 85.66: проверка «README знает текущий тег»
CPP="$ROOT/src/pawnai/Possession.cpp"
H="$ROOT/src/pawnai/Possession.h"
PAWN="$ROOT/src/PawnAI.cpp"
PROJ="$ROOT/ddda-ai-overhaul.vcxproj"
INI="$ROOT/ddda_ai_overhaul.default.ini"
PS="$ROOT/src/runtime/PartyStatus.cpp"

# 85.66: вместо пина «84.» — структура тега + согласие с README (не устаревает).
tag_val="$(sed -n 's/.*MOD_BUILD_TAG[[:space:]]*"\([^"]*\)".*/\1/p' "$TAG" | head -1)"
case "$tag_val" in
  [0-9]*.[0-9]*) ;;
  *) echo "BuildTag.h: тег не похож на билд: '$tag_val'" >&2; exit 1;;
esac
grep -Fq "$tag_val" "$README" || {
  echo "README не упоминает текущий тег '$tag_val'" >&2; exit 1; }
grep -Fq 'Possession.cpp' "$PROJ"
grep -Fq 'Possession.h' "$PROJ"
grep -Fq '#include "pawnai/Possession.h"' "$PAWN"
grep -Fq 'PawnAI::Possession::Tick()' "$PAWN"
grep -Fq 'PawnAI::Possession::Init()' "$PAWN"
grep -Fq 'PawnAI::Possession::Shutdown()' "$PAWN"

python3 - "$PAWN" <<'PY'
import sys
p = open(sys.argv[1], encoding='utf-8').read()
gate = p.index('if(!g_enabled || !pBase')
assert p.index('Possession::Tick') < gate
assert 'arm writes##poss' in p
assert 'layout' in p
assert 'custom params##poss' in p
assert 'timer s##poss' in p
PY

grep -Fq '0xA7000' "$CPP"
grep -Fq '0x7F0' "$CPP"
! grep -q '0xA7000;' "$CPP"
! grep -q '0x2DC8' "$CPP"
! grep -q '0x2EB8' "$CPP"
grep -Fq 'kIdPossession = 7' "$CPP"
grep -Fq 'WrSafe' "$CPP"
grep -Fq 'watch-ok' "$CPP"
! grep -q 'need-water-recipe' "$CPP"
grep -Fq 'vanilla-applied PENDING' "$CPP"
grep -Fq '__thiscall' "$CPP"
grep -Fq 's_inject' "$CPP"
grep -Fq 'movss   xmm0, s_injT' "$CPP"
grep -Fq 'cmp     s_inject, 0' "$CPP"
grep -Fq 'SetCustom' "$H"
grep -Fq 'customOn' "$H"
grep -Fq '[possession]' "$INI"
grep -Fq 'enabled = off' "$INI"
grep -Fq 'customParams = off' "$INI"
grep -Fq 'PS: SHEET %s status count=' "$PS"
# 85.66: было байтовое равенство живого ини и default — ломалось на любой правке
# комментария или значения владельцем. Контракт поставки — совпадение НАБОРА ключей
# и их значений (чтобы распаковка зипа ничего не теряла и не добавляла).
python3 - "$ROOT" <<'PYKEY'
import io, sys
root = sys.argv[1]
def keys(p):
    out, sec = {}, ''
    for line in io.open(p, encoding='utf-8', errors='replace'):
        s = line.strip()
        if not s or s.startswith(('#', ';')):
            continue
        if s.startswith('[') and s.endswith(']'):
            sec = s[1:-1].lower(); continue
        if '=' in s:
            k, v = s.split('=', 1)
            out['%s/%s' % (sec, k.strip().lower())] = v.strip().lower()
    return out
live = keys(root + '/ddda_ai_overhaul.ini')
dflt = keys(root + '/ddda_ai_overhaul.default.ini')
only_live = sorted(set(live) - set(dflt))
only_dflt = sorted(set(dflt) - set(live))
assert not only_live, 'ключи есть в живом ини, но нет в default: %s' % only_live[:5]
assert not only_dflt, 'ключи есть в default, но нет в живом ини: %s' % only_dflt[:5]
print('ini supply contract: keys %d ok' % len(dflt))
PYKEY

echo "Possession 84.37 contracts passed."

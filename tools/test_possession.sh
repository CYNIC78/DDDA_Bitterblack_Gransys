#!/usr/bin/env bash
# 84.37 Possession: xmm inject on our Set only.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="$ROOT/src/BuildTag.h"
CPP="$ROOT/src/pawnai/Possession.cpp"
H="$ROOT/src/pawnai/Possession.h"
PAWN="$ROOT/src/PawnAI.cpp"
PROJ="$ROOT/ddda-ai-overhaul.vcxproj"
INI="$ROOT/ddda_ai_overhaul.default.ini"
PS="$ROOT/src/runtime/PartyStatus.cpp"

grep -Fq '84.' "$TAG"
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
cmp -s "$ROOT/ddda_ai_overhaul.ini" "$INI"

echo "Possession 84.37 contracts passed."

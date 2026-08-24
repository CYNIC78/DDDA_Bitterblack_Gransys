#!/usr/bin/env bash
# Build 84.17 goblin-fakehit: фейк-урон гоблину + goblin-гейт пина +
# PS-опрос детей.
#
# Контракты:
#   * build tag + README;
#   * гейт живых карт гоблина: (flag & 1) && fC ∈ {4,5}, потолок 300/484;
#     константа старших битов НЕ пишется (flagCur + 1, только младший бит);
#   * фейк-хит: блок B на волчьих оффсетах +0x274/+0x27C, readback+rollback,
#     +0x278/+0x280 не трогаются;
#   * goblin-lease получает fakehit (включая ALERT), suppress НЕ получает;
#   * occupy-чек расширен на fC=5;
#   * PS: discovery + опрос детей одним проходом, read-only сохранён;
#   * offline fixtures: гейт карт + downed/revive FSM.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="$ROOT/src/BuildTag.h"
README="$ROOT/README.md"
AGGRO="$ROOT/src/runtime/AggroWatch.cpp"
AGGRO_H="$ROOT/src/runtime/AggroWatch.h"
PS_CPP="$ROOT/src/runtime/PartyStatus.cpp"
DOC1="$ROOT/docs/GOBLIN_FAKEHIT.md"
DOC2="$ROOT/docs/PARTY_STATUS_OBSERVE.md"
GATE_FIXTURE="$ROOT/tools/tcomp/goblin_fakehit_test.cpp"
FSM_FIXTURE="$ROOT/tools/tcomp/partystatus_fsm_test.cpp"
TMP="$(mktemp -d /tmp/goblin_fakehit_8417.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# Identity and documentation must agree.
grep -Fq '84.21-species-rage' "$TAG"
grep -Fq '84.16-dual-observe' "$README"
grep -Fq '84.21-species-rage' "$README"
[ -f "$DOC1" ] && [ -f "$DOC2" ]

python3 - "$AGGRO" <<'PY'
import sys
aggro = open(sys.argv[1], encoding='utf-8').read()

# Goblin live gate: low-byte flag, fC=4 perception / fC=5 combat.
assert 'kGobCombatPinValue = 484.0f' in aggro
assert 'bool LiveGoblinCardMode(uint32_t flag, uint32_t mode,' in aggro
gate = aggro[aggro.index('bool LiveGoblinCardMode'):]
gate = gate[:gate.index('\n}', gate.index('{'))]
assert '(flag & 1u) != 1u' in gate
assert 'mode == 5' in gate
assert 'kGobCombatPinValue' in gate

# PinWriteCard dispatches the goblin gate; wolf gate untouched.
assert 'LiveGoblinCardMode(flag, c4, &pinCeil, &maxNative)' in aggro
assert 'LiveWolfCardMode(flag, c4, &pinCeil, &maxNative)' in aggro

# GoblinFakehitCard: same block-B offsets as wolf, low bit only, no 278/280.
fh = aggro[aggro.index('static void GoblinFakehitCard'):]
fh = fh[:fh.index('// 83.0/84.12')]
assert '+ 0x274' in fh and '+ 0x27C' in fh
assert '(flagCur & 1u) == 0' in fh
assert 'flagCur + 1' in fh, 'flag write must preserve the per-card constant'
assert '+ 0x278' not in fh.replace('// +0x278', ''), 'counter must not be written'
assert 'WrSafe((void*)(card + 0x280)' not in fh, 'weight must not be written'
assert fh.count('PIN ROLLBACK goblin-fakehit') == 2

# PinRow dispatches fakehit by kind.
row = aggro[aggro.index('static void PinRow'):]
row = row[:row.index('static void PinSummary')]
assert 'GoblinFakehitCard(R, S, who, now, director)' in row
assert 'PinFakehitCard(R, S, who, now, director)' in row

# Goblin lease gets fakehit (including ALERT); suppress stays ALARM-only.
assert 'const bool goblinLease = directorActive' in aggro
assert '(directorAlarm || goblinLease)' in aggro
assert 'const bool activeSuppress = directorActive ? directorAlarm : s_pinSuppress;' in aggro

# Occupy check: goblin combat mode is fC=5 with the low-byte flag.
occ = aggro[aggro.index('static bool CombatOccupiesOther'):]
occ = occ[:occ.index('\n}', occ.index('{'))]
assert 'goblin' in occ and 'mode == 5' in occ and '(flag & 1u)' in occ

# Wolf write path is intact: wolf fakehit unchanged.
assert 'static void PinFakehitCard' in aggro
PY

# PartyStatus: discovery + child survey in one pass, read-only preserved.
python3 - "$PS_CPP" <<'PY'
import sys
ps = open(sys.argv[1], encoding='utf-8').read()
assert 'WrSafe' not in ps, 'PS must stay read-only'
assert 'kSurveyMaxNames' in ps
assert 'children (scan ' in ps
assert 'NameOfLiveObject' in ps
# The survey loop replaced the FindChildByClass call in discovery.
disc = ps[ps.index('static void DiscoverOne'):]
disc = disc[:disc.index('\n}', disc.rindex('size='))]
assert 'FindChildByClass' not in disc, 'discovery must be the survey pass'
PY
grep -Fq 'kSurveyMaxNames' "$PS_CPP"

# Offline fixtures: goblin card gate + downed/revive FSM regression.
g++ -std=c++11 -Wall -Wextra -Werror \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$GATE_FIXTURE" -o "$TMP/goblin_fakehit_test"
"$TMP/goblin_fakehit_test"
g++ -std=c++11 -Wall -Wextra -Werror \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$FSM_FIXTURE" -o "$TMP/partystatus_fsm_test"
"$TMP/partystatus_fsm_test"

# Touched product modules compile cleanly under the shim.
g++ -std=c++11 -fsyntax-only -Wall -Wextra \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$ROOT/tools/tcomp/aggro_t.cpp"
g++ -std=c++11 -fsyntax-only -Wall -Wextra \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$ROOT/tools/tcomp/director_t.cpp"
g++ -std=c++11 -fsyntax-only -Wall -Wextra \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$ROOT/tools/tcomp/partystatus_t.cpp"

echo "Build 84.17 goblin-fakehit contracts passed."

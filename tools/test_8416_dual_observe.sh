#!/usr/bin/env bash
# Build 84.16 dual-observe: два read-only прибора без единой записи.
#
#   GOBCARD — временной дифф карточек exact uEm0100 (адрес «свежего урона»
#             гоблина не копируется с волка, а измеряется);
#   PS      — статусные блоки партии (cStatus/cEffectStatusManager) +
#             downed/revive FSM (закрывает downedValid/downedRevivable).
#
# Контракты:
#   * build tag + README;
#   * оба прибора в продуктовом тике ПОД СВОИМ SEH и ДО гейта gameplay;
#   * read-only: ни WrSafe, ни записей в GOBCARD-регионе и в PartyStatus;
#   * wire: dinput8 Init/Shutdown, PartyRecon FillMemberStatus,
#     Director snapshot, vcxproj;
#   * offline FSM fixture (downed/revive последовательность).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="$ROOT/src/BuildTag.h"
README="$ROOT/README.md"
PS_CPP="$ROOT/src/runtime/PartyStatus.cpp"
PS_H="$ROOT/src/runtime/PartyStatus.h"
AGGRO="$ROOT/src/runtime/AggroWatch.cpp"
AGGRO_H="$ROOT/src/runtime/AggroWatch.h"
PAWN="$ROOT/src/PawnAI.cpp"
DINPUT="$ROOT/src/dinput8.cpp"
PARTY="$ROOT/src/runtime/PartyRecon.cpp"
DIRECTOR="$ROOT/src/monsterai/MonsterDirector.cpp"
PROJ="$ROOT/ddda-ai-overhaul.vcxproj"
FIXTURE="$ROOT/tools/tcomp/partystatus_fsm_test.cpp"
DOC1="$ROOT/docs/GOBLIN_CARD_DIFF_OBSERVE.md"
DOC2="$ROOT/docs/PARTY_STATUS_OBSERVE.md"
TMP="$(mktemp -d /tmp/dual_observe_8416.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# Identity and documentation must agree.
grep -Fq '84.' "$TAG"
grep -Fq '84.15-goblin-grab-hold' "$README"
grep -Fq '84.16-dual-observe' "$README"
grep -Fq '84.16-dual-observe' "$README"
grep -Fq '84.21-species-rage' "$README"
[ -f "$DOC1" ] && [ -f "$DOC2" ]

# Project files are registered.
grep -Fq 'src\runtime\PartyStatus.cpp' "$PROJ" 2>/dev/null \
  || grep -Fq 'PartyStatus.cpp' "$PROJ"
grep -Fq 'PartyStatus.h' "$PROJ"

# Both instruments run in the product tick under their own SEH, BEFORE the
# gameplay gate (must work with Director off, PackObserve precedent).
grep -Fq '#include "runtime/PartyStatus.h"' "$PAWN"
python3 - "$PAWN" <<'PY'
import sys
pawn = open(sys.argv[1], encoding='utf-8').read()
gate = pawn.index('if(!g_enabled || !pBase')
assert '__try { Runtime::Aggro::CardReconTick(); }' in pawn
assert '__try { Runtime::PartyStatus::Tick(); }' in pawn
assert pawn.index('CardReconTick') < gate, 'GOBCARD must run before gameplay gate'
assert pawn.index('PartyStatus::Tick') < gate, 'PS must run before gameplay gate'
PY

# Init/Shutdown wired in dinput8.
grep -Fq 'Runtime::PartyStatus::Init();' "$DINPUT"
grep -Fq 'Runtime::PartyStatus::Shutdown();' "$DINPUT"

# Party snapshot consumes the observer-confirmed downed fields.
grep -Fq 'Runtime::PartyStatus::FillMemberStatus(M.body, slot, M);' "$PARTY"

# Director manual snapshot dumps both instruments.
grep -Fq 'Runtime::Aggro::CardReconDump();' "$DIRECTOR"
grep -Fq 'Runtime::PartyStatus::DumpSnapshot();' "$DIRECTOR"

# Read-only contracts: no writes anywhere in the new code; the GOBCARD
# region of AggroWatch.cpp (between the section marker and the next PIN
# helper) must not reference WrSafe.
! grep -q 'WrSafe' "$PS_CPP"
! grep -q 'WrSafe' "$PS_H"
python3 - "$AGGRO" <<'PY'
import sys
aggro = open(sys.argv[1], encoding='utf-8').read()
start = aggro.index('// --- GOBCARD / CARDRECON')
end = aggro.index('// Одна карта: форма -> диапазон -> запись -> readback -> откат.')
gob = aggro[start:end]
assert 'WrSafe' not in gob, 'GOBCARD region must be read-only'
assert 'CardReconTick' in aggro and 'CardReconDump' in aggro
assert 'DiscoverSlots' in gob and 'ClassifySlots' in gob, 'recon must discover the roster, not hardcode it'
assert 'refmatch' in aggro, 'recon must cross-check the wolf reference layout'
# MARK and Aggro snapshot both trigger the full dump: first call lives in
# MarkEvent, last call lives in DumpSnapshot.
mark = aggro.index('void MarkEvent')
dumpfn = aggro.index('void DumpSnapshot')
call1 = aggro.index('CardReconDump();')
call2 = aggro.rindex('CardReconDump();')
assert mark < call1 < dumpfn < call2, 'GOBCARD dump must hook MARK and snapshot'
PY
grep -Fq 'CardReconTick' "$AGGRO_H"
grep -Fq 'CardReconDump' "$AGGRO_H"

# PS contract: anchor discovery, FSM actions, no named-status guessing.
grep -Fq 'cStatus' "$PS_CPP"
grep -Fq 'cEffectStatusManager' "$PS_CPP"
grep -Fq 'cPlReviveCMC' "$PS_CPP"
grep -Fq 'cPlActCmcNeardeath' "$PS_CPP"
grep -Fq 'cPlActDmgCrumbleDead' "$PS_CPP"
grep -Fq 'M.statusMask = 0' "$PARTY"
grep -q 'statusMask/statusValid остаются' "$PS_CPP" \
  || grep -q 'statusMask' "$PS_CPP"

# Syntax + offline FSM fixture (compiled, run).
g++ -std=c++11 -Wall -Wextra -Werror \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$FIXTURE" -o "$TMP/partystatus_fsm_test"
"$TMP/partystatus_fsm_test"

# GOBCARD + PS live inside modules whose syntax is already proven; re-check
# the two touched product modules compile cleanly under the shim.
g++ -std=c++11 -fsyntax-only -Wall -Wextra \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$ROOT/tools/tcomp/aggro_t.cpp"
g++ -std=c++11 -fsyntax-only -Wall -Wextra \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$ROOT/tools/tcomp/director_t.cpp"

echo "Build 84.16 dual-observe contracts passed."

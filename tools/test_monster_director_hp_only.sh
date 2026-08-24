#!/usr/bin/env bash
# Build 012 Monster Director regression: preserve absolute-HP PackMark and the
# Build 011 cue model while validating target+urgency and Tempo mobilization.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRODUCT="$ROOT/src/monsterai/MonsterDirector.cpp"
HEADER="$ROOT/src/monsterai/MonsterDirector.h"
CUES="$ROOT/src/monsterai/TacticalCues.cpp"
CUES_H="$ROOT/src/monsterai/TacticalCues.h"
TEMPO="$ROOT/src/runtime/MonsterTempo.cpp"
TEMPO_H="$ROOT/src/runtime/MonsterTempo.h"
AGGRO="$ROOT/src/runtime/AggroWatch.cpp"
TAG="$ROOT/src/BuildTag.h"
README="$ROOT/README.md"
DOC="$ROOT/docs/MONSTER_TARGETING_PROTOTYPE.md"
INI="$ROOT/ddda_ai_overhaul.ini"
DEF_INI="$ROOT/ddda_ai_overhaul.default.ini"
FIXTURE="$ROOT/tools/tcomp/director_moment_priority_test.cpp"
PARTY_FIXTURE="$ROOT/tools/tcomp/party_arisen_identity_test.cpp"
TEMPO_FIXTURE="$ROOT/tools/tcomp/monster_tempo_mobilization_test.cpp"
TMP="$(mktemp -d /tmp/director_build012.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# Identity and documentation must agree.
grep -Fq '84.21-species-rage' "$TAG"
grep -Fq '84.9-pilot012-urgency-mobilization' "$README"
grep -Fq '84.10-goblin-pack-observe' "$README"
grep -Fq '84.12-wolf-combat-card' "$README"
grep -Fq '84.13-leave-engaged' "$README"
grep -Fq '84.14-goblin-grab-pin' "$README"
grep -Fq '84.15-goblin-grab-hold' "$README"
grep -Fq '84.16-dual-observe' "$README"
grep -Fq '84.16-dual-observe' "$README"
grep -Fq '84.21-species-rage' "$README"
grep -Fq '84.9-pilot012-urgency-mobilization' "$DOC"
grep -q 'session Build 012' "$PRODUCT"
grep -q 'Build 012' "$HEADER"

# Retained strategic contract: rank by absolute current HP only. maxHp and all
# unvalidated core/loadout stats remain diagnostics, never scoring inputs.
grep -q 'm.currentHp' "$PRODUCT"
grep -q 'relative vulnerability = highest current HP / current' "$HEADER"
! grep -q 'm.currentHp / m.maxHp' "$PRODUCT"
! grep -q 'm.maxHp / m.currentHp' "$PRODUCT"
! grep -q 'coreDefense.*huntScore' "$PRODUCT"
! grep -q 'coreStrength.*huntScore' "$PRODUCT"
grep -q 'kMinHoldMs.*2500' "$PRODUCT"
grep -q 'ResetDecisionMemory("party-topology-reset")' "$PRODUCT"

# Universal data-defined cue recognition and exact taxonomy are preserved.
grep -q 'static const TacticalRule kRules\[\]' "$CUES"
grep -q '"cPlActGrabStart"' "$CUES"
grep -q '"cPlActHagaijime4Feet"' "$CUES"
grep -q '"cEm0200Lifted"' "$CUES"
grep -q '"uEm0200"' "$CUES"
grep -q 'GOBLIN-GRAB-ALERT' "$CUES"
grep -q '"cPlActHagaijime"' "$CUES"
grep -q '"uEm0100"' "$CUES"
grep -q 'TryGoblinEmptyCardWake' "$AGGRO"
grep -q 'EnsureGoblinRosterSlots' "$AGGRO"
grep -q 'kEm0100RosterBase' "$AGGRO"
grep -q 'TACTICAL_RESPONSE_ALERT' "$CUES"
grep -q 'TACTICAL_RESPONSE_ALARM' "$CUES"
grep -q '1.0f' "$CUES"
grep -q 'InspectTacticalContinuation' "$CUES"
grep -q 'Continuation never admits a new pair' "$CUES_H"
! grep -q 'kGroundPinProbeMs' "$PRODUCT"
! grep -q 'ProbePair' "$PRODUCT"

# Every admitted policy retains exact target + normalized urgency. Aggro consumes
# response-aware target state while Tempo owns exact responder envelopes.
grep -q 's_policyTargetBody' "$PRODUCT"
grep -q 's_policyUrgency' "$PRODUCT"
grep -q 'kEmergencyUrgency = 1.0f' "$PRODUCT"
grep -q 'policy-urgency-invalid' "$PRODUCT"
grep -q 'Runtime::Aggro::DirectorFocusSet(targetSlot, targetBody, excludedBody' "$PRODUCT"
grep -q 'DIRECTOR_RESPONSE_ALERT' "$PRODUCT"
grep -q 'DIRECTOR_RESPONSE_ALARM' "$PRODUCT"
grep -q 'Runtime::Tempo::AdmitDirectorMobilization' "$PRODUCT"
grep -q 'Runtime::Tempo::ReleaseDirectorMobilization' "$PRODUCT"
grep -q 'Runtime::Tempo::HardResetAllDirectorMobilization' "$PRODUCT"
grep -q 's_ownedWolf' "$PRODUCT"
grep -q 's_responderWolf' "$PRODUCT"
! grep -q 'Runtime::Tempo::SetOverride' "$PRODUCT"
! grep -q 'Runtime::Tempo::ClearOverride' "$PRODUCT"
! grep -q 'kPolicyLoco' "$PRODUCT"
! grep -q 'kPolicyAttack' "$PRODUCT"

# Exact exclusion and topology remain fail-closed.
grep -q 'v.body == excludedBody' "$PRODUCT"
grep -q 'wolf-pack-no-free-responder' "$PRODUCT"
grep -q 'goblin-no-free-responder' "$PRODUCT"
grep -q 'CollectEligibleResponders' "$PRODUCT"
grep -q 'card->tempoRage' "$PRODUCT"
grep -q 'policy-topology-changed' "$PRODUCT"
grep -q 'identity-slot-body-mismatch' "$PRODUCT"
grep -q 'identity-body-not-unique' "$PRODUCT"
grep -q 'Runtime::Tempo::DirectorReady' "$PRODUCT"
grep -q 'tempo-general-hook-missing' "$TEMPO"
grep -q 'tempo-animation-scope-not-attacks-only' "$TEMPO"
grep -q 'strcmp(v.kind, "uEm0200")' "$PRODUCT"
grep -q 'strcmp(kind, "uEm0200")' "$AGGRO"

# Product Tempo owns a separate bounded, non-ratcheting stable->rage table.
grep -q 'kMaxDirectorMobilizations = 16' "$TEMPO"
grep -q 'kDirectorDecayMs = 1400' "$TEMPO"
grep -q 'kWolfRageLocoLo = 1.20f' "$TEMPO"
grep -q 'kWolfRageLocoHi = 1.25f' "$TEMPO"
grep -q 'kWolfRageAnimLo = 1.20f' "$TEMPO"
grep -q 'kWolfRageAnimHi = 1.26f' "$TEMPO"
grep -q 'if (urgency > m.level) m.level = urgency' "$TEMPO"
grep -q 'fresh.stableLoco = FactorFor(body)' "$TEMPO"
grep -q 'fresh.stableAnim = AnimFactorFor(body)' "$TEMPO"
grep -q 'director-mobilization-baseline-outside-profile' "$TEMPO"
grep -q 'Missing controller refresh is unsafe, not ordinary evidence release' "$TEMPO"
grep -q 'immutable baseline -> Director envelope -> generic override' "$TEMPO"
grep -q 'float f = AnimFactorFor(body)' "$TEMPO"
grep -q 'DirectorMobilizationFor(body, 0, &f)' "$TEMPO"
grep -q 'f \*= oa' "$TEMPO"
grep -q 'float f = FactorFor(body)' "$TEMPO"
grep -q 'DirectorMobilizationFor(body, &f, 0)' "$TEMPO"
grep -q 'f \*= ol' "$TEMPO"

# Ordinary completion decays; unsafe lifecycle paths hard-reset. Generic
# overrides must not be globally cleared by Director.
grep -q 'OrdinaryIntentCompletion' "$PRODUCT"
grep -q 'ReleasePolicy(reason, !OrdinaryIntentCompletion(reason))' "$PRODUCT"
grep -q '"HARD-RESET" : "DECAY"' "$PRODUCT"
grep -q 'ReleasePolicy("actuator-off", true)' "$PRODUCT"
grep -q 'ReleasePolicy("director-disabled", true)' "$PRODUCT"
grep -q 'ReleasePolicy("shutdown", true)' "$PRODUCT"
! grep -q 'Runtime::Tempo::ClearAllOverrides' "$PRODUCT"
grep -q 'HardResetAllDirectorMobilization();' "$TEMPO"

# Consent and shipped stable tuning remain unchanged and synchronized.
python3 - "$INI" "$DEF_INI" <<'PY'
from pathlib import Path
import sys

a = Path(sys.argv[1]).read_text(encoding='utf-8')
b = Path(sys.argv[2]).read_text(encoding='utf-8')
assert a == b

def section(text, name):
    start = text.index('[' + name + ']')
    end = text.find('\n[', start + 1)
    return text[start:] if end < 0 else text[start:end]

tempo = section(a, 'monsterTempo')
monster = section(a, 'monsterAI')
for line in ('enabled = on', 'factorMin = 1.05', 'factorMax = 1.20',
             'animEnabled = on', 'animAttacksOnly = on',
             'animFactorMin = 1.05', 'animFactorMax = 1.15',
             'animCoupling = 0.00'):
    assert line in tempo, line
assert 'enabled = off' in monster and 'wolfActuator = off' in monster
PY

# Linked Director/Aggro/Tempo ownership fixture.
g++ -std=c++11 -Wall -Wextra -Werror \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$FIXTURE" "$CUES" -o "$TMP/director012_test"
"$TMP/director012_test"

LOG=/tmp/director_moment_priority_test.log
grep -q 'policy ENGAGED reason=tactical-grab-alert.*response=ALERT.*urgency=1.*responders=2 tempoOwned=2.*mobilization=HOLD' "$LOG"
grep -q 'policy ENGAGED reason=tactical-ground-pin-alarm.*response=ALARM.*urgency=1.*responders=2 tempoOwned=2.*mobilization=HOLD' "$LOG"
grep -q 'policy RELEASED reason=decision-none.*mobilization=DECAY' "$LOG"
grep -q 'policy RELEASED reason=hard-timeout.*mobilization=HARD-RESET' "$LOG"
grep -q 'policy RELEASED reason=tempo-general-hook-missing.*mobilization=HARD-RESET' "$LOG"
grep -q 'policy FAIL-CLOSED reason=identity-Arisen-body-unresolved-or-duplicate.*mobilization=HARD-RESET-ONCE' "$LOG"
grep -q 'policy RECOVERED priorFailClosed=identity-Arisen-body-unresolved-or-duplicate coalesced=6' "$LOG"
grep -q 'policy ENGAGED reason=tactical-goblin-grab-alert.*response=ALERT.*urgency=1.*responders=2 tempoOwned=2.*mobilization=HOLD' "$LOG"

# Direct PartyRecon fixture: DTI alone never claims Arisen. Exactly one body
# carrying the fixed player-record pointer wins; zero/multiple claims fail
# closed, and all four selected fixed slots fit the scratch table.
g++ -std=c++11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$PARTY_FIXTURE" -Wl,--gc-sections -o "$TMP/party_arisen012_test"
"$TMP/party_arisen012_test"

# Direct product Tempo fixture: immutable endpoints, refresh, composition,
# linear decay, hard reset/re-admission, TTL expiry, and capacity.
g++ -std=c++11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -DDDDA_TEMPO_PORTABLE_FIXTURE -include "$ROOT/tools/tcomp/tempo_stdafx.h" \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$TEMPO_FIXTURE" "$TEMPO" -Wl,--gc-sections -o "$TMP/tempo012_test"
"$TMP/tempo012_test"

# 84.21: rage-профили — поля карточки вида; Director регистрирует их в Tempo;
# гоблин: разгон атаки сильнее, чем локомоции; волк — проверенный профиль.
python3 - "$ROOT" <<'PY2'
import re, sys
root = sys.argv[1]
card = open(root + "/src/monsterai/SpeciesCard.h", encoding="utf-8").read()
assert "rageLocoLo, rageLocoHi" in card and "rageAnimLo, rageAnimHi" in card
w = re.search(r'uEm0200.*?1\.20f, 1\.25f, 1\.20f, 1\.26f', card, re.S)
g = re.search(r'uEm0100.*?1\.21f, 1\.24f, 1\.32f, 1\.40f', card, re.S)
assert w and g, "species rage profiles missing/changed"
tempo_h = open(root + "/src/runtime/MonsterTempo.h", encoding="utf-8").read()
tempo = open(root + "/src/runtime/MonsterTempo.cpp", encoding="utf-8").read()
director = open(root + "/src/monsterai/MonsterDirector.cpp", encoding="utf-8").read()
assert "RegisterRageProfile" in tempo_h and "RegisterRageProfile" in tempo
assert "FindRageProfile(exactKind)" in tempo
assert "prof->locoLo + (prof->locoHi - prof->locoLo)" in tempo, "roll must come from the species profile"
assert "Runtime::Tempo::RegisterRageProfile(card->kind" in director, "Director must register card profiles"
PY2
echo "Monster Director Build 012 urgency/mobilization regression passed."

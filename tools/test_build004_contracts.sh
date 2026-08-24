#!/usr/bin/env bash
# Build 012 static preservation contracts: retain Builds 004-008 while
# validating exact fixed-slot identity, ownership, readiness and cleanup.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
director = (root / 'src/monsterai/MonsterDirector.cpp').read_text(encoding='utf-8')
header = (root / 'src/monsterai/MonsterDirector.h').read_text(encoding='utf-8')
tempo = (root / 'src/runtime/MonsterTempo.cpp').read_text(encoding='utf-8')
aggro = (root / 'src/runtime/AggroWatch.cpp').read_text(encoding='utf-8')
party = (root / 'src/runtime/PartyRecon.cpp').read_text(encoding='utf-8')
runtime_internal = (root / 'src/runtime/RuntimeInternal.h').read_text(encoding='utf-8')
haste = (root / 'src/pawnai/PawnHaste.cpp').read_text(encoding='utf-8')
pawnai = (root / 'src/PawnAI.cpp').read_text(encoding='utf-8')
goap = (root / 'src/devtools/GoapProbe.cpp').read_text(encoding='utf-8')
tag = (root / 'src/BuildTag.h').read_text(encoding='utf-8')
readme = (root / 'README.md').read_text(encoding='utf-8')
doc = (root / 'docs/MONSTER_TARGETING_PROTOTYPE.md').read_text(encoding='utf-8')
dinput = (root / 'src/dinput8.cpp').read_text(encoding='utf-8')

assert '84.15-goblin-grab-hold' in tag
assert '84.9-pilot012-urgency-mobilization' in readme
assert '84.10-goblin-pack-observe' in readme
assert '84.11-arisen-record-glue' in readme
assert '84.12-wolf-combat-card' in readme
assert '84.13-leave-engaged' in readme
assert '84.14-goblin-grab-pin' in readme
assert '84.15-goblin-grab-hold' in readme
assert 'Build 012' in doc

# Build 004 HP-only ranking/hold/axes stay intact.
assert 'float TargetIsolationRatio()' in header
assert 'float TargetDepthRatio()' in header
assert 'return (s_score[mark].huntScore - r) / r;' in director
assert 's_score[mark].huntScore - 1.0f' in director
assert 'isolation=' in director and 'targetDepth=' in director
assert 'focusIntent=' in director

# Build 012 retains consent and fail-closed gates.
assert 'wolfActuator", false' in director
assert 'if (!s_actuatorEnabled)' in director
assert 'ExactPartyIdentity' in director
for slot in ('Arisen', 'MainPawn', 'Hired1', 'Hired2'):
    assert f'identity-{slot}-snapshot-body-unresolved' in director
assert 'ResolveMemberBodyStatus' in director
assert 'identity-slot-body-mismatch' in director
assert 'identity-body-not-unique' in director
assert 'Runtime::Tempo::DirectorReady' in director
assert 'Runtime::Aggro::DirectorFocusSet' in director
assert 'Runtime::Tempo::AdmitDirectorMobilization' in director
assert 'Runtime::Tempo::ReleaseDirectorMobilization' in director
assert 'Runtime::Tempo::HardResetAllDirectorMobilization' in director
assert 'Runtime::Tempo::SetOverride' not in director
assert 'Runtime::Tempo::ClearOverride' not in director
assert 'Runtime::Tempo::ClearAllOverrides' not in director
assert 's_policyTargetBody' in director and 's_policyUrgency' in director
assert 'ReleasePolicy("shutdown", true)' in director
assert 'ReleasePolicy("director-disabled", true)' in director
assert 'decision-bias' in director and 'decision-none' in director
assert 's_inactiveResetLatched' in director
assert 'policy FAIL-CLOSED reason=' in director
assert 'policy RECOVERED priorFailClosed=' in director
assert 'mobilization=HARD-RESET-ONCE' in director
assert 'wolf-pack-lost' in director
assert 'GameplayWriteCount() { return s_gameplayWrites; }' in director

# PartyRecon uses only exact fixed-slot evidence. The validated current-HP
# bridge must be stable and bit-exact; duplicate claims and all order fallbacks
# remain unresolved. The speculative max-stamina pair scan must stay gone.
assert 'info + 0x29C' in party and 'rec + 0x96C' in party
assert 'liveBefore != liveAfter' in party
assert 'recBefore[r] != recAfter[r]' in party
assert 'recBefore[r] == liveBefore' in party
assert 'if (hits == 1) *bodyOut = unique;' in party
assert 'PartyRetryUnresolvedPawnIdentity' in party
assert 'now - last < 1000u' in party
assert 'direct-pointer-ambiguous' in party
assert 'direct-pointer-conflict' in party
assert 'pointer-current-hp-conflict' in party
assert 'PartyRecordInfo(0, 0, 0, &body)' in party
assert 'PartyIsRecordBackedArisen' in party
assert 'PartyClaimUniqueArisen' in party
assert 'unique-live-uPlayer' in party
assert 'cPlayerInfo' in party
assert 'P.playerRecordRef' in party
assert 'if (!PartyIsRecordBackedArisen(g_party[i])) continue;' in party
assert 'unresolved: no player-record-pointer' in party
assert 'skipEmptyHired' in director
assert 'identity-occupied-exact' in director
assert 'if (duplicateFixedClaim) return;' in party
assert 'kPartyExactSlots     = 4' in runtime_internal
assert 'g_partyChosen[kPartyExactSlots]' in runtime_internal
assert 'falling back to the first one' not in party
assert 'float maxSt' not in party and '0x97C' not in party

# Consumers that address one fixed pawn slot must not mistake the compact
# public enumeration ordinal for a record index. Generic all-pawn loops may
# still use PawnBodyAt().
assert 'ExactPawnBodyForRecord' in pawnai
assert 'PartyRecordInfo(idx, 0, 0, &body)' in pawnai
assert 'PawnBodyAt(g_hst.idx' not in pawnai
assert 'PawnBodyAt(idx, &im)' not in pawnai
assert 'PartyRecordInfo(s_probePawn, 0, 0, &b)' in goap
assert 'if (!b) b = Runtime::MainPawnBody()' not in goap
assert 'return 0;' in goap[goap.index('static uintptr_t ProbeBody()'):goap.index('static bool ProbeIdentityOk()')]

# Aggro uses fixed party-record indices, exposes slot-specific availability,
# and validates the expected target body before every product mutation.
assert 'PartyRecordInfo(recordIdx, 0, 0, &body)' in aggro
assert 'MEMBER_MAIN + recordIdx' in aggro
assert 'ResolveMemberBodyStatus' in aggro
for slot in ('Arisen', 'MainPawn', 'Hired1', 'Hired2'):
    assert f'identity-{slot}-body-unresolved-or-duplicate' in aggro
assert 'fixed-slot identity availability' in aggro
assert 'DirectorIdentityExactNow()' in aggro
assert 'if (director && !DirectorIdentityExactNow())' in aggro
assert 'LiveWolfCardMode' in aggro
assert 'kCombatPinValue' in aggro
assert 'not live 1/4 or 1/2' in aggro
assert 'if (mode == 2)' in aggro
assert 'CombatOccupiesOther' in aggro
assert 'leave-engaged' in aggro
assert '  left ' in aggro
assert 'if (director && CombatOccupiesOther' in aggro
assert 's_pinMember = MEMBER_NONE;' in aggro
assert 'product lease owns the actuator' in aggro
assert 'TryGoblinEmptyCardWake' in aggro
assert 'EnsureGoblinRosterSlots' in aggro
assert 'kEm0100RosterBase   = 0x2FA0' in aggro
assert 'kEm0100RosterStride = 0x28C' in aggro
assert 'goblin-card-wake' in aggro
assert '0/0 -> 1/4 att=300 w=1.0' in aggro
assert 'kMaxSlots    = 12' in (root / 'src/runtime/AggroWatch.h').read_text(encoding='utf-8')

# Readiness explicitly reports all four required facts.
assert 'r.movementEnabled = g_enabled;' in tempo
assert 'r.generalHookInstalled = pHkWalk != nullptr;' in tempo
assert 'r.animationEnabled = g_animEnabled;' in tempo
assert 'r.attacksOnly = (g_animScope == kScopeAttack);' in tempo
assert 'tempo-general-hook-missing' in tempo
assert 'tempo-animation-scope-not-attacks-only' in tempo

# Detach order releases Director/Aggro ownership before Tempo shutdown and has
# no new waits/joins in the cleanup block.
assert dinput.index('MonsterAI::Shutdown();') < dinput.index('Runtime::Aggro::Shutdown();')
assert dinput.index('Runtime::Aggro::Shutdown();') < dinput.index('Runtime::Shutdown();')

# Existing sparse tempo/haste logs remain bounded.
assert 'Tempo: attack scope applied on entry' in tempo
assert 'actionChanged && t.actIsAttack' in tempo
assert 'LooksAttackLikeForDiagnostics' in tempo
assert 'diagnostic only, gameplay=other' in tempo
assert 'g_animEnrolls <= 3' in tempo
assert 'g_animRestores <= 2' in tempo
assert 'Tempo: diagnostics summary' in tempo
assert 's_actLogged < 3' in haste
assert 'PawnHaste: burst detail limit reached' in haste
assert 'PawnHaste: session summary' in haste

# Shipped Director sections must remain byte-for-byte synchronized and default off.
def section(path):
    s = path.read_text(encoding='utf-8')
    a = s.index('[monsterAI]')
    b = s.index('\n[aggro]', a)
    return s[a:b]
a = section(root / 'ddda_ai_overhaul.ini')
b = section(root / 'ddda_ai_overhaul.default.ini')
assert a == b
assert 'enabled = off' in a and 'wolfActuator = off' in a

print('Build 004-008 contracts retained in Build 012: PASS (exact slots; HP observer; mobilization cleanup)')
PY

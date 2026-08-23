#!/usr/bin/env bash
# Focused static contracts for the Build 005 locomotion proof patch.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ROOT" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
tempo = (root / 'src/runtime/MonsterTempo.cpp').read_text(encoding='utf-8')
header = (root / 'src/runtime/MonsterTempo.h').read_text(encoding='utf-8')
ui = (root / 'src/EnemyAI.cpp').read_text(encoding='utf-8')
tag = (root / 'src/BuildTag.h').read_text(encoding='utf-8')
director = (root / 'src/monsterai/MonsterDirector.cpp').read_text(encoding='utf-8')

# Build 012 is a full build over (and must retain) the completed Build 005 proof.
assert '84.9-pilot012-urgency-mobilization' in tag

walk = re.search(
    r'static void __declspec\(naked\) HMoveWalk\(\).*?'
    r'(?=// ---------------------------------------------------------------------------\n// Хук спринта)',
    tempo, re.S)
sprint = re.search(
    r'static void __declspec\(naked\) HMoveSprint\(\).*?'
    r'(?=// ---------------------------------------------------------------------------\n// Множитель конкретного монстра)',
    tempo, re.S)
assert walk and sprint, 'movement hook bodies not found'
walk = walk.group(0)
sprint = sprint.group(0)

# The naked hot paths may only touch preallocated POD telemetry. They must
# retain the actual multiplication before publishing the matching receipt.
for body in (walk, sprint):
    assert 'logFile' not in body and 'sprintf' not in body and 'new ' not in body
    assert '[g_tempoEnemyEnd]' in body
assert walk.index('mulss xmm1, xmm0') < walk.index('[g_locoGeneralEnemyHits]')
assert '[g_lastLocoGeneralFactorBits]' in walk
assert '[g_lastLocoGeneralEnemyBody]' in walk
assert walk.count('[g_locoGeneralReceiptSeq]') == 2
assert sprint.index('mulss xmm2, xmm0') < sprint.index('[g_locoSprintEnemyHits]')
assert '[g_lastLocoSprintFactorBits]' in sprint
assert '[g_lastLocoSprintEnemyBody]' in sprint
assert sprint.count('[g_locoSprintReceiptSeq]') == 2
assert 'ReadLocomotionReceipt' in tempo and 'before != after' in tempo

# Product code—not asm—resolves context and emits one first-hit receipt per
# path plus aggregate counters on manual request or shutdown.
assert 'Tempo: locomotion applied first path=%s enemyHit=%u body=0x%08X' in tempo
assert 'GENERAL(dash/run/walk)' in tempo
assert 'SPRINT(optional)' in tempo
assert 'kind=%s tickAct=%s factor=x%.6f bits=0x%08X' in tempo
assert 'Tempo: locomotion summary reason=%s generalEnemyHits=%u' in tempo
assert 'sprintEnemyHits=%u firstGeneral=%d firstSprint=%d' in tempo
assert 'LocomotionProofTick();' in tempo
assert 'DumpLocomotionDiagnostics("shutdown");' in tempo
assert 'void        DumpLocomotionDiagnostics(const char* reason);' in header
assert 'DumpLocomotionDiagnostics("manual")' in ui

# The generic locomotion hook is the acceptance path; sprint remains separate.
assert 'general locomotion hook (dash/run/walk)' in ui
assert 'separate sprint hook' in ui
assert 'Need both hooks' not in ui

# Build 012 must preserve attack scoping and the default-off observer receipt,
# while connecting only the explicitly gated pilot.
assert 'actionChanged && t.actIsAttack' in tempo
assert 'observerOnly=' in director and 'writes=0' in director
assert 'wolfActuator", false' in director
assert 'Runtime::Tempo::AdmitDirectorMobilization' in director
assert 'Runtime::Tempo::ReleaseDirectorMobilization' in director
assert 'Runtime::Tempo::HardResetAllDirectorMobilization' in director
assert 'Runtime::Tempo::SetOverride' not in director
assert 'Runtime::Tempo::ClearOverride' not in director
assert 'Runtime::Tempo::ClearAllOverrides' not in director

print('Build 005 locomotion proof retained in Build 012: PASS')
PY

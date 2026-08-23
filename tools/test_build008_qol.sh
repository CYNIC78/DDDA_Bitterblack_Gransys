#!/usr/bin/env bash
# Build 008 contracts retained in Build 012: accepted stable Tempo profile, live
# toggle, quiet routine telemetry, and bounded Director mobilization above it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
tag = (root / 'src/BuildTag.h').read_text(encoding='utf-8')
tempo = (root / 'src/runtime/MonsterTempo.cpp').read_text(encoding='utf-8')
tempo_h = (root / 'src/runtime/MonsterTempo.h').read_text(encoding='utf-8')
ui = (root / 'src/EnemyAI.cpp').read_text(encoding='utf-8')
aggro = (root / 'src/runtime/AggroWatch.cpp').read_text(encoding='utf-8')
wand = (root / 'src/pawnai/WandRange.cpp').read_text(encoding='utf-8')
director = (root / 'src/monsterai/MonsterDirector.cpp').read_text(encoding='utf-8')
readme = (root / 'README.md').read_text(encoding='utf-8')
doc = (root / 'docs/MONSTER_TARGETING_PROTOTYPE.md').read_text(encoding='utf-8')

assert '84.9-pilot012-urgency-mobilization' in tag
assert '84.9-pilot012-urgency-mobilization' in readme
assert '84.9-pilot012-urgency-mobilization' in doc

# Parse only active key/value lines from the Tempo section.
def tempo_values(path):
    text = path.read_text(encoding='utf-8')
    a = text.index('[monsterTempo]')
    b = text.index('\n[', a + 1)
    values = {}
    for raw in text[a:b].splitlines()[1:]:
        line = raw.strip()
        if not line or line.startswith(('#', ';')) or '=' not in line:
            continue
        k, v = line.split('=', 1)
        values[k.strip()] = v.strip()
    return values

expected = {
    'enabled': 'on',
    'factorMin': '1.05',
    'factorMax': '1.20',
    'hookWalk': 'on',
    'hookSprint': 'off',
    'animEnabled': 'on',
    'animAttacksOnly': 'on',
    'animFactorMin': '1.05',
    'animFactorMax': '1.15',
    'animCoupling': '0.00',
}
installed = tempo_values(root / 'ddda_ai_overhaul.ini')
shipped = tempo_values(root / 'ddda_ai_overhaul.default.ini')
for key, value in expected.items():
    assert installed.get(key) == value, (key, installed.get(key), value)
    assert shipped.get(key) == value, (key, shipped.get(key), value)
assert installed == shipped

# Missing settings are backfilled from code, so source fallbacks must match.
for needle in (
    'getBool("monsterTempo", "enabled", true)',
    'getFloat("monsterTempo", "factorMin", 1.05f)',
    'getFloat("monsterTempo", "factorMax", 1.20f)',
    'getBool("monsterTempo", "hookSprint", false)',
    'getBool("monsterTempo", "animEnabled", true)',
    'getBool("monsterTempo", "animAttacksOnly", true)',
    'getFloat("monsterTempo", "animFactorMin", 1.05f)',
    'getFloat("monsterTempo", "animFactorMax", 1.15f)',
    'getFloat("monsterTempo", "animCoupling", 0.0f)',
):
    assert needle in tempo, needle

# OFF-at-start remains live-switchable after restart: configured hooks install
# independently, while RefreshTable still gates ordinary enemies on g_enabled.
assert 'void SetEnabled(bool on);' in tempo_h
assert 'void SetEnabled(bool on)' in tempo
setter = tempo[tempo.index('void SetEnabled(bool on)'):tempo.index('void SetRange(', tempo.index('void SetEnabled(bool on)'))]
assert 'g_enabled = on;' in setter and 'RefreshTable();' in setter
assert 'ClearOverride' not in setter and 'ClearAllOverrides' not in setter
assert 'const bool wantHooks = g_hookWalk || g_hookSprint;' in tempo
assert 'const bool wantHooks = g_enabled' not in tempo
assert 'for (int i = 0; g_enabled && i < g_nAct' in tempo
assert 'enable movement tempo##mt' in ui
assert 'Runtime::Tempo::SetEnabled(movementOn);' in ui
assert 'config.setBool("monsterTempo", "enabled", movementOn);' in ui

# Routine success detail and shape census are explicit research verbosity.
assert '[aggro] logEvents=on explicitly restores this detail' in aggro
assert 'if (!s_logEvents) return 0;' in aggro
assert 'if (!s_logEvents) return;' in aggro
shape = aggro[aggro.index('static void PinShapeDump'):aggro.index('static void PinRow')]
assert 'if (!s_logEvents) return;' in shape
# Genuine anomalies, bounded summaries, rollbacks and a footer stay automatic.
anomaly = aggro[aggro.index('static void PinAnomaly'):aggro.index('static bool DirectorIdentityExactNow')]
assert 's_pinUnsafeSkips' in anomaly and '10000' in anomaly
assert 'Aggro: PIN anomaly @' in anomaly and 's_logEvents' not in anomaly
assert 'Aggro: PIN ROLLBACK' in aggro
assert 'unsafeSkips' in aggro
assert 'Aggro: shutdown summary' in aggro
assert 's_pinLastLog && now - s_pinLastLog < 10000' in aggro

# Ephemeral WandRange action objects no longer repeat unchanged band output.
assert 'WandRange: first ' in wand
assert 'LogBands("first applied")' in wand
assert 'WandRange: waiting: ' in wand
assert 'further unchanged retries counted silently' in wand
assert 'WandRange: shutdown summary' in wand
assert 'none patchable (see log)' not in wand
assert 'logFile << "WandRange: " << s_why' not in wand

# The shipped stable profile remains byte-level unchanged. Build 012 replaces
# only the old fixed Director override with bounded personal rage endpoints.
assert 'kWolfRageLocoLo = 1.20f' in tempo
assert 'kWolfRageLocoHi = 1.25f' in tempo
assert 'kWolfRageAnimLo = 1.20f' in tempo
assert 'kWolfRageAnimHi = 1.26f' in tempo
assert 'AdmitDirectorMobilization' in director
assert 'kPolicyLoco = 1.03f' not in director
assert 'kPolicyAttack = 0.96f' not in director
assert 'ExactPartyIdentity' in director
assert 'Runtime::Tempo::ClearAllOverrides' not in director
for action in ('AttackRun', 'JumpBite', 'DownBite', 'Bite', 'ContinueBite'):
    assert action in (root / 'src/ActMap.Generated.h').read_text(encoding='utf-8')

print('Build 008 QoL retained in Build 012: PASS (stable profile, live toggle, bounded evidence)')
PY

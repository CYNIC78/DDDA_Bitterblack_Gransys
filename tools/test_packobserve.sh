#!/usr/bin/env bash
# PackObserve night-instrument fixture + static write-path isolation.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/packobserve.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
obs = (root / 'src/monsterai/PackObserve.cpp').read_text(encoding='utf-8')
card = (root / 'src/monsterai/SpeciesCard.h').read_text(encoding='utf-8')
director = (root / 'src/monsterai/MonsterDirector.cpp').read_text(encoding='utf-8')
pawn = (root / 'src/PawnAI.cpp').read_text(encoding='utf-8')
ui = (root / 'src/EnemyAI.cpp').read_text(encoding='utf-8')
proj = (root / 'ddda-ai-overhaul.vcxproj').read_text(encoding='utf-8')
tag = (root / 'src/BuildTag.h').read_text(encoding='utf-8')

assert '84.37-xmm-params' in tag
aggro = (root / 'src/runtime/AggroWatch.cpp').read_text(encoding='utf-8')
assert 'TryGoblinEmptyCardWake' in aggro
assert 'EnsureGoblinRosterSlots' in aggro
assert 'kEm0100RosterBase   = 0x2FA0' in aggro
assert '"uEm0100", 29632u, true, true' in card
assert '1.21f, 1.24f, 1.32f, 1.40f' in card  # goblin rage profile (84.21)
assert '"uEm0200", 29888u, true, true,  true' in card
assert 'strcmp(kind, expect)' in card
assert 'PackObserveIngest' in obs
assert 'exact uEm0100' in obs
assert 'AdmitDirectorMobilization' not in obs
assert 'DirectorFocusSet' not in obs
assert 'SetOverride' not in obs
assert 'DevTools' not in obs
assert 'PackObserveInit();' in director
assert 'PackObserveShutdown();' in director
assert 'PackObserveDump();' in director
assert 'PackObserveTick()' in pawn
assert 'WorldScan_Tick' in pawn
assert pawn.index('PackObserveTick') < pawn.index('if(!g_enabled || !pBase')
assert 'TextDisabled("%s", MonsterAI::PackObserveStatus())' in ui
assert 'enable pack observe' not in ui.lower()
assert 'PackObserve.cpp' in proj
assert 'SpeciesCard.h' in proj
assert 'PackObserve.h' in proj
# Wolf Tempo admit stays exact uEm0200. Goblin grab is pin-only, no rage.
assert 'PolicyResponderKind' in director
assert 'CollectEligibleResponders' in director
assert 'card->tempoRage' in director
assert 'goblin-no-free-responder' in director
assert 'AdmitDirectorMobilization(' in director and '"uEm0200"' in director
print('PackObserve static isolation: PASS')
PY

g++ -std=c++11 -Wall -Wextra -Werror \
  -DDDDA_PACKOBSERVE_PORTABLE \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" \
  "$ROOT/tools/tcomp/packobserve_t.cpp" -o "$TMP/packobserve_t"
"$TMP/packobserve_t"
echo "PackObserve fixture passed."

#!/usr/bin/env bash
# Build 012 preservation: deterministic ActMap and live-validated wolf attacks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/ddda_act_map_build004.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

python3 "$ROOT/tools/generate_act_map.py" \
  "$ROOT/src/TypeAtlas.Generated.h" "$TMP/ActMap.Generated.h"
cmp "$ROOT/src/ActMap.Generated.h" "$TMP/ActMap.Generated.h"

python3 - "$ROOT/src/ActMap.Generated.h" "$ROOT/tools/generate_act_map.py" <<'PY'
import re
import sys

header = open(sys.argv[1], encoding='utf-8').read()
generator = open(sys.argv[2], encoding='utf-8').read()

m = re.search(r'static const int kCount = (\d+);', header)
assert m and int(m.group(1)) == 873, m.group(1) if m else 'missing kCount'

expected = {
    'cEm0200AttackRun': ('AttackRun', 200, '0x11A6710', 120, 'attack'),
    'cEm0200Bite': ('Bite', 200, '0x11A6758', 124, 'attack'),
    'cEm0200ContinueBite': ('ContinueBite', 200, '0x11A6830', 160, 'attack'),
    'cEm0200JumpBite': ('JumpBite', 200, '0x11A6878', 256, 'attack'),
    'cEm0200DownBite': ('DownBite', 200, '0x11A68C0', 192, 'attack'),
}
row_re = re.compile(
    r'\{ "([^"]+)", "([^"]+)", (\d+), (0x[0-9A-F]+), (\d+), "([^"]+)" \},')
rows = {full: (short, int(emid), vt, int(size), category)
        for short, full, emid, vt, size, category in row_re.findall(header)}
for name, want in expected.items():
    assert rows.get(name) == want, (name, rows.get(name), want)
    assert repr(name) in generator, 'missing explicit allowlist entry: ' + name

# The broad cEm<ID><anything> fallback remains forbidden: only the named
# live-validated legacy set may bypass the normal ...Act... pattern.
assert 'LIVE_VALIDATED_LEGACY_ACTIONS' in generator
assert "'cEm0200Action'" not in generator
attack_count = sum(1 for row in rows.values() if row[4] == 'attack')
assert attack_count == 216, attack_count
print('ActMap retained in Build 012: PASS (deterministic, 873 states, 216 attacks, five explicit wolf attacks)')
PY

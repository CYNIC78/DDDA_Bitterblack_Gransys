#!/usr/bin/env bash
# Защита рабочих конфигов игрока от затирания при распаковке зипа (85.66).
#
# Почему контракт, а не вкусовщина: zip распаковывают ПОВЕРХ папки игры, а
# копирование файла затирает ручные значения — те самые ×3 атака/защита.
# Убирать конфиги из git нельзя: владелец диктует правки, источник подсказок —
# git diff. Поэтому правило узкое: файлы остаются в git, но не едут в архив.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "$ROOT" <<'PY'
import importlib.util
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("package_build", root / "tools/package_build.py")
pb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pb)

owner = set(pb.OWNER_CONFIGS)
packed = set(pb.files_to_package())

# 1. Конфиги игрока не попадают в пакет.
leaked = sorted(owner & packed)
assert not leaked, "в zip попали рабочие конфиги игрока: %s" % leaked

# 2. Эталоны и примеры в пакет входят: свежему игроку есть что взять.
for must in ("ddda_ai_overhaul.default.ini", "ddda_music_map.example.ini"):
    assert must in packed, "в zip нет %s — свежему игроку неоткуда взять настройки" % must

# 3. В git конфиги остаются: диктовка правок и git diff должны работать.
tracked = set(subprocess.check_output(
    ["git", "ls-files"], cwd=str(root), universal_newlines=True).splitlines())
for name in sorted(owner):
    assert name in tracked, "%s пропал из git — сломается диктовка правок через diff" % name

# 4. У каждой причины есть текст: исключение без причины — забывчивость.
for name, why in sorted(pb.OWNER_CONFIGS.items()):
    assert why and len(why) > 10, "%s: причина не записана" % name

print("owner ini protection: PASS (%d конфигов не в пакете, в git остаются; эталоны на месте)"
      % len(owner))
PY

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bump_build.py — поднять MOD_BUILD_TAG и проверить слои.

Audit 2026-09-21:
  * tag auto-bump (84.98-audio-frozen -> 84.99-audio-frozen, или --set X)
  * analyze_devtools_layers check — не даёт упаковать билд с нарушением слоёв
    (продуктовый код дергает DevTools::).

Использование:
  python3 tools/bump_build.py           # auto bump minor
  python3 tools/bump_build.py --set 85.00
  python3 tools/bump_build.py --set 85.00-combat-stats --no-check
"""

from __future__ import annotations
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TAG_H = ROOT / "src" / "BuildTag.h"
LAYERS_SCRIPT = ROOT / "tools" / "analyze_devtools_layers.py"

TAG_RE = re.compile(r'#define\s+MOD_BUILD_TAG\s+"([^"]+)"')
# 84.98-audio-frozen  -> groups: (84.98, -audio-frozen)
# 84.98.1             -> (84.98.1, '')
VER_SPLIT = re.compile(r'^([0-9]+(?:\.[0-9]+)+)(.*)$')

def read_tag() -> str:
    txt = TAG_H.read_text(encoding='utf-8')
    m = TAG_RE.search(txt)
    if not m:
        raise SystemExit("MOD_BUILD_TAG не найден в src/BuildTag.h")
    return m.group(1)

def write_tag(new_tag: str):
    txt = TAG_H.read_text(encoding='utf-8')
    if not TAG_RE.search(txt):
        raise SystemExit("MOD_BUILD_TAG не найден")
    new_txt = TAG_RE.sub(f'#define MOD_BUILD_TAG "{new_tag}"', txt)
    TAG_H.write_text(new_txt, encoding='utf-8')
    print(f"BuildTag: {read_tag()} -> {new_tag} (written)")

def bump_tag(cur: str) -> str:
    """
    84.98-audio-frozen -> 84.99-audio-frozen
    84.98               -> 84.99
    84.98.3             -> 84.98.4
    Если не парсится — добавить .1
    """
    m = VER_SPLIT.match(cur)
    if not m:
        return cur + ".1"
    ver, suffix = m.group(1), m.group(2)
    parts = ver.split('.')
    # increment last numeric part
    try:
        parts[-1] = str(int(parts[-1]) + 1)
    except ValueError:
        parts.append("1")
    return ".".join(parts) + suffix

def run_layers_check() -> bool:
    if not LAYERS_SCRIPT.exists():
        print(f"WARN: {LAYERS_SCRIPT} не найден, пропуск проверки слоёв")
        return True
    print(f"Running layers check: {LAYERS_SCRIPT.name} ...")
    res = subprocess.run([sys.executable, str(LAYERS_SCRIPT)], cwd=str(ROOT))
    if res.returncode != 0:
        print("FAIL: analyze_devtools_layers обнаружил нарушение слоёв!")
        print("  Продуктовый код дергает DevTools:: — билд упаковывать нельзя.")
        return False
    print("OK: слои чистые (продуктовый код не зависит от DevTools)")
    return True

def main():
    ap = argparse.ArgumentParser(description="Bump MOD_BUILD_TAG + layers check")
    ap.add_argument("--set", dest="set_tag", help="установить тег явно, напр. 85.00-combat-stats")
    ap.add_argument("--no-check", action="store_true", help="не запускать analyze_devtools_layers")
    args = ap.parse_args()

    cur = read_tag()
    print(f"Current MOD_BUILD_TAG: {cur}")

    if args.set_tag:
        new_tag = args.set_tag.strip()
        if not re.fullmatch(r"[0-9A-Za-z._-]+", new_tag):
            raise SystemExit(f"Недопустимый тег: {new_tag}")
    else:
        new_tag = bump_tag(cur)
        print(f"Auto-bumped -> {new_tag}")

    if not args.no_check:
        if not run_layers_check():
            print("\nОтмена: сначала почините слои (см. docs/DEVTOOLS_LAYER_MAP.md)")
            sys.exit(1)

    write_tag(new_tag)
    print(f"\nГотово. Новый тег: {new_tag}")
    print("Далее: запишите изменения в CHANGELOG.md (секция Текущий milestone) и")
    print("  python3 tools/package_build.py")

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""package_build.py — упаковка билда в zip (source package).

Правило (docs/BUILD_INSTRUCTIONS_RU.md §7):
  * билд = zip в builds/, имя = {MOD_BUILD_TAG}.zip (кратко, префикс = номер);
  * перед упаковкой поднять MOD_BUILD_TAG и записать изменения в CHANGELOG.md
    (секция «Текущий milestone») — её текст попадает в MANIFEST.txt «Состав»;
  * пакет = отслеживаемые файлы (git ls-files) — без .git, мусора сборки,
    чужих zip; MANIFEST.sha256 сам себя не хеширует;
  * MANIFEST.txt / MANIFEST.sha256 пересоздаются каждый раз.
"""

import hashlib
import os
import re
import subprocess
import sys
import zipfile
from datetime import datetime, timezone, timedelta

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_TAG_H = os.path.join(ROOT, "src", "BuildTag.h")
CHANGELOG = os.path.join(ROOT, "CHANGELOG.md")
MANIFEST_TXT = os.path.join(ROOT, "MANIFEST.txt")
MANIFEST_SHA = os.path.join(ROOT, "MANIFEST.sha256")
BUILDS_DIR = os.path.join(ROOT, "builds")

MSK = timezone(timedelta(hours=3))  # Europe/Moscow

# Файлы пакета, которые пишутся явно и не хешируются/не попадают в общий
# список (MANIFEST.sha256 не хеширует сам себя; MANIFEST.txt кладётся в архив
# отдельно, чтобы не дублироваться).
SELF_EXCLUDE = {"MANIFEST.sha256", "MANIFEST.txt"}


def read_build_tag():
    with open(BUILD_TAG_H, encoding="utf-8") as f:
        for line in f:
            m = re.search(r'#define\s+MOD_BUILD_TAG\s+"([^"]+)"', line)
            if m:
                return m.group(1)
    raise SystemExit("MOD_BUILD_TAG не найден в src/BuildTag.h")


def git_ls_files():
    # Отслеживаемые + новые не-игнорируемые файлы (например, свежий скрипт,
    # ещё не закоммиченный). Мусор сборки и builds/*.zip отсекает .gitignore.
    out = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT, universal_newlines=True)
    return [ln for ln in out.splitlines() if ln.strip()]


def current_milestone_block():
    """Первый блок текущего билда из секции «Текущий milestone»."""
    with open(CHANGELOG, encoding="utf-8") as f:
        s = f.read()
    idx = s.find("## Текущий milestone")
    if idx < 0:
        return "(CHANGELOG.md: секция «Текущий milestone» не найдена)"
    tail = s[idx + len("## Текущий milestone"):]
    first = tail.find("**Build")
    if first < 0:
        return tail.strip()
    second = tail.find("\n**Build", first + 1)
    block = tail[first:second if second != -1 else len(tail)]
    return block.strip()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    tag = read_build_tag()
    if not re.fullmatch(r"[0-9A-Za-z._-]+", tag):
        raise SystemExit("MOD_BUILD_TAG содержит недопустимые символы: " + tag)

    files = sorted(f for f in git_ls_files() if f not in SELF_EXCLUDE)
    zname = tag + ".zip"

    # 1. MANIFEST.txt (пишется до хеширования, чтобы его сумма была честной)
    now = datetime.now(MSK)
    total = len(files) + 2  # + MANIFEST.txt + MANIFEST.sha256
    with open(MANIFEST_TXT, "w", encoding="utf-8") as f:
        f.write("DDDA AI Overhaul — source package %s\n" % tag)
        f.write("=" * 72 + "\n")
        f.write("MOD_BUILD_TAG : %s\n" % tag)
        f.write("Дата сборки   : %s (Europe/Moscow)\n"
                % now.strftime("%Y-%m-%d %H:%M"))
        f.write("Файлов        : %d\n" % total)
        f.write("Архив         : builds/%s\n" % zname)
        f.write("\nСостав:\n%s\n" % current_milestone_block())

    # 2. MANIFEST.sha256 — по всем файлам пакета кроме себя самого
    entries = sorted(files + ["MANIFEST.txt"])
    with open(MANIFEST_SHA, "w", encoding="utf-8") as f:
        for rel in entries:
            f.write("%s  %s\n" % (sha256(os.path.join(ROOT, rel)), rel))

    # 3. zip
    os.makedirs(BUILDS_DIR, exist_ok=True)
    zip_path = os.path.join(BUILDS_DIR, zname)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for rel in files:
            z.write(os.path.join(ROOT, rel), rel)
        z.write(MANIFEST_TXT, "MANIFEST.txt")
        z.write(MANIFEST_SHA, "MANIFEST.sha256")

    size = os.path.getsize(zip_path)
    print("MOD_BUILD_TAG : %s" % tag)
    print("Архив         : builds/%s" % zname)
    print("Файлов        : %d" % total)
    print("Размер        : %.2f MiB" % (size / 1048576.0))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""grab_enemy_data.py — выдрать из распакованного дерева только data-файлы.

Зачем: после распаковки .arc (FluffyQuack ARCtool) в дереве лежат модели,
текстуры и звуки (десятки МБ). Для MonsterCard нужны только крохи:

    charparam/em/*.rst       HP / knockdown / flinch
    charparam/em/*_cmn.prp   статы / резисты / класс размера
    AI/Sensor/Enemy/*.sn2    зрение / ближняя зона / слух
    AI/SensorTarget/*.stg    сенсор-таргет (опционально)

Скрипт копирует ТОЛЬКО эти четыре типа, сохраняя внутреннюю структуру, в
resources/enemy_data/<архив>/... — папку, которую .gitignore не пускает в git.

Использование:
    python3 tools/grab_enemy_data.py <папка с распакованными архивами> [dest]

Первый компонент пути каждого файла считается именем архива (emXXXX). Если
файл лежит без обёртки архива, он уходит в _misc/ и печатается предупреждение.
"""

import os
import shutil
import sys

MASKS = (".rst", "_cmn.prp", ".sn2", ".stg")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: grab_enemy_data.py <распакованное дерево> [dest]")
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "resources", "enemy_data")

    kept, skipped = 0, 0
    for dirpath, _dirs, files in os.walk(src):
        for fn in files:
            if not fn.endswith(MASKS):
                continue
            rel = os.path.relpath(os.path.join(dirpath, fn), src)
            parts = rel.split(os.sep)
            arc = parts[0]
            if not arc.lower().startswith("em") or "." in arc:
                arc = "_misc"
                print("  [предупреждение] вне папки архива: %s" % rel)
            dest = os.path.join(dst, arc, *parts[1:])
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copy2(os.path.join(dirpath, fn), dest)
            kept += 1

    total = sum(len(fs) for _, _, fs in os.walk(src))
    print("взято: %d data-файлов (пропущено %d тяжёлых) -> %s" % (kept, total - kept, dst))


if __name__ == "__main__":
    main()

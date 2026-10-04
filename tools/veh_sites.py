#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Опознание площадок [VEH] по карте линковки (85.69).

ЗАЧЕМ. Лог говорит «DINPUT8.dll+0xA2BC7», а понять, КТО читает, по одному
смещению нельзя. Карта линковки `dinput8.map` генерируется автоматически рядом
с DLL (ключ /MAP стоит в обоих профилях проекта), и в ней у каждой функции есть
смещение. Инструмент складывает одно с другим.

ГЛАВНОЕ ПРАВИЛО: карта должна быть от ТОЙ ЖЕ сборки, что DLL и лог. Карта от
другой сборки даёт правдоподобную чушь: 01.10.2026 так и было — к логу свежей
сборки приложили карту от 26.09. Инструмент печатает дату карты, чтобы это
было видно сразу.

Запуск:
  python3 tools/veh_sites.py --map dinput8.map log.txt          # площадки из лога
  python3 tools/veh_sites.py --map dinput8.map 0xA2BC7 0x456A9  # смещения вручную

Смещения — RVA, то есть ровно те числа, что стоят в логе после «DINPUT8.dll+».
"""
import bisect
import re
import sys

SITE_RE = re.compile(
    r"\[VEH\] NEW SITE (\w+)\((\d+)\) at 0x([0-9A-Fa-f]+) in (\S+)"
    r"(?: \| data 0x([0-9A-Fa-f]+) ([^\s|]+))?"
    r"(?:\s*\|\s*(.*))?$")
SUMMARY_RE = re.compile(r"faults=(\d+) dumps=(\d+) sites=(\d+)")
TS_RE = re.compile(r"Timestamp is\s+(\S+)\s*\(([^)]*)\)")


def load_map(path):
    """(предпочтительная база, [(rva, имя, объект)], строка с датой карты)."""
    pref = 0x10000000
    ts = ""
    syms = []
    row = re.compile(r"^\s+([0-9A-Fa-f]{4}):[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{8})\b")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TS_RE.search(line)
            if m and not ts:
                ts = "%s (%s)" % (m.group(1), m.group(2).strip())
            if "Preferred load address is" in line:
                pref = int(line.split()[-1], 16)
            m = row.match(line)
            if not m:
                continue
            sec, name, rvabase = m.group(1), m.group(2), m.group(3)
            if sec == "0000":
                continue
            parts = line.split()
            obj = parts[-1] if len(parts) >= 4 else "?"
            syms.append((int(rvabase, 16) - pref, name, obj))
    syms.sort()
    return pref, syms, ts


def resolve(syms, rva):
    if not syms:
        return None
    keys = [s[0] for s in syms]
    i = bisect.bisect_right(keys, rva) - 1
    if i < 0:
        return None
    base, name, obj = syms[i]
    return name, rva - base, obj


def rva_from_module(mod):
    m = re.search(r"\+0x([0-9A-Fa-f]+)$", mod)
    return int(m.group(1), 16) if m else None


def main(argv):
    if "--map" not in argv:
        print(__doc__.strip())
        return 2
    mp = argv[argv.index("--map") + 1]
    rest = [a for a in argv[1:] if a not in ("--map", mp)]
    pref, syms, ts = load_map(mp)
    print("карта: %s | функций-ориентиров: %d%s"
          % (mp, len(syms), (" | Timestamp " + ts) if ts else ""))
    if ts and "Timestamp" in ts:
        print("  (сверь дату с временем сборки DLL/лога — иначе карта от другой сборки)")

    if not rest:
        print("нечего опознавать: дай файл лога или смещения")
        return 2

    # Режим 1: файл(ы) лога.
    if all("0x" not in a for a in rest):
        sites = []
        summary = None
        for path in rest:
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    m = SUMMARY_RE.search(line)
                    if m and not summary:
                        summary = m.groups()
                    m = SITE_RE.search(line)
                    if m:
                        sites.append(m)
        if summary:
            print("итог сессии: faults=%s dumps=%s sites=%s" % summary)
        if not sites:
            print("в логе нет строк [VEH] NEW SITE — опознавать нечего")
            return 0
        for m in sites:
            access, _, pc, mod, data, dclass, tail = m.groups()
            rva = rva_from_module(mod)
            name = resolve(syms, rva) if rva is not None else None
            where = ("%s (+0x%X) · %s" % (name[0], name[1], name[2])) if name \
                else "— (смещение раньше первого ориентира карты?)"
            line = "  0x%X  %s  %s  [%s" % (rva, mod.split("+")[0], where, access)
            if data is not None:
                line += ", data 0x%s %s" % (data, dclass)
            if tail:
                line += ", " + tail.strip()
            print(line + "]")
        return 0

    # Режим 2: смещения переданы явно.
    for a in rest:
        v = a.lower().replace("+", "")
        if v.startswith("0x"):
            v = v[2:]
        if v.endswith("h"):
            v = v[:-1]
        try:
            rva = int(v, 16)
        except ValueError:
            print("  не понял смещение %r" % a)
            continue
        name = resolve(syms, rva)
        if name:
            print("  0x%X  →  %s (+0x%X) · %s" % (rva, name[0], name[1], name[2]))
        else:
            print("  0x%X  →  — (раньше первого ориентира карты)" % rva)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

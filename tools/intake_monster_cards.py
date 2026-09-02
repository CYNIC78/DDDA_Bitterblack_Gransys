#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""intake_monster_cards.py — приёмка data-файлов врага -> MonsterCard (CATALOG).

Замысел: карточка статов монстра как read-only каталог (НЕ SpeciesCard —
тот остаётся шлюзом записи и наполняется только живым A/B).

Читает (из resources/enemy_data/emXXXX/ или resources/extracted_assets/):
    charparam/em/*.rst        HP / knockdown / flinch (rec0 = normal)
    charparam/em/*_cmn.prp    atk/def/matk/mdef/weight/size_class + резисты
    AI/Sensor/Enemy/*.sn2     зрение / ближняя зона / слух
Ключ карточки — имя *.rst (emXXXX) -> DTI uEmXXXX (эрата 84.66: номер
архива = номер DTI, ARC_TO_UEM пуст). Дубли (один .rst в нескольких
архивах) сворачиваются по uEm, первое вхождение выигрывает.

Выводит:
    - таблицу в stdout (аудит);
    - src/MonsterCards.Generated.h (собственно каталог).
"""

import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rst_dump
import xfs_dump

# ЭРАТА 84.66: номер архива = номер DTI для всех семейств (см. ARC_MAP.txt).
ARC_TO_UEM = {}

PRP_WANT = {
    "攻撃力": "atk", "防御力": "def", "魔法攻撃力": "matk", "魔法防御力": "mdef",
    "体重": "weight", "大きさ": "sizeClass",
    "耐炎": "resFire", "耐氷": "resIce", "耐雷": "resThunder",
    "耐聖": "resHoly", "耐魔": "resDark",
    "耐斬": "resSlash", "耐打": "resStrike",
}

OUT_HEADER = os.path.join("src", "MonsterCards.Generated.h")

FIELDS = [
    ("uEm", "s"),
    ("hp", "f"), ("kd", "f"), ("flinch", "f"),
    ("atk", "f"), ("def", "f"), ("matk", "f"), ("mdef", "f"),
    ("weight", "f"), ("sizeClass", "u"),
    ("resFire", "f"), ("resIce", "f"), ("resThunder", "f"),
    ("resHoly", "f"), ("resDark", "f"), ("resSlash", "f"), ("resStrike", "f"),
    ("sightRadius", "f"), ("sightAngle", "f"), ("nearRadius", "f"), ("hearRadius", "f"),
]

DEFAULTS = {  # отсутствие поля ≠ 0: нейтральное значение честнее
    "hp": 0.0, "kd": 0.0, "flinch": 0.0,
    "atk": 0.0, "def": 0.0, "matk": 0.0, "mdef": 0.0,
    "weight": 0.0, "sizeClass": 0,
    "resFire": 1.0, "resIce": 1.0, "resThunder": 1.0, "resHoly": 1.0,
    "resDark": 1.0, "resSlash": 1.0, "resStrike": 1.0,
    "sightRadius": 0.0, "sightAngle": 0.0, "nearRadius": 0.0, "hearRadius": 0.0,
}


def map_to_uem(name):
    for arc, uem in ARC_TO_UEM.items():
        if name.startswith(arc):
            return uem
    return "uEm" + name[2:]  # passthrough


def parse_sn2(path):
    """-> {sightRadius, sightAngle, nearRadius, hearRadius} или None."""
    d = open(path, "rb").read()
    if d[:4] != b"SNR2":
        return None
    n = struct.unpack_from("<H", d, 6)[0]
    out = {}
    for i in range(n):
        o = 0x10 + i * 0x50
        if o + 0x50 > len(d):
            break
        t = struct.unpack_from("<I", d, o)[0]
        r1 = struct.unpack_from("<f", d, o + 0x18)[0]
        ang = struct.unpack_from("<f", d, o + 0x30)[0]
        if t == 1:      # зрение, конус
            out["sightRadius"] = r1
            out["sightAngle"] = ang
        elif t == 2:    # ближняя круговая (первая запись)
            out.setdefault("nearRadius", r1)
        elif t == 3:    # слух
            out["hearRadius"] = r1
    return out


def collect(root):
    """Проходит подкаталоги, собирает карточки по *.rst (дедуп по uEm)."""
    cards = []
    seen = {}
    for rst in sorted(glob.glob(os.path.join(root, "**", "charparam", "em", "*.rst"),
                               recursive=True)):
        base = os.path.basename(rst)[:-4]      # em0100
        uem = map_to_uem(base)
        if uem in seen:
            print("  [дубль] %s уже взят из %s — пропуск %s" % (uem, seen[uem], rst))
            continue
        card = {"arc": base, "uEm": uem, "source": "FILE"}
        try:
            r = rst_dump.parse(rst)["records"][0]
            card.update(hp=r["maxHP"], kd=r["knockdownDurability"],
                        flinch=r["flinchDurability"])
        except Exception as e:
            print("  [сбой] %s: %s" % (rst, e))
            continue

        prp = os.path.join(os.path.dirname(rst), base + "_cmn.prp")
        if os.path.exists(prp):
            inst = xfs_dump.read_values(prp)
            for it in inst["values"]:
                if it["name"] in PRP_WANT:
                    card[PRP_WANT[it["name"]]] = it["value"]
        else:
            print("  [пропуск] нет %s" % prp)

        sn2s = sorted(glob.glob(os.path.join(os.path.dirname(rst),
                          "..", "..", "AI", "Sensor", "Enemy", "*.sn2")))
        own = [s for s in sn2s if os.path.basename(s).startswith(base)]
        sn2s = own or sn2s
        if sn2s:
            s = parse_sn2(sn2s[0])
            if s:
                card.update(s)
        cards.append(card)
        seen[uem] = rst
    return cards


def fmt_f(v):
    f = float(v)
    if f != f or f in (float("inf"), float("-inf")):
        return "0.0"
    s = "%.6g" % f
    if "e" in s.lower():
        return s                 # 1.47e+06
    if "." not in s:
        s += ".0"                # 1000 -> 1000.0
    return s


def emit_header(cards):
    with open(OUT_HEADER, "w", encoding="utf-8") as o:
        o.write("// MonsterCards.Generated.h — AUTOGENERATED from resources/enemy_data\n")
        o.write("// Не редактировать вручную! Перегенерить: python3 tools/intake_monster_cards.py\n")
        o.write("//\n")
        o.write("// CATALOG, read-only. НЕ SpeciesCard: запись в память по этим числам\n")
        o.write("// запрещена без живого A/B (SOURCE_OF_TRUTH.md). Значения извлечены из\n")
        o.write("// *.rst / *_cmn.prp / *.sn2 (source=FILE), не из рантайма.\n")
        o.write("#pragma once\n#include <stdint.h>\n#include <string.h>\n\n")
        o.write("struct MonsterCard {\n")
        o.write("    const char* uEm;   // DTI-имя (uEm0100)\n")
        o.write("    float hp, kd, flinch;\n")
        o.write("    float atk, def, matk, mdef, weight;\n")
        o.write("    uint32_t sizeClass;\n")
        o.write("    float resFire, resIce, resThunder, resHoly, resDark;\n")
        o.write("    float resSlash, resStrike;\n")
        o.write("    float sightRadius, sightAngle, nearRadius, hearRadius;\n")
        o.write("};\n\n")
        o.write("static const MonsterCard kMonsterCards[] = {\n")
        for c in cards:
            vals = []
            for name, kind in FIELDS:
                if kind == "s":
                    v = c[name]
                    vals.append('"%s"' % v)
                elif kind == "u":
                    vals.append("%du" % int(c.get(name, DEFAULTS[name])))
                else:
                    vals.append(fmt_f(c.get(name, DEFAULTS[name])) + "f")
            o.write("    { %s },\n" % ", ".join(vals))
        o.write("};\n\n")
        o.write("#define MONSTER_CARD_COUNT "
                "(sizeof(kMonsterCards)/sizeof(kMonsterCards[0]))\n\n")
        o.write("inline const MonsterCard* FindMonsterCard(const char* uEm) {\n")
        o.write("    if (!uEm || !uEm[0]) return 0;\n")
        o.write("    for (size_t i = 0; i < MONSTER_CARD_COUNT; ++i)\n")
        o.write("        if (kMonsterCards[i].uEm && !strcmp(kMonsterCards[i].uEm, uEm))\n")
        o.write("            return &kMonsterCards[i];\n")
        o.write("    return 0;\n}\n")
    print("✅ Сгенерирован %s (%d карточек)" % (OUT_HEADER, len(cards)))


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else None
    if root is None:
        data_dir = os.path.join("resources", "enemy_data")
        root = data_dir if glob.glob(os.path.join(data_dir, "**", "*.rst"),
                                     recursive=True) else "resources/extracted_assets"
    cards = collect(root)
    if not cards:
        raise SystemExit("не найдено *.rst под %s" % root)

    hdr = ("uEm", "HP", "KD", "atk", "def", "matk", "mdef",
           "sizeCls", "resF", "resI", "resT", "resH", "resD", "resS", "resB",
           "sightR", "sightA", "nearR", "hearR")
    print("%-12s %8s %8s %7s %6s %7s %6s %7s %5s %5s %5s %5s %5s %5s %5s %7s %6s %6s %6s" % hdr)
    for c in cards:
        print("%-12s %8.0f %8.0f %7.0f %6.0f %7.0f %6.0f %7d %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %7.0f %6.0f %6.0f %6.0f" % (
            c["uEm"], c.get("hp", 0), c.get("kd", 0),
            c.get("atk", 0), c.get("def", 0), c.get("matk", 0), c.get("mdef", 0),
            int(c.get("sizeClass", 0)),
            c.get("resFire", 1), c.get("resIce", 1), c.get("resThunder", 1),
            c.get("resHoly", 1), c.get("resDark", 1),
            c.get("resSlash", 1), c.get("resStrike", 1),
            c.get("sightRadius", 0), c.get("sightAngle", 0),
            c.get("nearRadius", 0), c.get("hearRadius", 0)))
    print("\nкарточек: %d, source=FILE" % len(cards))
    emit_header(cards)


if __name__ == "__main__":
    main()

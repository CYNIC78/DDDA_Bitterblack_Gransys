#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""intake_monster_cards.py — приёмка data-файлов врага -> MonsterCard (CATALOG).

Замысел: карточка статов монстра как read-only каталог (НЕ SpeciesCard —
тот остаётся шлюзом записи и наполняется только живым A/B).

Читает (из resources/enemy_data/emXXXX/ или resources/extracted_assets/):
    charparam/em/*.rst        HP / knockdown / flinch (rec0 = normal)
    charparam/em/*_cmn.prp    atk/def/matk/mdef/weight/size_class + резисты
    AI/Sensor/Enemy/*.sn2     зрение / ближняя зона / слух
Ключ карточки — имя *.rst (emXXXX), маппится в DTI uEmXXXX через
ARC_TO_UEM (только РАСХОЖДЕНИЯ; совпадение = passthrough). Всё, что не
сопоставилось, печатается в аудит-лог — НЕ заполняется догадкой.

Имя архива ≠ DTI: Harpy архив em0600 -> uEm0700, Cyclops em5000 -> uEm2000
(SoT §8.5.2). Расхождения вносятся в ARC_TO_UEM по факту листинга.
"""

import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rst_dump
import xfs_dump

# ЭРАТА 84.66: номер архива = номер DTI для всех семейств (см. ARC_MAP.txt).
# Старый «сдвиг» (em0600->uEm0700 Harpy и т.п.) был ярлыком из ARC_MAP.txt и
# отменён. Поэтому ARC_TO_UEM пуст: emXXXX -> uEmXXXX по умолчанию.
ARC_TO_UEM = {}

PRP_WANT = {
    "攻撃力": "atk", "防御力": "def", "魔法攻撃力": "matk", "魔法防御力": "mdef",
    "体重": "weight", "大きさ": "sizeClass",
    "耐炎": "resFire", "耐氷": "resIce", "耐雷": "resThunder",
    "耐聖": "resHoly", "耐魔": "resDark",
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
    """Проходит подкаталоги, собирает карточки по *.rst."""
    cards = []
    for rst in sorted(glob.glob(os.path.join(root, "**", "charparam", "em", "*.rst"),
                               recursive=True)):
        base = os.path.basename(rst)[:-4]      # em0100
        uem = map_to_uem(base)
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
        # эндфикс сенсора: em0100 -> em0100A.sn2; чужой вид (em0101A) не берём
        own = [s for s in sn2s if os.path.basename(s).startswith(base)]
        sn2s = own or sn2s
        if sn2s:
            s = parse_sn2(sn2s[0])
            if s:
                card.update(s)
        cards.append(card)
    return cards


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
           "sizeCls", "resF", "resI", "resT", "resH", "resD",
           "sightR", "sightA", "nearR", "hearR")
    print("%-10s %8s %8s %7s %6s %7s %6s %7s %5s %5s %5s %5s %5s %7s %6s %6s %6s" % hdr)
    for c in cards:
        print("%-10s %8.0f %8.0f %7.0f %6.0f %7.0f %6.0f %7d %5.2f %5.2f %5.2f %5.2f %5.2f %7.0f %6.0f %6.0f %6.0f" % (
            c["uEm"], c.get("hp", 0), c.get("kd", 0),
            c.get("atk", 0), c.get("def", 0), c.get("matk", 0), c.get("mdef", 0),
            int(c.get("sizeClass", 0)),
            c.get("resFire", 1), c.get("resIce", 1), c.get("resThunder", 1),
            c.get("resHoly", 1), c.get("resDark", 1),
            c.get("sightRadius", 0), c.get("sightAngle", 0),
            c.get("nearRadius", 0), c.get("hearRadius", 0)))
    print("\nкарточек: %d, source=FILE" % len(cards))


if __name__ == "__main__":
    main()

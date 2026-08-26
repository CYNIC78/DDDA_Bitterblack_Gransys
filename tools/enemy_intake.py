#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Стол + лог: кто закрыт, кто подписывается без боя, кого не пишем в Tempo.

Не двигает SpeciesCard. Не пишет в BestiaryData.h — только отчёт.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ATLAS = ROOT / "src" / "TypeAtlas.Generated.h"
BESTIARY = ROOT / "src" / "BestiaryData.h"
FLUFFY = ROOT / "resources" / "fluffy_em.txt"
SPECIES = ROOT / "src" / "monsterai" / "SpeciesCard.h"

WRITE_KINDS = {"uEm0100", "uEm0101", "uEm0200"}

# Имена актов, которые однозначно называют семью (не шум Howl/Bite).
ACT_FAMILY = (
    ("cyclops", ("BlindAttack", "EyeDamage", "PickupWeapon", "PrisonHold", "OMThrow")),
    ("golem", ("HpDrain", "DmgWeak", "GroundPunch", "Prostrate", "Muzzle", "Laser")),
    ("drake/dragon", ("Habataki", "BackBreath", "BackFireBall", "CatchDamagePL")),
    ("evil-eye", ("Tentacle", "GazeLight", "Petrif")),
    ("griffin", ("Gakenobori",)),
    ("harpy", ("Habataki",)),  # noisy; used only with fluffy
)


def load_atlas():
    text = ATLAS.read_text(encoding="utf-8", errors="replace")
    rows = re.findall(
        r'\{ "([^"]+)", (0x[0-9A-F]+), (0x[0-9A-F]+), (\d+), (\d+) \}', text
    )
    bodies = {}
    acts = defaultdict(list)
    for n, _f, _v, s, t in rows:
        if re.fullmatch(r"uEm\d{4}", n):
            bodies[n] = {"size": int(s), "gid": int(t)}
        m = re.match(r"cEm(\d{4})", n)
        if m:
            acts["uEm" + m.group(1)].append(n)
    return bodies, acts


def load_fluffy():
    out = {}
    if not FLUFFY.exists():
        return out
    for line in FLUFFY.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or " - " not in line:
            continue
        k, v = line.split(" - ", 1)
        out[k.strip()] = v.strip()
    return out


def load_bestiary():
    text = BESTIARY.read_text(encoding="utf-8", errors="replace")
    by_uem = {}
    for bid, gid, _ms, name, fam, uem, _vt in re.findall(
        r'\{\s*(-?\d+),\s*0x([0-9A-Fa-f]+),\s*(-?\d+),\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*(0x[0-9A-F]+)\s*\}',
        text,
    ):
        by_uem.setdefault(uem, []).append(
            {"bid": int(bid), "gid": gid, "name": name, "family": fam}
        )
    return by_uem


def load_write_kinds():
    text = SPECIES.read_text(encoding="utf-8", errors="replace")
    found = set(re.findall(r'\{ "([^"]+)", \d+u,', text))
    return found or set(WRITE_KINDS)


def act_tags(names):
    blob = " ".join(names)
    hits = []
    for tag, keys in ACT_FAMILY:
        if any(k in blob for k in keys):
            hits.append(tag)
    return hits


def parse_log(path: Path):
    """Живые тела из MD: enemy / Tempo species / DUMP kind=."""
    live = {}
    rx = re.compile(
        r"(?:MD: enemy |Tempo: species |kind=)(uEm\d{4})(?:\S*)?(?:.*?act=(\S+))?"
    )
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        for m in re.finditer(r"uEm\d{4}", line):
            kind = m.group(0)
            rec = live.setdefault(kind, {"acts": set(), "lines": 0})
            rec["lines"] += 1
        am = re.search(r"act=(\S+)", line)
        km = re.search(r"uEm\d{4}", line)
        if am and km:
            live[km.group(0)]["acts"].add(am.group(1).rstrip(","))
    return live


def classify(uem, fluffy, bestiary, acts, write_kinds):
    num = uem[3:]
    fl = fluffy.get("em" + num, "")
    fl_known = bool(fl) and not fl.startswith("?")
    labels = bestiary.get(uem, [])
    tags = act_tags(acts.get(uem, []))
    write = uem in write_kinds

    # Закрытые вручную пары (live Devilfire + Fluffy + acts).
    closed = {
        "uEm0100": "Goblin",
        "uEm0200": "Wolf",
        "uEm5000": "Cyclops",
        "uEm5100": "Golem",
        "uEm5900": "Drake",
    }
    if uem in closed:
        return "CLOSED", closed[uem], fl, tags, write, labels

    if fl_known and tags:
        return "DESK", fl, fl, tags, write, labels
    if fl_known and not acts.get(uem):
        # нет своих актов — как Drake на 5800. Подпись Флаффи, live не обязателен.
        return "DESK-WEAK", fl, fl, tags, write, labels
    if not fl_known and tags:
        return "ACTS-ONLY", "", fl, tags, write, labels
    return "OPEN", "", fl, tags, write, labels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", type=Path, help="ddda_ai_overhaul*.log")
    ap.add_argument("--arcs", type=Path, help="dir /b nativePC\\rom\\enemy\\*.arc")
    args = ap.parse_args()

    bodies, acts = load_atlas()
    fluffy = load_fluffy()
    bestiary = load_bestiary()
    write_kinds = load_write_kinds()

    arc_set = set()
    if args.arcs and args.arcs.exists():
        for line in args.arcs.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.search(r"(em\d{4})", line, re.I)
            if m:
                arc_set.add(m.group(1).lower())

    live = parse_log(args.log) if args.log else {}

    print("status   uEm      fluffy                 live-act              write  our-label")
    print("-" * 100)
    kinds = sorted(set(bodies) | set(live))
    counts = defaultdict(int)
    for uem in kinds:
        st, guess, fl, tags, write, labels = classify(
            uem, fluffy, bestiary, acts, write_kinds
        )
        if live and uem not in live and st not in ("CLOSED",):
            continue
        counts[st] += 1
        ours = labels[0]["name"] if labels else "—"
        act = ",".join(sorted(live[uem]["acts"])[:2]) if uem in live else "-"
        print(
            f"{st:9s} {uem:8s} {(fl or '—')[:22]:22s} {act[:20]:20s} "
            f"{'YES' if write else 'no ':3s}  {ours}"
        )
        if uem in live and st not in ("CLOSED", "DESK", "DESK-WEAK"):
            print(f"           ^ NEW IN LOG — подпись не закрыта, Tempo не писать")

    print()
    print("counts:", dict(counts))
    print("write-path (SpeciesCard):", ", ".join(sorted(write_kinds)))
    if arc_set:
        missing = [a for a in sorted(arc_set) if ("uEm" + a[2:]) not in bodies]
        print("arcs listed:", len(arc_set), "no DTI body:", missing[:12])
    if args.log:
        print("live kinds:", ", ".join(sorted(live)))


if __name__ == "__main__":
    sys.exit(main() or 0)

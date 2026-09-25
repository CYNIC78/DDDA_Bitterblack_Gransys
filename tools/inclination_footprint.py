#!/usr/bin/env python3
"""inclination_footprint.py — what a pawn's inclination stack actually does.

Offline, no game. Takes nine inclination values, derives the engine's rank
per inclination (2 primary / 1 secondary / 0 tertiary / -1 out of stack),
then prints every cmc.prt personality rule whose {inclination, rank} check
fires and the resulting AddS32 per priority code.

This is the honest answer to "how do primary and secondary interact": they
do not mix by magic — each rule fires on a {inclination, rank} pair and the
firing rules simply add up.

Usage:
    python3 tools/inclination_footprint.py 750 400 500 700 750 350 350 400 250
    python3 tools/inclination_footprint.py --demo

Order (SOURCE_OF_TRUTH.md §3.3):
    0 Scather 1 Medicant 2 Mitigator 3 Challenger 4 Utilitarian
    5 Guardian 6 Nexus 7 Pioneer 8 Acquisitor
"""
import csv
import json
import os
import sys

ROWS_CSV = os.path.join("docs", "PLAYER_PAWN_WORK", "generated",
                        "cmc_priority_rows.csv")
SEM_CSV = os.path.join("docs", "PLAYER_PAWN_WORK", "generated",
                       "pawn_priority_semantics.csv")

NAMES = ["Scather", "Medicant", "Mitigator", "Challenger", "Utilitarian",
         "Guardian", "Nexus", "Pioneer", "Acquisitor"]
INCL_GUARDIAN = 5
INCL_NEXUS = 6


def ranks_of(values):
    """2 = primary, 1 = secondary, 0 = tertiary, -1 = out of stack.

    Tie-break: lower inclination id wins. The engine's own tie-break is
    unknown; this one only has to be deterministic (docs/PAWN_ROLE_STACK.md
    §3, "where the rank comes from").
    """
    ranked = [-1] * 9
    order = sorted(range(9), key=lambda i: (-values[i], i))
    for place, idx in enumerate(order[:3]):
        ranked[idx] = 2 - place
    return ranked


def load_rows():
    rows = []
    with open(ROWS_CSV, newline="", encoding="utf-8-sig") as fh:
        for row in csv.DictReader(fh):
            rules = json.loads(row["personalities"]) if row["personalities"] else []
            rows.append((row["slot"], int(row["code"]), rules))
    return rows


def goal_names():
    out = {}
    if not os.path.exists(SEM_CSV):
        return out
    with open(SEM_CSV, newline="", encoding="utf-8-sig") as fh:
        for row in csv.DictReader(fh):
            out[int(row["code"])] = row["displayName"]
    return out


def footprint(values):
    ranked = ranks_of(values)
    goals = goal_names()
    fired = []
    for slot, code, rules in load_rows():
        for rule in rules:
            for check in rule.get("checks", []):
                pid = check.get("personality")
                state = check.get("state")
                if 0 <= pid < 9 and ranked[pid] == state:
                    fired.append((code, goals.get(code, "?"), NAMES[pid],
                                  state, rule.get("addS32", 0), slot))
    return ranked, fired


def persona(ranked, values):
    """Which doctrine this pawn belongs to, and whether it clears the bar."""
    rg, vg = ranked[INCL_GUARDIAN], values[INCL_GUARDIAN]
    rn, vn = ranked[INCL_NEXUS], values[INCL_NEXUS]
    if rg > rn or (rg == rn and vg >= vn):
        name, rank, val = "Guardian", rg, vg
    else:
        name, rank, val = "Nexus", rn, vn
    if rank < 1 or val < 350:
        return "NONE (%s rank %d, value %.0f - below threshold)" % (name, rank, val)
    return "%s (rank %d, value %.0f)" % (name, rank, val)


def report(title, values):
    ranked, fired = footprint(values)
    who = persona(ranked, values)
    print("=" * 72)
    print(title)
    print("-" * 72)
    print("  values : " + "  ".join("%s=%g" % (NAMES[i][:4], values[i])
                                    for i in range(9)))
    print("  stack  : primary=%s  secondary=%s  tertiary=%s"
          % (NAMES[ranked.index(2)] if 2 in ranked else "-",
             NAMES[ranked.index(1)] if 1 in ranked else "-",
             NAMES[ranked.index(0)] if 0 in ranked else "-"))
    print("  persona: %s" % who)
    print("-" * 72)
    if not fired:
        print("  no cmc.prt personality rule fires for this stack")
    else:
        print("  code  goal                     inclination  rank  AddS32  slot")
        for code, goal, name, state, add, slot in sorted(fired, key=lambda r: r[0]):
            print("  %4d  %-24s  %-11s  %4d  %+6d  %s"
                  % (code, goal[:24], name, state, add, slot))
    print()


def main():
    args = sys.argv[1:]
    if args and args[0] == "--demo":
        report("MainPawn / Strider (ползунки из ini репозитория)",
               [750, 400, 500, 700, 750, 350, 350, 400, 250])
        report("Hired Fighter, Guardian первичная (пример)",
               [300, 200, 150, 400, 250, 912, 120, 180, 100])
        report("Hired Fighter, Guardian вторичная при Scather-первичной",
               [900, 200, 150, 300, 250, 850, 120, 180, 100])
        report("Hired Mage, Nexus первичная (пример)",
               [180, 500, 200, 150, 600, 240, 880, 120, 300])
        return
    if len(args) != 9:
        print(__doc__)
        return 2
    report("Party pawn", [float(x) for x in args])
    return 0


if __name__ == "__main__":
    sys.exit(main())

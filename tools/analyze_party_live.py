#!/usr/bin/env python3
"""Condense a Build 39 party live CSV into action/stat transitions."""

from __future__ import annotations

import argparse
import csv
import struct
from collections import Counter
from pathlib import Path


def raw_u32(text: str) -> int:
    return int(text, 0)


def raw_f32(text: str) -> float:
    return struct.unpack("<f", struct.pack("<I", raw_u32(text)))[0]


def load_rows(path: Path) -> list[dict[str, str]]:
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    header = next(i for i, line in enumerate(lines) if line.startswith("ms,"))
    return [row for row in csv.DictReader(lines[header:]) if row.get("ms", "").isdigit()]


def action_report(rows: list[dict[str, str]], who: str) -> None:
    print(f"\n{who} action transitions")
    previous = None
    for row in rows:
        action = row[f"{who}Action"]
        if action == previous:
            continue
        fields = [raw_u32(row[f"{who}_{off}"]) for off in ("2DD4", "4AE8", "32D8", "1C94", "4B14")]
        print(f"{int(row['ms']) / 1000:8.3f}  {action:<34} {fields}")
        previous = action
    print("samples:", dict(Counter(row[f"{who}Action"] for row in rows)))


def stat_report(rows: list[dict[str, str]], who: str) -> None:
    prefix = f"{who}Rec_"
    hp = [raw_f32(row[prefix + "96C"]) for row in rows]
    hp_max = [raw_f32(row[prefix + "970"]) for row in rows]
    hp_aux = [raw_f32(row[prefix + "974"]) for row in rows]
    stamina = [raw_f32(row[prefix + "978"]) for row in rows]
    print(f"\n{who} record")
    print(f"current HP: {hp[0]:.4f} -> {hp[-1]:.4f}; range {min(hp):.4f}..{max(hp):.4f}")
    print(f"max HP:     {hp_max[0]:.4f}")
    print(f"HP aux:     {hp_aux[0]:.4f} -> {hp_aux[-1]:.4f}")
    print(f"stamina:    {stamina[0]:.4f} -> {stamina[-1]:.4f}; range {min(stamina):.4f}..{max(stamina):.4f}")

    previous = hp[0]
    for row, value in zip(rows[1:], hp[1:]):
        if value != previous:
            print(f"HP change at {int(row['ms']) / 1000:.3f}s: {previous:.4f} -> {value:.4f}")
            previous = value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()
    rows = load_rows(args.csv)
    if not rows:
        raise SystemExit("no data rows")
    print(f"rows={len(rows)} duration={int(rows[-1]['ms']) / 1000:.3f}s")
    action_report(rows, "arisen")
    action_report(rows, "pawn")
    stat_report(rows, "player")
    stat_report(rows, "pawn")


if __name__ == "__main__":
    main()

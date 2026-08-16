#!/usr/bin/env python3
"""Extract code -> GOAP links from a Build 52+ pawn AI bridge snapshot."""

from __future__ import annotations

import argparse
import csv
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_ai_catalog.json"
DEFAULT_JSON = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_planner_semantics.json"
DEFAULT_CSV = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_planner_semantics.csv"


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def resource_path(resource: dict) -> str | None:
    data = bytes.fromhex(resource["hex"])
    raw = data[0x08:0x48].split(b"\0", 1)[0]
    try:
        value = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    return value if value.startswith(("AI\\", "tu2\\")) else None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--json", type=Path, default=DEFAULT_JSON)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    args = parser.parse_args()

    snapshot = json.loads(args.snapshot.read_text(encoding="utf-8"))
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))

    interface_by_ptr: dict[int, dict] = {}
    runtime_paths = []
    for resource in snapshot.get("goapResources", []):
        path = resource_path(resource)
        if path:
            runtime_paths.append(path)
        for target in resource.get("targets", []):
            for child in target.get("children", []):
                if child.get("name") != "rAIGoalPlanning::ActionInterfaceParam":
                    continue
                data = bytes.fromhex(child.get("hex", ""))
                if len(data) < 12:
                    continue
                interface_by_ptr[int(child["ptr"], 16)] = {
                    "resourcePath": path,
                    "resourceName": path.split("\\")[-1] if path else None,
                    "interfaceId": u32(data, 0x04),
                }

    source_rows: dict[int, list[dict]] = {}
    for row in catalog["priorityThink"]["rows"]:
        source_rows.setdefault(row["code"], []).append({
            "slot": row["slot"],
            "index": row["index"],
            "identity": [
                row["sensor"], row["code"], row["category"],
                row["objectId"], row["extra"],
            ],
        })

    entries = []
    for slot in snapshot.get("plannerSlots", []):
        links = []
        seen = set()
        for node in slot.get("nodeLinks", []):
            match = interface_by_ptr.get(int(node["linkPtr"], 16))
            if not match:
                continue
            key = (match["resourceName"], match["interfaceId"])
            if key in seen:
                continue
            seen.add(key)
            links.append({**match, "linkPtr": node["linkPtr"]})
        resources = sorted({link["resourceName"] for link in links if link["resourceName"]})
        status = "unmatched"
        if len(resources) == 1:
            status = "direct_goap_link"
        elif len(resources) > 1:
            status = "mixed_goap_links"
        entries.append({
            "code": slot["code"],
            "planHash": slot["hash"],
            "status": status,
            "resources": resources,
            "links": links,
            "usedByPriority": slot["code"] in source_rows,
            "sourceRows": source_rows.get(slot["code"], []),
        })

    payload = {
        "schemaVersion": 1,
        "buildEvidence": snapshot.get("build"),
        "plannerSlotCount": len(entries),
        "runtimeGoapResourceCount": len(set(runtime_paths)),
        "directSlotCount": sum(entry["status"] == "direct_goap_link" for entry in entries),
        "mixedSlotCount": sum(entry["status"] == "mixed_goap_links" for entry in entries),
        "unmatchedSlotCount": sum(entry["status"] == "unmatched" for entry in entries),
        "usedPriorityCodeCount": len(source_rows),
        "mappedUsedPriorityCodeCount": sum(
            entry["usedByPriority"] and entry["status"] != "unmatched"
            for entry in entries
        ),
        "entries": entries,
    }
    args.json.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    with args.csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow([
            "code", "usedByPriority", "status", "resources",
            "interfaceIds", "sourceSlots", "planHash",
        ])
        for entry in entries:
            writer.writerow([
                entry["code"], int(entry["usedByPriority"]), entry["status"],
                "|".join(entry["resources"]),
                "|".join(str(link["interfaceId"]) for link in entry["links"]),
                "|".join(row["slot"] for row in entry["sourceRows"]),
                entry["planHash"],
            ])

    print(
        f"wrote {args.json}: {payload['mappedUsedPriorityCodeCount']}/"
        f"{payload['usedPriorityCodeCount']} used priority codes mapped"
    )
    print(f"wrote {args.csv}")


if __name__ == "__main__":
    main()

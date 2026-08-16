#!/usr/bin/env python3
"""Build a readable catalogue from extracted DDDA pawn AI XFS resources."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from typing import Any

from xfs_tree_dump import Decoder


def many(value: Any) -> list[Any]:
    if value is None or value == []:
        return []
    return value if isinstance(value, list) else [value]


def wrapper_array(value: Any) -> list[dict[str, Any]]:
    if not isinstance(value, dict):
        return []
    return [v for v in many(value.get("mpArray", [])) if isinstance(v, dict)]


def flatten_prio(path: Path) -> list[dict[str, Any]]:
    root = Decoder(path).decode()["roots"][0]
    rows: list[dict[str, Any]] = []
    for slot, wrapper in root.items():
        if slot in ("mQuality", "タイプ"):
            continue
        for index, entry in enumerate(wrapper_array(wrapper)):
            personalities = []
            for change in wrapper_array(entry.get("性格")):
                checks = [
                    {
                        "personality": check.get("チェックする性格"),
                        "state": check.get("チェックする状態"),
                    }
                    for check in wrapper_array(change.get("性格リスト"))
                ]
                personalities.append(
                    {
                        "addS32": change.get("加算値S32"),
                        "addF32": change.get("加算値F32"),
                        "breakAfterApply": change.get("適用したら抜ける"),
                        "checks": checks,
                    }
                )
            orders = [
                {"value": order.get("設定値"), "type": order.get("命令タイプ")}
                for order in wrapper_array(entry.get("命令"))
            ]
            rows.append(
                {
                    "slot": slot,
                    "index": index,
                    "sensor": entry.get("センサー"),
                    "code": entry.get("コード"),
                    "category": entry.get("カテゴリ"),
                    "objectId": entry.get("OBJECT_ID"),
                    "extra": entry.get("付加情報"),
                    "personalities": personalities,
                    "orders": orders,
                }
            )
    return rows


def action_parameter_files(root: Path) -> list[Path]:
    return sorted((root / "AI" / "AIPlayerActionParameter").glob("*.AIPlActParam"))


def decode_action_parameters(root: Path) -> dict[str, list[dict[str, Any]]]:
    result = {}
    for path in action_parameter_files(root):
        decoded = Decoder(path).decode()["roots"][0]
        result[path.stem] = [v for v in many(decoded.get("mArray")) if isinstance(v, dict)]
    return result


def decode_goap_file(path: Path) -> dict[str, Any]:
    decoder = Decoder(path)
    decoded = decoder.decode()
    root = decoded["roots"][0]
    schema = decoder.schema["classes"]

    premises = [row.get("条件") for row in wrapper_array(root.get("PremiseConditionList"))]
    goals = []
    for goal in wrapper_array(root.get("GoalList")):
        goal_premises = [
            row.get("PremiseCondition")
            for row in wrapper_array(goal.get("PremiseConditionList"))
        ]
        goals.append({
            "name": goal.get("GoalName"),
            "premiseCount": goal.get("PremiseConditionNum"),
            "premises": goal_premises,
        })

    interfaces = []
    for row in wrapper_array(root.get("ActionInterfaceList")):
        interface = row.get("Interface") if isinstance(row.get("Interface"), dict) else {}
        class_index = interface.get("$class")
        class_info = schema[class_index] if isinstance(class_index, int) and class_index < len(schema) else {}
        interfaces.append({
            "premise": row.get("PremiseCondition"),
            "effect": row.get("Effect"),
            "interfaceId": row.get("InterfaceID"),
            "elementAttr": row.get("ElementAttr"),
            "attackAttr": row.get("AtkAttr"),
            "useAttr": row.get("UseAttr"),
            "classIndex": class_index,
            "classHash": class_info.get("hash"),
            "classSize": class_info.get("sizeof"),
        })

    return {
        "file": path.name,
        "premiseConditions": premises,
        "goals": goals,
        "interfaces": interfaces,
        "classCount": decoded["classCount"],
        "instanceCount": decoded["instanceCount"],
    }


def decode_goap(root: Path) -> dict[str, dict[str, Any]]:
    result = {}
    for path in sorted((root / "AI" / "Goap" / "Cmc").glob("*.gop")):
        result[path.stem] = decode_goap_file(path)
    return result


def write_prio_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fields = ["slot", "index", "sensor", "code", "category", "objectId", "extra", "personalities", "orders"]
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fields)
        writer.writeheader()
        for row in rows:
            out = dict(row)
            out["personalities"] = json.dumps(out["personalities"], ensure_ascii=False, separators=(",", ":"))
            out["orders"] = json.dumps(out["orders"], ensure_ascii=False, separators=(",", ":"))
            writer.writerow(out)


def write_goap_csv(path: Path, goap: dict[str, dict[str, Any]]) -> None:
    fields = ["file", "premises", "goals", "interfaceId", "premise", "effect", "elementAttr", "attackAttr", "useAttr", "classHash", "classSize"]
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fields)
        writer.writeheader()
        for name, item in goap.items():
            interfaces = item["interfaces"] or [{}]
            for interface in interfaces:
                writer.writerow({
                    "file": name,
                    "premises": json.dumps(item["premiseConditions"], separators=(",", ":")),
                    "goals": json.dumps(item["goals"], ensure_ascii=False, separators=(",", ":")),
                    **{key: interface.get(key) for key in fields if key not in ("file", "premises", "goals")},
                })


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="resources/extracted_assets/pawnAI")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    prio_path = args.root / "AI" / "PrioThink" / "cmc.prt"
    rows = flatten_prio(prio_path)
    params = decode_action_parameters(args.root)
    goap = decode_goap(args.root)

    result = {
        "priorityThink": {
            "file": str(prio_path),
            "entries": len(rows),
            "slotCounts": dict(Counter(row["slot"].split(" - ", 1)[0] for row in rows)),
            "sensorCounts": dict(Counter(row["sensor"] for row in rows)),
            "categoryCounts": dict(Counter(row["category"] for row in rows)),
            "rows": rows,
        },
        "actionParameters": {
            name: {"entries": len(entries), "rows": entries}
            for name, entries in params.items()
        },
        "goap": goap,
    }

    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "pawn_ai_catalog.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        write_prio_csv(args.out / "cmc_priority_rows.csv", rows)
        write_goap_csv(args.out / "cmc_goap_interfaces.csv", goap)
        for name, entries in params.items():
            if not entries:
                continue
            fields = [key for key in entries[0] if not key.startswith("$")]
            with (args.out / f"{name}.csv").open("w", encoding="utf-8-sig", newline="") as stream:
                writer = csv.DictWriter(stream, ["index"] + fields)
                writer.writeheader()
                for index, entry in enumerate(entries):
                    writer.writerow({"index": index, **{field: entry.get(field) for field in fields}})

    print(json.dumps({
        "priorityEntries": len(rows),
        "slotGroups": result["priorityThink"]["slotCounts"],
        "sensors": result["priorityThink"]["sensorCounts"],
        "categories": result["priorityThink"]["categoryCounts"],
        "actionParameterEntries": {name: len(entries) for name, entries in params.items()},
        "goapFiles": len(goap),
        "goapInterfaces": sum(len(item["interfaces"]) for item in goap.values()),
        "goapGoals": sum(len(item["goals"]) for item in goap.values()),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Condense pawn-AI bridge snapshots into live priority/planner evidence."""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


NO_PRIORITY = 0xFFFFFFFF
PRIORITY_SLOT_NAMES = [
    f"{group} - {index:02d}"
    for group in ("QUEST", "PL_Party", "Situ_Personal", "Enemy", "Wait_Follow", "Etc")
    for index in range(8)
]
PRIORITY_ARRAY_BASE = 0x38
PRIORITY_ARRAY_SIZE = 0x14


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def object_map(snapshot: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    return {(obj["name"], obj["ptr"]): obj for obj in snapshot["objects"]}


def one_by_name(snapshot: dict[str, Any], name: str) -> dict[str, Any] | None:
    values = [obj for obj in snapshot["objects"] if obj["name"] == name]
    return values[0] if len(values) == 1 else None


def inclination_rows(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    data = bytes.fromhex(snapshot["cmcInfoHex"])
    rows = []
    for personality_id in range(9):
        offset = 0x14B8 + personality_id * 0x0C
        rows.append({
            "id": u32(data, offset + 4),
            "state": u32(data, offset),
            "value": round(f32(data, offset + 8), 6),
            "offset": f"0x{offset:04X}",
        })
    return rows


def priority_tuple(obj: dict[str, Any]) -> tuple[int, int, int, int, int] | None:
    data = bytes.fromhex(obj["headHex"])
    if len(data) < 0x18:
        return None
    return tuple(u32(data, offset) for offset in (0x04, 0x08, 0x0C, 0x10, 0x14))


def priority_objects(snapshot: dict[str, Any]) -> dict[int, dict[str, Any]]:
    result = {}
    for obj in snapshot["objects"]:
        if obj["name"] == "rAIPriorityThink::cPrioParam":
            result[int(obj["ptr"], 16)] = obj
    return result


def priority_object(snapshot: dict[str, Any], code: int | None) -> dict[str, Any] | None:
    if code is None:
        return None
    for obj in priority_objects(snapshot).values():
        row = priority_tuple(obj)
        if row is not None and row[1] == code:
            return obj
    return None


def priority_catalog_index(
    catalog: dict[str, Any],
) -> dict[tuple[int, int, int, int, int], list[dict[str, Any]]]:
    result: dict[tuple[int, int, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    for row in catalog["priorityThink"]["rows"]:
        key = (
            row["sensor"], row["code"], row["category"],
            row["objectId"], row["extra"],
        )
        result[key].append(row)
    return result


def priority_bucket_report(
    snapshot: dict[str, Any], catalog: dict[str, Any]
) -> dict[str, Any] | None:
    """Decode the 48 live cArray descriptors in cAIPriorityThink.

    Build 42 called each data pointer a 0x90-byte node. The root layout proves
    that the pointer is cArray.mpArray: only `count` leading pointers are live;
    the rest of the 0x90 capture is allocator/stale storage and must be ignored.
    """
    roots = [
        obj for obj in snapshot["objects"]
        if obj["name"] == "cAIPriorityThink"
    ]
    # The C++ writer captures buckets from the first root in census order.
    # Several pawn roots may coexist after reload; preserve that same order
    # instead of discarding otherwise coherent bucket payloads.
    root = roots[0] if roots else None
    exact_buckets = snapshot.get("priorityBuckets")
    nodes = snapshot.get("priorityNodes")
    if root is None or (not exact_buckets and not nodes):
        return None

    root_data = bytes.fromhex(root["headHex"])
    prio_by_ptr = priority_objects(snapshot)
    catalog_by_tuple = priority_catalog_index(catalog)
    slot_index = {name: index for index, name in enumerate(PRIORITY_SLOT_NAMES)}

    buckets = []
    relocations = []
    valid_pointers = 0
    unknown_pointers = 0

    def decode_entry(prio_ptr: int, index: int, position: int) -> dict[str, Any]:
        nonlocal valid_pointers, unknown_pointers
        prio_obj = prio_by_ptr.get(prio_ptr)
        live_row = priority_tuple(prio_obj) if prio_obj else None
        if live_row is None:
            unknown_pointers += 1
            return {
                "position": position,
                "ptr": f"0x{prio_ptr:08X}",
                "known": False,
            }

        valid_pointers += 1
        sensor, code, category, object_id, extra = live_row
        definitions = catalog_by_tuple.get(live_row, [])
        source_slots = [row["slot"] for row in definitions]
        source_indices = [slot_index[name] for name in source_slots if name in slot_index]
        source_index = source_indices[0] if len(source_indices) == 1 else None
        delta = index - source_index if source_index is not None else None
        entry = {
            "position": position,
            "ptr": f"0x{prio_ptr:08X}",
            "known": True,
            "sensor": sensor,
            "code": code,
            "category": category,
            "objectId": object_id,
            "extra": extra,
            "sourceSlots": source_slots,
            "runtimeSlotDelta": delta,
        }
        if delta not in (None, 0):
            relocations.append({
                "ptr": entry["ptr"],
                "sensor": sensor,
                "code": code,
                "extra": extra,
                "sourceSlot": source_slots[0],
                "sourceSlotIndex": source_index,
                "runtimeSlot": PRIORITY_SLOT_NAMES[index],
                "runtimeSlotIndex": index,
                "runtimeSlotDelta": delta,
            })
        return entry

    if exact_buckets:
        coherent_count = 0
        for captured in exact_buckets:
            index = captured["slotIndex"]
            if not 0 <= index < len(PRIORITY_SLOT_NAMES):
                continue
            if captured.get("coherent"):
                coherent_count += 1
            entries = [
                decode_entry(int(ptr, 16), index, position)
                for position, ptr in enumerate(captured.get("pointers", []))
            ]
            buckets.append({
                "slot": PRIORITY_SLOT_NAMES[index],
                "slotIndex": index,
                "descriptorOffset": captured["descriptorOff"],
                "dataPointerOffset": captured["pointerOff"],
                "dataPointer": captured["ptr"],
                "coherent": captured["coherent"],
                "count": captured["count"],
                "capacity": captured["capacity"],
                "flags": captured["flags"],
                "payloadOk": captured["payloadOk"],
                "entries": entries,
            })
        return {
            "root": root["ptr"],
            "arrayDescriptorBase": f"0x{PRIORITY_ARRAY_BASE:04X}",
            "arrayDescriptorSize": PRIORITY_ARRAY_SIZE,
            "arrayCount": len(PRIORITY_SLOT_NAMES),
            "captureCaveat": None,
            "coherentDescriptors": coherent_count,
            "nonEmptyBuckets": sum(bucket["count"] != 0 for bucket in buckets),
            "validPriorityPointers": valid_pointers,
            "unknownPriorityPointers": unknown_pointers,
            "relocationCount": len(relocations),
            "relocations": relocations,
            "buckets": buckets,
        }

    # Build 42 captured `objects` and `priorityNodes` in two passes while the
    # game was running. The allocator can rotate mpArray between those passes,
    # so node.slotOff (the second-pass root offset) is authoritative.
    node_by_offset = {int(node["slotOff"], 16): node for node in nodes}

    for index, slot_name in enumerate(PRIORITY_SLOT_NAMES):
        descriptor = PRIORITY_ARRAY_BASE + index * PRIORITY_ARRAY_SIZE
        pointer_offset = descriptor + 0x10
        if descriptor + PRIORITY_ARRAY_SIZE > len(root_data):
            break

        root_count = u32(root_data, descriptor + 0x04)
        root_capacity = u32(root_data, descriptor + 0x08)
        root_flags = u32(root_data, descriptor + 0x0C)
        root_data_ptr = u32(root_data, pointer_offset)

        node = node_by_offset.get(pointer_offset)
        if node is None:
            continue
        node_data = bytes.fromhex(node["hex"])
        entries = []

        # Each valid bucket begins with count contiguous cPrioParam pointers.
        # Stop at the first other word. Later matching words belong to stale or
        # adjacent allocator storage and caused the old "score node" false lead.
        for position in range(min(16, len(node_data) // 4)):
            prio_ptr = u32(node_data, position * 4)
            if prio_ptr not in prio_by_ptr:
                break
            entries.append(decode_entry(prio_ptr, index, position))

        buckets.append({
            "slot": slot_name,
            "slotIndex": index,
            "descriptorOffset": f"0x{descriptor:04X}",
            "dataPointerOffset": f"0x{pointer_offset:04X}",
            "capturedDataPointer": node["ptr"],
            "capturedCount": len(entries),
            "countSource": "consecutive known pointers in second-pass payload",
            "earlierRootCount": root_count,
            "earlierRootCapacity": root_capacity,
            "earlierRootFlags": root_flags,
            "earlierRootDataPointer": f"0x{root_data_ptr:08X}",
            "entries": entries,
        })

    return {
        "root": root["ptr"],
        "arrayDescriptorBase": f"0x{PRIORITY_ARRAY_BASE:04X}",
        "arrayDescriptorSize": PRIORITY_ARRAY_SIZE,
        "arrayCount": len(PRIORITY_SLOT_NAMES),
        "captureCaveat": "root object and bucket payloads were captured in two live passes",
        "nonEmptyBuckets": len(buckets),
        "validPriorityPointers": valid_pointers,
        "unknownPriorityPointers": unknown_pointers,
        "relocationCount": len(relocations),
        "relocations": relocations,
        "buckets": buckets,
    }


def priority_rule_report(
    snapshot: dict[str, Any], catalog: dict[str, Any]
) -> dict[str, Any] | None:
    captured_rules = snapshot.get("priorityRules")
    if not captured_rules:
        return None

    prio_by_ptr = priority_objects(snapshot)
    catalog_by_tuple = priority_catalog_index(catalog)
    errors = []
    vanilla_differences = []
    rows = []
    mutation = snapshot.get("priorityMutation") or {}
    profile_desired = {
        (
            row["sensor"], row["code"], row["category"],
            row["objectId"], row["extra"], row["ruleIndex"],
        ): row["desiredAddS32"]
        for row in snapshot.get("profileRules", [])
        if row.get("applied")
    }
    personality_count = 0
    personality_check_count = 0
    order_count = 0
    personality_vtables: Counter[str] = Counter()
    order_vtables: Counter[str] = Counter()

    for captured in captured_rules:
        prio_ptr = int(captured["prioPtr"], 16)
        prio_obj = prio_by_ptr.get(prio_ptr)
        live_row = priority_tuple(prio_obj) if prio_obj else None
        definitions = catalog_by_tuple.get(live_row, []) if live_row else []
        if len(definitions) != 1:
            errors.append({
                "kind": "priorityCatalogMatch",
                "prioPtr": captured["prioPtr"],
                "matches": len(definitions),
            })
            continue
        definition = definitions[0]
        expected_personalities = definition["personalities"]
        expected_orders = definition["orders"]
        if captured["personalityCount"] != len(expected_personalities):
            errors.append({
                "kind": "personalityCount", "code": definition["code"],
                "actual": captured["personalityCount"],
                "expected": len(expected_personalities),
            })
        if captured["orderCount"] != len(expected_orders):
            errors.append({
                "kind": "orderCount", "code": definition["code"],
                "actual": captured["orderCount"], "expected": len(expected_orders),
            })

        decoded_personalities = []
        for index, (item, expected) in enumerate(zip(
            captured["personalityItems"], expected_personalities
        )):
            data = bytes.fromhex(item["hex"])
            if not item.get("ok") or len(data) < 0x24:
                errors.append({
                    "kind": "personalityRead", "code": definition["code"],
                    "index": index,
                })
                continue
            vtable = u32(data, 0x00)
            add_s32 = struct.unpack_from("<i", data, 0x04)[0]
            add_f32 = f32(data, 0x08)
            break_after_apply = u32(data, 0x0C)
            check_count = u32(data, 0x14)
            personality_vtables[f"0x{vtable:08X}"] += 1
            personality_count += 1
            personality_check_count += check_count
            actual = (add_s32, round(add_f32, 6), break_after_apply, check_count)
            vanilla = (
                expected["addS32"], round(expected["addF32"], 6),
                expected["breakAfterApply"], len(expected["checks"]),
            )
            wanted_add_s32 = expected["addS32"]
            profile_key = (
                definition["sensor"], definition["code"],
                definition["category"], definition["objectId"],
                definition["extra"], index,
            )
            if profile_key in profile_desired:
                wanted_add_s32 = profile_desired[profile_key]
            elif (
                mutation.get("enabled")
                and definition["code"] == mutation.get("targetCode")
                and index == 0
            ):
                wanted_add_s32 = mutation.get("testAddS32", wanted_add_s32)
            wanted = (wanted_add_s32, vanilla[1], vanilla[2], vanilla[3])
            if actual != vanilla:
                vanilla_differences.append({
                    "code": definition["code"], "index": index,
                    "actual": actual, "vanilla": vanilla,
                })
            if actual != wanted:
                errors.append({
                    "kind": "personalityFields", "code": definition["code"],
                    "index": index, "actual": actual, "expectedForCapture": wanted,
                })
            decoded_personalities.append({
                "index": index,
                "ptr": item["ptr"],
                "addS32": add_s32,
                "addF32": round(add_f32, 6),
                "breakAfterApply": break_after_apply,
                "checkCount": check_count,
                "checksFromCatalog": expected["checks"],
            })

        decoded_orders = []
        for index, (item, expected) in enumerate(zip(
            captured["orderItems"], expected_orders
        )):
            data = bytes.fromhex(item["hex"])
            if not item.get("ok") or len(data) < 0x0C:
                errors.append({
                    "kind": "orderRead", "code": definition["code"],
                    "index": index,
                })
                continue
            vtable, value, order_type = struct.unpack_from("<3I", data, 0)
            order_vtables[f"0x{vtable:08X}"] += 1
            order_count += 1
            if (value, order_type) != (expected["value"], expected["type"]):
                errors.append({
                    "kind": "orderFields", "code": definition["code"],
                    "index": index, "actual": [value, order_type],
                    "expected": [expected["value"], expected["type"]],
                })
            decoded_orders.append({
                "index": index, "ptr": item["ptr"],
                "value": value, "type": order_type,
            })

        if decoded_personalities or decoded_orders:
            rows.append({
                "prioPtr": captured["prioPtr"],
                "slot": definition["slot"],
                "sensor": definition["sensor"],
                "code": definition["code"],
                "extra": definition["extra"],
                "personalities": decoded_personalities,
                "orders": decoded_orders,
            })

    return {
        "priorityRowsCaptured": len(captured_rules),
        "personalityArraysReadable": sum(
            bool(row.get("personalityArrayOk")) for row in captured_rules
        ),
        "orderArraysReadable": sum(bool(row.get("orderArrayOk")) for row in captured_rules),
        "personalityRuleCount": personality_count,
        "personalityCheckCount": personality_check_count,
        "orderRuleCount": order_count,
        "personalityVtables": dict(personality_vtables),
        "orderVtables": dict(order_vtables),
        "vanillaDifferenceCount": len(vanilla_differences),
        "vanillaDifferences": vanilla_differences,
        "validationErrorCount": len(errors),
        "validationErrors": errors,
        "layouts": {
            "cCodeParam": {
                "size": 104, "addS32": "0x04", "addF32": "0x08",
                "breakAfterApply": "0x0C", "checkCount": "0x14",
                "checkArrayPointer": "0x20",
            },
            "cOrderValue": {"size": 12, "value": "0x04", "type": "0x08"},
        },
        "rowsWithRules": rows,
    }


def parameter_index(catalog: dict[str, Any]) -> dict[tuple[Any, ...], list[tuple[str, int, str]]]:
    result: dict[tuple[Any, ...], list[tuple[str, int, str]]] = defaultdict(list)
    for table, payload in catalog["actionParameters"].items():
        for index, row in enumerate(payload["rows"]):
            for level in ("Lv1", "Lv2", "LvEX"):
                keys = [
                    f"RangeMinXZ_{level}", f"RangeMaxXZ_{level}",
                    f"RangeMinY_{level}", f"RangeMaxY_{level}",
                    f"EnableMinXZ_{level}", f"EnableMaxXZ_{level}",
                    f"ElementAttr_{level}", f"AtkAttr_{level}", f"UseAtr_{level}",
                ]
                result[tuple(row[key] for key in keys)].append((table, index, level))
    return result


def live_parameter(obj: dict[str, Any]) -> tuple[Any, ...] | None:
    data = bytes.fromhex(obj["headHex"])
    if len(data) < 0x27C:
        return None
    floats = tuple(round(value, 7) for value in struct.unpack_from("<6f", data, 0x258))
    attrs = struct.unpack_from("<3I", data, 0x270)
    value = floats + attrs
    return value if any(item != 0 for item in value) else None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshots", nargs="+", type=Path)
    parser.add_argument(
        "--catalog", type=Path,
        default=Path("docs/PLAYER_PAWN_WORK/generated/pawn_ai_catalog.json"),
    )
    args = parser.parse_args()

    snapshots = [json.loads(path.read_text(encoding="utf-8")) for path in args.snapshots]
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    param_index = parameter_index(catalog)
    maps = [object_map(snapshot) for snapshot in snapshots]
    common = set.intersection(*(set(mapping) for mapping in maps))

    changed = [
        key for key in common
        if len({mapping[key]["hash"] for mapping in maps}) > 1
    ]

    result: dict[str, Any] = {
        "snapshots": [],
        "commonObjects": len(common),
        "changedObjects": len(changed),
        "changedNames": dict(Counter(key[0] for key in changed)),
        "liveActionParameterMatches": [],
    }

    for path, snapshot in zip(args.snapshots, snapshots):
        planner = one_by_name(snapshot, "cAIGoalPlanning")
        selected_code_raw = None
        if planner:
            planner_data = bytes.fromhex(planner["headHex"])
            if len(planner_data) >= 0x180:
                selected_code_raw = u32(planner_data, 0x17C)
        selected_code = (
            None if selected_code_raw in (None, NO_PRIORITY)
            else selected_code_raw
        )
        prio = priority_object(snapshot, selected_code)
        priority_definition = None
        if selected_code is not None:
            definitions = [
                row for row in catalog["priorityThink"]["rows"]
                if row["code"] == selected_code
            ]
            priority_definition = definitions[0] if len(definitions) == 1 else definitions
        active_plan = None
        if planner and selected_code is not None:
            plan_ptr = int(planner["ptr"], 16) + 0x190 + selected_code * 0x110
            plan_key = ("cAIGoalPlanning::cPlanCtrl", f"0x{plan_ptr:08X}")
            plan_obj = object_map(snapshot).get(plan_key)
            active_plan = {
                "ptr": f"0x{plan_ptr:08X}",
                "found": plan_obj is not None,
                "hash": plan_obj["hash"] if plan_obj else None,
            }
        result["snapshots"].append({
            "file": str(path),
            "seq": snapshot["seq"],
            "selectedPriorityCodeRaw": selected_code_raw,
            "selectedPriorityCode": selected_code,
            "hasSelectedPriority": selected_code is not None,
            "priorityMutation": snapshot.get("priorityMutation"),
            "priorityProfile": snapshot.get("priorityProfile"),
            "profileRules": snapshot.get("profileRules", []),
            "selectedPriorityObject": prio["ptr"] if prio else None,
            "priorityDefinition": priority_definition,
            "activePlanCtrl": active_plan,
            "inclinations": inclination_rows(snapshot),
            "priorityBuckets": priority_bucket_report(snapshot, catalog),
            "priorityRuleValidation": priority_rule_report(snapshot, catalog),
        })

    last = snapshots[-1]
    first_map = maps[0]
    last_map = maps[-1]
    for key in changed:
        name, ptr = key
        if not name.startswith("cCmc") or name == "cCmcInfo":
            continue
        value = live_parameter(last_map[key])
        if value is None:
            continue
        result["liveActionParameterMatches"].append({
            "name": name,
            "ptr": ptr,
            "hashBefore": first_map[key]["hash"],
            "hashAfter": last_map[key]["hash"],
            "liveTuple": value,
            "catalogMatches": param_index.get(value, []),
        })

    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

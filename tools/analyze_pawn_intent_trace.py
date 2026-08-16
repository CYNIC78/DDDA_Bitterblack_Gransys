#!/usr/bin/env python3
"""Join a compact Build 53 intent CSV with a same-session AI baseline.

The baseline owns the runtime ActionInterfaceParam pointer map. The trace only
records selected PlanCtrl links, exact cPlAct and uCmc+0x2EB8 target transitions.
"""
from __future__ import annotations

import argparse
import csv
import json
import struct
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SEMANTICS = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_priority_semantics.json"


def ptr(value: str | int | None) -> int:
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    return int(value, 0)


def resource_path(resource: dict) -> str:
    data = bytes.fromhex(resource.get("hex", ""))
    if len(data) <= 8:
        return ""
    return data[8:0x48].split(b"\0", 1)[0].decode("ascii", "replace")


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def build_interface_index(snapshot: dict) -> dict[int, list[dict]]:
    index: dict[int, list[dict]] = defaultdict(list)
    for resource in snapshot.get("goapResources", []):
        path = resource_path(resource)
        resource_name = path.split("\\")[-1] if path else ""
        for target in resource.get("targets", []):
            for child in target.get("children", []):
                if child.get("name") != "rAIGoalPlanning::ActionInterfaceParam":
                    continue
                data = bytes.fromhex(child.get("hex", ""))
                if len(data) < 12:
                    continue
                row = {
                    "resource": resource_name,
                    "resourcePath": path,
                    "interfaceId": u32(data, 0x04),
                    "executionInterfacePtr": f"0x{u32(data, 0x08):08X}",
                }
                if row not in index[ptr(child.get("ptr"))]:
                    index[ptr(child.get("ptr"))].append(row)
    return dict(index)


def load_trace(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8-sig", newline="") as handle:
        lines = (line for line in handle if not line.startswith("#"))
        return list(csv.DictReader(lines))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=Path, help="same-session ddda_pawn_ai_bridge JSON")
    parser.add_argument("trace", type=Path, help="ddda_pawn_intent_trace CSV")
    parser.add_argument("--semantics", type=Path, default=DEFAULT_SEMANTICS)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--compact-out", type=Path,
                        help="optional aggregate-only evidence JSON")
    args = parser.parse_args()

    snapshot = json.loads(args.snapshot.read_text(encoding="utf-8-sig"))
    semantics_payload = json.loads(args.semantics.read_text(encoding="utf-8-sig"))
    semantics = {entry["code"]: entry for entry in semantics_payload["entries"]}
    interface_index = build_interface_index(snapshot)
    raw_rows = load_trace(args.trace)

    rows: list[dict] = []
    code_counts: Counter[int] = Counter()
    action_counts: Counter[str] = Counter()
    unknown_actions: dict[int, Counter[str]] = defaultdict(Counter)
    planner_inactive_actions: Counter[str] = Counter()
    for raw in raw_rows:
        code = int(raw["priorityCode"])
        code_counts[code] += 1
        action = raw.get("actionName", "")
        action_counts[action] += 1
        semantic = semantics.get(code, {})
        link_ptrs = [ptr(value) for value in raw.get("nodeLinks", "").split(";") if value]
        matches = []
        for link_ptr in link_ptrs:
            for match in interface_index.get(link_ptr, []):
                candidate = {"linkPtr": f"0x{link_ptr:08X}", **match}
                if candidate not in matches:
                    matches.append(candidate)
        if code == 0xFFFFFFFF and action:
            planner_inactive_actions[action] += 1
        elif not semantic.get("goapCandidates") and action:
            unknown_actions[code][action] += 1
        rows.append({
            "ms": int(raw["ms"]),
            "priorityCode": code,
            "semantic": semantic.get("displayName", f"unknown code {code}"),
            "semanticConfidence": semantic.get("confidence", "unknown"),
            "actionName": action,
            "actionPtr": raw.get("actionPtr"),
            "packedCode": raw.get("packedCode"),
            "targetPtr": raw.get("targetPtr"),
            "targetName": raw.get("targetName", ""),
            "targetMode": raw.get("targetMode") or (
                "none" if not ptr(raw.get("targetPtr")) else
                "retained_no_priority" if code == 0xFFFFFFFF else "planner_target"
            ),
            "nodeLinks": [f"0x{value:08X}" for value in link_ptrs],
            "goapMatches": matches,
        })

    code_actions: dict[int, Counter[str]] = defaultdict(Counter)
    code_packed: dict[int, Counter[str]] = defaultdict(Counter)
    code_targets: dict[int, Counter[str]] = defaultdict(Counter)
    code_resource_rows: dict[int, Counter[str]] = defaultdict(Counter)
    code_resource_ids: dict[int, dict[str, set[int]]] = defaultdict(lambda: defaultdict(set))
    target_types: Counter[str] = Counter()
    target_modes: Counter[str] = Counter()
    target_ptrs: dict[str, set[str]] = defaultdict(set)
    for row in rows:
        target_modes[row["targetMode"]] += 1
        code = row["priorityCode"]
        code_actions[code][row["actionName"]] += 1
        code_packed[code][row["packedCode"]] += 1
        code_targets[code][row["targetName"] or "none"] += 1
        if ptr(row["targetPtr"]):
            target_types[row["targetName"] or "unnamed"] += 1
            target_ptrs[row["targetName"] or "unnamed"].add(row["targetPtr"])
        resources_this_row = set()
        for match in row["goapMatches"]:
            resource = match["resource"]
            resources_this_row.add(resource)
            interface_id = match.get("interfaceId")
            if interface_id is not None:
                code_resource_ids[code][resource].add(interface_id)
        for resource in resources_this_row:
            code_resource_rows[code][resource] += 1

    code_summaries = {}
    for code in sorted(code_counts):
        semantic = semantics.get(code, {})
        code_summaries[str(code)] = {
            "semantic": semantic.get("displayName", "No selected priority"
                                     if code == 0xFFFFFFFF else f"unknown code {code}"),
            "confidence": semantic.get("confidence", "planner_not_selected"
                                       if code == 0xFFFFFFFF else "unknown"),
            "rows": code_counts[code],
            "actions": dict(code_actions[code].most_common()),
            "packedCodes": dict(code_packed[code].most_common()),
            "targetTypes": dict(code_targets[code].most_common()),
            "goapResources": {
                resource: {
                    "rowsLinked": count,
                    "interfaceIds": sorted(code_resource_ids[code][resource]),
                }
                for resource, count in code_resource_rows[code].most_common()
            },
        }

    notable_actions = {
        "cPlActJump", "cPlActJumpBegin", "cPlCliffHangBegin",
        "cPlCliffHangClimb", "cPlActGather", "cPlActOpenTreasureBox",
        "cPlActCmcNeardeath", "cPlActCmcReturn", "cPlActLifted",
        "cPlActLiftedEnd",
    }
    notable_spans = []
    span_start = 0
    while span_start < len(rows):
        action = rows[span_start]["actionName"]
        span_end = span_start
        while span_end + 1 < len(rows) and rows[span_end + 1]["actionName"] == action:
            span_end += 1
        if action in notable_actions:
            span_rows = rows[span_start:span_end + 1]
            notable_spans.append({
                "actionName": action,
                "startMs": span_rows[0]["ms"],
                "endMs": span_rows[-1]["ms"],
                "rows": len(span_rows),
                "priorityCodes": sorted({row["priorityCode"] for row in span_rows}),
                "targetTypes": sorted({row["targetName"] or "none" for row in span_rows}),
                "previousAction": rows[span_start - 1]["actionName"] if span_start else "",
                "nextAction": rows[span_end + 1]["actionName"]
                              if span_end + 1 < len(rows) else "",
            })
        span_start = span_end + 1

    summary = {
        "durationMs": max((row["ms"] for row in rows), default=0),
        "transitionRowCount": len(rows),
        "uniquePriorityCodes": sorted(code_counts),
        "priorityCodeCounts": {str(key): value for key, value in sorted(code_counts.items())},
        "exactActionCounts": dict(action_counts.most_common()),
        "notableActionSpans": notable_spans,
        "targetSummary": {
            "nonNullRows": sum(target_types.values()),
            "modes": dict(target_modes.most_common()),
            "types": dict(target_types.most_common()),
            "uniquePointersByType": {
                name: len(values) for name, values in sorted(target_ptrs.items())
            },
        },
        "plannerInactiveActions": dict(planner_inactive_actions.most_common()),
        "unmappedCodeActions": {
            str(code): dict(counts.most_common())
            for code, counts in sorted(unknown_actions.items())
        },
        "codeSummaries": code_summaries,
    }
    payload = {
        "schema": "ddda-pawn-intent-trace-analysis-v2",
        "source": {
            "snapshot": args.snapshot.name,
            "trace": args.trace.name,
            "build": snapshot.get("build", ""),
        },
        "summary": summary,
        "rows": rows,
    }
    out = args.out or args.trace.with_name(args.trace.stem + "_analysis.json")
    out.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {out}: {len(rows)} rows, {len(code_counts)} priority codes")
    if args.compact_out:
        compact = {key: value for key, value in payload.items() if key != "rows"}
        args.compact_out.parent.mkdir(parents=True, exist_ok=True)
        args.compact_out.write_text(
            json.dumps(compact, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"wrote {args.compact_out}: compact aggregate evidence")


if __name__ == "__main__":
    main()

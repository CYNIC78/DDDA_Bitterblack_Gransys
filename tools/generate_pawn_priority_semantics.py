#!/usr/bin/env python3
"""Generate the conservative Main Pawn priority semantic map.

The map never guesses an exact intent from slot position alone. Runtime-observed
codes are marked `observed`; everything else remains `family_only`/`unknown`
until Build 48+ links its planner/GOAP resource.
"""

from __future__ import annotations

import csv
import json
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_ai_catalog.json"
OUT_JSON = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_priority_semantics.json"
OUT_CSV = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_priority_semantics.csv"
OUT_CPP = ROOT / "src/devtools/generated/PawnPrioritySemantics.inl"
PLANNER_MAP = ROOT / "docs/PLAYER_PAWN_WORK/generated/pawn_planner_semantics.json"

OBSERVED = {
    0: {
        "intentKey": "wait_or_quest_fallback",
        "displayName": "Wait / quest fallback (code-level ambiguous)",
        "goapCandidates": ["Wait"],
        "confidence": "observed_ambiguous",
        "evidence": [
            "Build 41/45: cPlActWait with planner code 0",
            "Build 50: selected PlanCtrl nodes link to Wait ID 0 and Follow ID 1 fallback",
            "cmc.prt: code 0 is shared by 16 quest rows; full tuple is required",
        ],
    },
    1: {
        "intentKey": "follow_arisen",
        "displayName": "Follow Arisen",
        "goapCandidates": ["Follow"],
        "confidence": "compiled_links",
        "evidence": [
            "Build 40/42: cPlActRun while following with planner code 1",
            "Build 50: selected PlanCtrl nodes link directly to Follow ID 1 and Jump ID 2",
        ],
    },
    35: {
        "intentKey": "victory_pose",
        "displayName": "Victory Pose",
        "goapCandidates": ["VictoryPose"],
        "confidence": "planner_static_links",
        "evidence": [
            "Build 52: static planner slot 35 links directly to VictoryPose ID 117",
            "Build 50 selected runtime graph also carried ItemThrow/Follow transient links; they are not the slot's primary resource",
        ],
    },
    54: {
        "intentKey": "weapon_attack_dagger",
        "displayName": "Dagger weapon attack",
        "goapCandidates": ["WpnDaggerAtk"],
        "confidence": "compiled_links",
        "evidence": [
            "Build 40/42: dagger attack/Hyakuretsu with planner code 54",
            "Build 51: exact cPlActWpnDaggerAtckLandL, packed 0x01050003",
            "Build 51: PlanCtrl links to WpnDaggerAtk Interface IDs 9,124,132,133,10",
        ],
    },
    57: {
        "intentKey": "weapon_attack_bow",
        "displayName": "Bow weapon attack",
        "goapCandidates": ["WpnBowAtk2"],
        "confidence": "compiled_links",
        "evidence": [
            "Build 51: exact cPlActWpnBow, packed 0x02060000, planner code 57",
            "Build 51: PlanCtrl links to runtime-only WpnBowAtk2 IDs 12/107",
            "Build 51: DmgUkemi ID 149 is present as auxiliary/fallback link",
        ],
    },
    74: {
        "intentKey": "combat_evasive_response",
        "displayName": "Combat evasive response",
        "goapCandidates": ["EscapeNotice2"],
        "confidence": "runtime_exact",
        "evidence": [
            "Build 53: planner-only code 74 selected for 9 trace rows",
            "Build 53: exact EscapeNotice2 ID 167 link and uHumanEnemy target in every row",
            "Build 53: jump/walk/bow execution during this evasive combat intent",
        ],
    },
    76: {
        "intentKey": "gather_at_object",
        "displayName": "Gather / inspect object",
        "goapCandidates": ["GotoOm"],
        "confidence": "runtime_exact",
        "evidence": [
            "Build 53: planner-only code 76 selected for 35 trace rows",
            "Build 53: exact GotoOm ID 168 link in every row",
            "Build 53: cPlActGather packed 0x00000091 in 18 rows while looting/inspecting bodies",
        ],
    },
}

FAMILY = {
    "QUEST": ("quest", "Quest interaction"),
    "PL_Party": ("party", "Party/Arisen relation"),
    "Situ_Personal": ("personal", "Personal/situation response"),
    "Enemy": ("enemy", "Enemy-related response"),
    "Wait_Follow": ("wait_follow", "Wait/follow tactical response"),
    "Etc": ("misc", "Miscellaneous/battle response"),
}


def family_of(slot: str) -> tuple[str, str]:
    prefix = slot.rsplit(" - ", 1)[0]
    return FAMILY.get(prefix, ("unknown", "Unknown family"))


def snake_case(name: str) -> str:
    value = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)
    return re.sub(r"[^a-zA-Z0-9]+", "_", value).strip("_").lower()


def display_name(name: str) -> str:
    return re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", name).replace("Wpn", "Weapon")


def main() -> None:
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    grouped: dict[int, list[dict]] = defaultdict(list)
    for row in catalog["priorityThink"]["rows"]:
        grouped[row["code"]].append(row)

    planner_direct = {}
    if PLANNER_MAP.exists():
        planner_payload = json.loads(PLANNER_MAP.read_text(encoding="utf-8"))
        planner_direct = {
            entry["code"]: entry
            for entry in planner_payload.get("entries", [])
            if entry.get("status") == "direct_goap_link" and entry.get("resources")
        }

    entries = []
    # Runtime planner codes span all 91 cPlanCtrl slots. cmc.prt source rows
    # currently use 70 codes in 0..73, but Build 53 proved planner-only slots
    # 74 and 76 can also become selected during live gameplay.
    for code in range(91):
        rows = grouped.get(code, [])
        families = []
        for row in rows:
            fam = family_of(row["slot"])[0]
            if fam not in families:
                families.append(fam)
        known = OBSERVED.get(code)
        planner = planner_direct.get(code)
        if known:
            semantic = dict(known)
            if planner:
                semantic["goapCandidates"] = list(dict.fromkeys(
                    semantic["goapCandidates"] + planner["resources"]
                ))
                semantic["evidence"] = semantic["evidence"] + [
                    f"Build 52 static planner slot: {', '.join(planner['resources'])}"
                ]
        elif planner and rows:
            resource = planner["resources"][0]
            semantic = {
                "intentKey": snake_case(resource),
                "displayName": display_name(resource),
                "goapCandidates": planner["resources"],
                "confidence": "planner_static_links",
                "evidence": [
                    f"Build 52: planner slot {code} links directly to {resource} ActionInterfaceParam"
                ],
            }
        elif planner:
            resource = planner["resources"][0]
            semantic = {
                "intentKey": snake_case(resource),
                "displayName": display_name(resource),
                "goapCandidates": planner["resources"],
                "confidence": "planner_only",
                "evidence": [
                    f"Build 52: planner slot exists and links to {resource}, but cmc.prt has no source row"
                ],
            }
        elif rows:
            labels = [family_of(row["slot"])[1] for row in rows]
            semantic = {
                "intentKey": f"unknown_code_{code}",
                "displayName": labels[0] if len(set(labels)) == 1 else "Mixed priority family",
                "goapCandidates": [],
                "confidence": "family_only",
                "evidence": ["Only cmc.prt source-slot family is known"],
            }
        else:
            semantic = {
                "intentKey": f"unused_or_missing_code_{code}",
                "displayName": "Unused/missing in cmc.prt",
                "goapCandidates": [],
                "confidence": "missing",
                "evidence": ["No cmc.prt row uses this planner code"],
            }

        entries.append({
            "code": code,
            **semantic,
            "families": families,
            "sourceRows": [
                {
                    "slot": row["slot"],
                    "index": row["index"],
                    "identity": [
                        row["sensor"], row["code"], row["category"],
                        row["objectId"], row["extra"],
                    ],
                    "personalityRuleCount": len(row["personalities"]),
                    "orderRuleCount": len(row["orders"]),
                }
                for row in rows
            ],
            "runtime": {
                "mainPlannerConfirmed": code in OBSERVED or code in planner_direct,
                "goapResourceConfirmed": code in planner_direct,
                "currentTargetConfirmed": False,
            },
        })

    payload = {
        "schemaVersion": 1,
        "scope": "Main Pawn / 91 runtime planner slots plus cmc.prt source rows",
        "status": "Build 53 validated: 42/70 cmc.prt codes plus planner-only runtime slots",
        "codeRange": [0, 90],
        "plannerSlotCount": len(entries),
        "mappedPlannerSlotCount": sum(bool(entry["goapCandidates"]) for entry in entries),
        "usedCodeCount": len(grouped),
        "mappedUsedCodeCount": sum(
            bool(entry["goapCandidates"]) and bool(entry["sourceRows"])
            for entry in entries
        ),
        "missingCodes": [code for code in range(91) if code not in grouped],
        "confidenceLevels": {
            "compiled_links": "selected PlanCtrl and exact cPlAct link to named GOAP resources",
            "planner_static_links": "Build 52 planner slot links directly to one named GOAP resource",
            "planner_only": "planner slot is named but no cmc.prt source row uses this code",
            "runtime_exact": "static GOAP identity and repeated exact runtime actions agree",
            "observed": "priority code and behavior observed together at runtime",
            "observed_ambiguous": "runtime behavior observed, but code is shared by multiple source rows",
            "family_only": "only broad cmc.prt slot family is known",
            "missing": "no cmc.prt source row and no direct planner link",
        },
        "entries": entries,
    }
    OUT_JSON.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    with OUT_CSV.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow([
            "code", "intentKey", "displayName", "confidence", "families",
            "goapCandidates", "sourceRowCount", "sourceSlots", "evidence",
        ])
        for entry in entries:
            writer.writerow([
                entry["code"], entry["intentKey"], entry["displayName"],
                entry["confidence"], "|".join(entry["families"]),
                "|".join(entry["goapCandidates"]), len(entry["sourceRows"]),
                "|".join(row["slot"] for row in entry["sourceRows"]),
                "|".join(entry["evidence"]),
            ])

    OUT_CPP.parent.mkdir(parents=True, exist_ok=True)
    cpp_lines = [
        "// Generated by tools/generate_pawn_priority_semantics.py. Do not edit.",
        "static const char* PawnPriorityIntentName(unsigned code)",
        "{",
        "    switch (code) {",
    ]
    for entry in entries:
        label = entry["displayName"].replace("\\", "\\\\").replace('"', '\\"')
        cpp_lines.append(f'    case {entry["code"]}u: return "{label}";')
    cpp_lines.extend([
        '    default: return "No selected priority";',
        "    }",
        "}",
        "",
        "static bool PawnPriorityIntentMapped(unsigned code)",
        "{",
        "    switch (code) {",
    ])
    mapped_codes = [entry["code"] for entry in entries if entry["goapCandidates"]]
    for code in mapped_codes:
        cpp_lines.append(f"    case {code}u: return true;")
    cpp_lines.extend([
        "    default: return false;",
        "    }",
        "}",
        "",
    ])
    OUT_CPP.write_text("\n".join(cpp_lines), encoding="ascii")

    print(f"wrote {OUT_JSON} ({len(entries)} codes)")
    print(f"wrote {OUT_CSV}")
    print(f"wrote {OUT_CPP} ({len(mapped_codes)} mapped codes)")


if __name__ == "__main__":
    main()

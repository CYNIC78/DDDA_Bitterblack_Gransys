#!/usr/bin/env python3
"""Diff 84.30 PS: REC / PS: BODY hex dumps across snapshots."""
from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

SNAP_START = "PS: ===== party SHEET"
SNAP_END = "PS: ===== end party SHEET"
REC_RE = re.compile(r"^PS: REC (\S+) \+([0-9A-Fa-f]{4})((?: [0-9A-Fa-f]{2})+)$")
BODY_RE = re.compile(r"^PS: BODY (\S+) \+([0-9A-Fa-f]{4})((?: [0-9A-Fa-f]{2})+)$")
RSP_RE = re.compile(r"^PS: RSP (\S+) \+([0-9A-Fa-f]{4})((?: [0-9A-Fa-f]{2})+)$")
SHEET_RE = re.compile(r"^PS: SHEET (\S+) rec=(0x[0-9a-fA-F]+) body=(0x[0-9a-fA-F]+)")
KIND_RE = re.compile(r"^PS: SHEET (\S+) bodyKind=(\S+) bytes=(\d+)")
STATS_RE = re.compile(r"^PS: SHEET (\S+) voc=")


def parse_hex_bytes(s: str) -> bytes:
    return bytes(int(x, 16) for x in s.split())


def parse_log(path: Path):
    snaps = []
    cur = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(SNAP_START):
            cur = {"members": {}, "order": []}
            snaps.append(cur)
            continue
        if cur is None:
            continue
        if line.startswith(SNAP_END):
            cur = None
            continue
        m = SHEET_RE.match(line)
        if m:
            name = m.group(1)
            if name not in cur["members"]:
                cur["members"][name] = {
                    "rec_addr": m.group(2),
                    "body_addr": m.group(3),
                    "rec": {},
                    "body": {},
                    "rsp": {},
                    "kind": "",
                    "stats": "",
                }
                cur["order"].append(name)
            else:
                cur["members"][name]["rec_addr"] = m.group(2)
                cur["members"][name]["body_addr"] = m.group(3)
            continue
        m = KIND_RE.match(line)
        if m:
            cur["members"].setdefault(m.group(1), {}).update(kind=m.group(2))
            continue
        if STATS_RE.match(line):
            name = line.split()[2]
            if name in cur["members"]:
                cur["members"][name]["stats"] = line
            continue
        for rx, key in ((REC_RE, "rec"), (BODY_RE, "body"), (RSP_RE, "rsp")):
            m = rx.match(line)
            if not m:
                continue
            name, off_s, hexpart = m.group(1), m.group(2), m.group(3)
            off = int(off_s, 16)
            blob = parse_hex_bytes(hexpart)
            mem = cur["members"].setdefault(
                name,
                {"rec": {}, "body": {}, "rsp": {}, "kind": "", "stats": "",
                 "rec_addr": "", "body_addr": ""},
            )
            for i, b in enumerate(blob):
                mem[key][off + i] = b
            break
    return snaps


def blob_from_map(d: dict) -> tuple[int, bytearray]:
    if not d:
        return 0, bytearray()
    lo, hi = min(d), max(d)
    out = bytearray(hi - lo + 1)
    for off, b in d.items():
        out[off - lo] = b
    return lo, out


def diffs(a: dict, b: dict):
    keys = sorted(set(a) | set(b))
    out = []
    for k in keys:
        va, vb = a.get(k), b.get(k)
        if va != vb:
            out.append((k, va, vb))
    return out


def cluster(offs, gap=32):
    if not offs:
        return []
    offs = sorted(offs)
    runs = [[offs[0], offs[0]]]
    for o in offs[1:]:
        if o <= runs[-1][1] + gap:
            runs[-1][1] = o
        else:
            runs.append([o, o])
    return runs


def fmt_byte(v):
    return "--" if v is None else f"{v:02X}"


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "/home/user/uploads/ddda_ai_overhaul.log")
    snaps = parse_log(path)
    print(f"snapshots={len(snaps)}")
    for i, s in enumerate(snaps):
        print(f"  snap{i}: members={s['order']}")
        for name in s["order"]:
            m = s["members"][name]
            print(
                f"    {name:9} rec={m.get('rec_addr')} body={m.get('body_addr')} "
                f"kind={m.get('kind')} recB={len(m['rec'])} bodyB={len(m['body'])} rspB={len(m['rsp'])}"
            )
            if m.get("stats"):
                print(f"             {m['stats']}")

    if len(snaps) < 3:
        print("NEED 3 SNAPSHOTS")
        return 1

    names = ["MainPawn", "Hired1", "Hired2", "Arisen"]
    print("\n===== DRY1 vs WET vs DRY2 per member =====")
    wet_only = {}  # name -> set of (layer, off)
    for name in names:
        if name not in snaps[0]["members"]:
            continue
        print(f"\n--- {name} ---")
        for layer in ("rec", "body", "rsp"):
            d01 = diffs(snaps[0]["members"][name][layer], snaps[1]["members"][name][layer])
            d12 = diffs(snaps[1]["members"][name][layer], snaps[2]["members"][name][layer])
            d02 = diffs(snaps[0]["members"][name][layer], snaps[2]["members"][name][layer])
            print(f"  {layer}: dry1->wet {len(d01)}  wet->dry2 {len(d12)}  dry1->dry2 {len(d02)}")

    # Classify each body-offset: who changed dry1->wet
    print("\n===== BODY offsets that change dry1->wet, by who =====")
    body_who = defaultdict(list)  # off -> [names]
    rec_who = defaultdict(list)
    for name in names:
        if name not in snaps[0]["members"]:
            continue
        for off, a, b in diffs(snaps[0]["members"][name]["body"], snaps[1]["members"][name]["body"]):
            body_who[off].append((name, a, b))
        for off, a, b in diffs(snaps[0]["members"][name]["rec"], snaps[1]["members"][name]["rec"]):
            rec_who[off].append((name, a, b))

    # Intersect: offsets that also revert wet->dry2 for those same people
    def revert_ok(name, layer, off, wet_val):
        v2 = snaps[2]["members"][name][layer].get(off)
        v0 = snaps[0]["members"][name][layer].get(off)
        return v2 == v0 and v2 != wet_val

    print("\n-- RECORD offsets dry1->wet --")
    rec_counts = defaultdict(int)
    for off, hits in sorted(rec_who.items()):
        rec_counts[len(hits)] += 1
    print("  hit-count histogram:", dict(sorted(rec_counts.items())))

    print("\n-- BODY offsets dry1->wet histogram --")
    body_counts = defaultdict(int)
    for off, hits in body_who.items():
        body_counts[len(hits)] += 1
    print("  hit-count histogram:", dict(sorted(body_counts.items())))

    # Candidate: changed for exactly 2 pawns (not Arisen, not the dry one),
    # and reverted for those 2, and UNCHANGED for the remaining pawn + preferably arisen.
    pawns = ["MainPawn", "Hired1", "Hired2"]

    print("\n===== BODY candidates: change for 2 pawns, stable for 1 pawn, revert =====")
    cand_body = []
    for off, hits in sorted(body_who.items()):
        hit_names = [h[0] for h in hits]
        pawn_hits = [h for h in hits if h[0] in pawns]
        arisen_hit = any(h[0] == "Arisen" for h in hits)
        if len(pawn_hits) != 2:
            continue
        if arisen_hit:
            continue
        dry_name = [p for p in pawns if p not in [h[0] for h in pawn_hits]]
        if len(dry_name) != 1:
            continue
        if not all(revert_ok(h[0], "body", off, h[2]) for h in pawn_hits):
            continue
        # dry pawn unchanged dry1->wet already implied
        cand_body.append((off, pawn_hits, dry_name[0]))

    print(f"  count={len(cand_body)}")
    if cand_body:
        offs = [c[0] for c in cand_body]
        print("  clusters:", [(f"+0x{a:04X}", f"+0x{b:04X}", b - a + 1) for a, b in cluster(offs)])
        # show first 80
        for off, hits, dry in cand_body[:80]:
            bits = "  ".join(f"{n} {fmt_byte(a)}->{fmt_byte(b)}" for n, a, b in hits)
            print(f"    BODY +0x{off:04X} dry={dry}  {bits}")
        if len(cand_body) > 80:
            print(f"    ... +{len(cand_body)-80} more")

    print("\n===== BODY candidates: change for 2 pawns INCLUDING possible Arisen wet? =====")
    # Maybe arisen also wet? User said 2 of 3 pawns. Stick to pawns.

    print("\n===== RECORD candidates: change for exactly 2 pawns, revert, Arisen+dry stable =====")
    cand_rec = []
    for off, hits in sorted(rec_who.items()):
        pawn_hits = [h for h in hits if h[0] in pawns]
        arisen_hit = any(h[0] == "Arisen" for h in hits)
        if len(pawn_hits) != 2 or arisen_hit:
            continue
        dry_name = [p for p in pawns if p not in [h[0] for h in pawn_hits]]
        if len(dry_name) != 1:
            continue
        if not all(revert_ok(h[0], "rec", off, h[2]) for h in pawn_hits):
            continue
        cand_rec.append((off, pawn_hits, dry_name[0]))
    print(f"  count={len(cand_rec)}")
    for off, hits, dry in cand_rec[:80]:
        bits = "  ".join(f"{n} {fmt_byte(a)}->{fmt_byte(b)}" for n, a, b in hits)
        print(f"    REC +0x{off:04X} dry={dry}  {bits}")

    # Also: change for 2 OR 3 pawns (if user miscounted / all 3 got splash)
    print("\n===== BODY: change for 2 or 3 pawns, revert, Arisen stable =====")
    for want_n in (2, 3):
        rows = []
        for off, hits in sorted(body_who.items()):
            pawn_hits = [h for h in hits if h[0] in pawns]
            if len(pawn_hits) != want_n:
                continue
            if any(h[0] == "Arisen" for h in hits):
                continue
            if not all(revert_ok(h[0], "body", off, h[2]) for h in pawn_hits):
                continue
            rows.append((off, pawn_hits))
        print(f"  n={want_n}: {len(rows)}")
        if rows:
            offs = [r[0] for r in rows]
            print("   clusters:", [(f"+0x{a:04X}", f"+0x{b:04X}", b - a + 1) for a, b in cluster(offs, 16)])

    # Common wet pair identity
    pair_counter = defaultdict(int)
    for off, hits in body_who.items():
        pawn_hits = tuple(sorted(h[0] for h in hits if h[0] in pawns))
        if pawn_hits:
            pair_counter[pawn_hits] += 1
    print("\n===== Which pawns share BODY dry1->wet changes =====")
    for k, n in sorted(pair_counter.items(), key=lambda kv: -kv[1])[:20]:
        print(f"  {k}: {n} offsets")

    pair_counter_r = defaultdict(int)
    for off, hits in rec_who.items():
        pawn_hits = tuple(sorted(h[0] for h in hits if h[0] in pawns))
        if pawn_hits:
            pair_counter_r[pawn_hits] += 1
    print("\n===== Which pawns share REC dry1->wet changes =====")
    for k, n in sorted(pair_counter_r.items(), key=lambda kv: -kv[1])[:20]:
        print(f"  {k}: {n} offsets")

    # Look at rStatusParam — shared object, should be identical always
    print("\n===== rStatusParam (shared catalog) diffs =====")
    for name in names:
        if name not in snaps[0]["members"]:
            continue
        d01 = diffs(snaps[0]["members"][name]["rsp"], snaps[1]["members"][name]["rsp"])
        d12 = diffs(snaps[1]["members"][name]["rsp"], snaps[2]["members"][name]["rsp"])
        print(f"  {name} rsp dry1->wet {len(d01)} wet->dry2 {len(d12)}")

    # Dump interesting body windows around known status-ish areas
    # Also print any dword that looks like a timer ~90 or status id 6
    print("\n===== BODY dwords that look like status (id=6 / timer~90 / 0/1 switch) =====")
    for name in pawns:
        d01 = diffs(snaps[0]["members"][name]["body"], snaps[1]["members"][name]["body"])
        # group into dwords
        by_dw = defaultdict(dict)
        for off, a, b in d01:
            by_dw[off & ~3][off & 3] = (a, b)
        interesting = []
        for base, parts in by_dw.items():
            # reconstruct if all 4 bytes present in either snap
            def get_dw(snap_i):
                bs = []
                for i in range(4):
                    bs.append(snaps[snap_i]["members"][name]["body"].get(base + i, 0))
                return int.from_bytes(bytes(bs), "little"), bs

            v0, b0 = get_dw(0)
            v1, b1 = get_dw(1)
            v2, _ = get_dw(2)
            f0 = int.from_bytes(bytes(b0), "little")
            import struct
            fl0 = struct.unpack("<f", bytes(b0))[0]
            fl1 = struct.unpack("<f", bytes(b1))[0]
            looks = []
            if v0 == 0 and v1 == 6:
                looks.append("id0->6")
            if v0 == 0 and v1 == 1:
                looks.append("flag0->1")
            if abs(fl1 - 90.0) < 1.0 or abs(fl1 - 89.0) < 2.0:
                looks.append(f"timer~{fl1:.2f}")
            if 0.0 < fl1 <= 180.0 and fl0 == 0.0:
                looks.append(f"float0->{fl1:.3f}")
            if v0 == 0 and 1 <= v1 <= 40:
                looks.append(f"smallint->{v1}")
            if looks and revert_ok(name, "body", base, b1[0]):
                interesting.append((base, looks, v0, v1, fl0, fl1))
        print(f"  {name}: {len(interesting)} interesting dwords")
        for base, looks, v0, v1, fl0, fl1 in interesting[:40]:
            print(f"    +0x{base:04X} {looks}  i32 {v0}->{v1}  f32 {fl0:.3f}->{fl1:.3f}")

    print("\n===== REC dwords that look like status =====")
    for name in pawns:
        d01 = diffs(snaps[0]["members"][name]["rec"], snaps[1]["members"][name]["rec"])
        by_dw = set(off & ~3 for off, _, _ in d01)
        interesting = []
        import struct
        for base in sorted(by_dw):
            def get_dw(snap_i):
                bs = [snaps[snap_i]["members"][name]["rec"].get(base + i, 0) for i in range(4)]
                return int.from_bytes(bytes(bs), "little"), bs
            v0, b0 = get_dw(0)
            v1, b1 = get_dw(1)
            fl0 = struct.unpack("<f", bytes(b0))[0]
            fl1 = struct.unpack("<f", bytes(b1))[0]
            looks = []
            if v0 == 0 and v1 == 6:
                looks.append("id0->6")
            if v0 == 0 and v1 == 1:
                looks.append("flag0->1")
            if abs(fl1 - 90.0) < 2.0:
                looks.append(f"timer~{fl1:.2f}")
            if 0.0 < fl1 <= 180.0 and fl0 == 0.0:
                looks.append(f"float0->{fl1:.3f}")
            if v0 == 0 and 1 <= v1 <= 40:
                looks.append(f"smallint->{v1}")
            if looks:
                interesting.append((base, looks, v0, v1, fl0, fl1))
        print(f"  {name}: {len(interesting)} interesting dwords")
        for base, looks, v0, v1, fl0, fl1 in interesting[:40]:
            print(f"    +0x{base:04X} {looks}  i32 {v0}->{v1}  f32 {fl0:.3f}->{fl1:.3f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

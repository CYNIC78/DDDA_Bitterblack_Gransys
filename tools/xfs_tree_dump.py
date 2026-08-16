#!/usr/bin/env python3
"""Decode nested DDDA XFS object trees.

xfs_dump.py decodes schemas and flat one-class files. Pawn AI resources use
nested `class` / `classref` arrays. Their serialised object header is:

    u16 classTag (= classIndex * 2 + 1 for an inline object)
    u16 objectId
    u32 objectBytes (includes this length dword, excludes the tag)
    ... property stream ...

Every property starts with u32 elementCount. Primitive elements are packed
without padding; class elements carry the header above.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

import xfs_dump


class Decoder:
    def __init__(self, path: Path):
        self.path = path
        self.schema = xfs_dump.parse(str(path))
        raw = path.read_bytes()
        self.data = raw[self.schema["xfsAt"] :]
        self.pos = xfs_dump.BASE + self.schema["dataOffset"]
        self.objects: dict[int, dict[str, Any]] = {}
        self.warnings: list[str] = []

    def need(self, n: int) -> None:
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError(f"read past EOF at 0x{self.pos:X} (+{n})")

    def u32(self) -> int:
        self.need(4)
        value = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def blob(self, n: int) -> bytes:
        self.need(n)
        value = self.data[self.pos : self.pos + n]
        self.pos += n
        return value

    def serial_string(self) -> str:
        end = self.data.find(b"\0", self.pos)
        if end < 0:
            raise ValueError(f"unterminated string at 0x{self.pos:X}")
        value = self.data[self.pos:end].decode("cp932", errors="replace")
        self.pos = end + 1
        return value

    @staticmethod
    def primitive(kind: str, raw: bytes) -> Any:
        if kind in ("string", "cstring"):
            return raw.split(b"\0", 1)[0].decode("cp932", errors="replace")
        if kind == "bool":
            return bool(raw[0])
        if kind == "u8":
            return raw[0]
        if kind == "s8":
            return struct.unpack("<b", raw)[0]
        if kind == "u16":
            return struct.unpack("<H", raw)[0]
        if kind == "s16":
            return struct.unpack("<h", raw)[0]
        if kind == "u32":
            return struct.unpack("<I", raw)[0]
        if kind == "s32":
            return struct.unpack("<i", raw)[0]
        if kind == "u64":
            return struct.unpack("<Q", raw)[0]
        if kind == "s64":
            return struct.unpack("<q", raw)[0]
        if kind == "f32":
            return round(struct.unpack("<f", raw)[0], 7)
        if kind == "f64":
            return struct.unpack("<d", raw)[0]
        if kind in ("vector2", "float2") and len(raw) == 8:
            return [round(v, 7) for v in struct.unpack("<2f", raw)]
        if kind in ("vector3", "float3") and len(raw) in (12, 16):
            return [round(v, 7) for v in struct.unpack("<%df" % (len(raw) // 4), raw)]
        if kind in ("vector4", "float4", "quaternion") and len(raw) == 16:
            return [round(v, 7) for v in struct.unpack("<4f", raw)]
        return "0x" + raw.hex()

    def object_value(self) -> Any:
        tag = self.u32()
        low = tag & 0xFFFF
        object_id = tag >> 16
        if low == 0:
            return {"$null": True, "$id": object_id}
        if not (low & 1):
            return {"$ref": object_id, "$tag": low}
        class_index = (low - 1) // 2
        if class_index >= len(self.schema["classes"]):
            raise ValueError(f"bad class tag {low} at 0x{self.pos - 4:X}")
        length_pos = self.pos
        object_bytes = self.u32()
        end = length_pos + object_bytes
        if end < self.pos or end > len(self.data):
            raise ValueError(f"bad object length {object_bytes} at 0x{length_pos:X}")
        value = self.class_value(class_index, end)
        value["$class"] = class_index
        value["$id"] = object_id
        if self.pos != end:
            self.warnings.append(
                f"class {class_index} id {object_id}: parsed 0x{self.pos:X}, expected 0x{end:X}"
            )
            self.pos = end
        self.objects[object_id] = value
        return value

    def class_value(self, class_index: int, end: int | None = None) -> dict[str, Any]:
        cls = self.schema["classes"][class_index]
        out: dict[str, Any] = {}
        for prop in cls["props"]:
            if end is not None and self.pos >= end:
                self.warnings.append(
                    f"class {class_index}: object ended before property {prop['name']}"
                )
                break
            count = self.u32()
            if count > 100000:
                raise ValueError(
                    f"implausible count {count} for {prop['name']} at 0x{self.pos - 4:X}"
                )
            if prop["type"] in ("class", "classref"):
                values = [self.object_value() for _ in range(count)]
            elif prop["type"] in ("string", "cstring"):
                # Runtime field size is a buffer/pointer size. XFS stores only
                # the NUL-terminated text, with no padding to that runtime size.
                values = [self.serial_string() for _ in range(count)]
            else:
                size = prop["bytes"]
                values = [self.primitive(prop["type"], self.blob(size)) for _ in range(count)]
            out[prop["name"]] = values[0] if count == 1 else values
        return out

    def decode(self) -> dict[str, Any]:
        root_count = self.u32()
        root_bytes = self.u32()
        root_end = self.pos - 4 + root_bytes
        roots = [self.class_value(0, root_end) for _ in range(root_count)]
        if self.pos != root_end:
            self.warnings.append(f"root parsed 0x{self.pos:X}, expected 0x{root_end:X}")
        if root_end != len(self.data):
            self.warnings.append(f"root end 0x{root_end:X}, file end 0x{len(self.data):X}")
        return {
            "file": str(self.path),
            "version": self.schema["version"],
            "instanceCount": self.schema["instanceCount"],
            "classCount": self.schema["classCount"],
            "rootCount": root_count,
            "rootBytes": root_bytes,
            "roots": roots,
            "warnings": self.warnings,
        }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--compact", action="store_true")
    args = parser.parse_args()
    all_values = []
    for path in args.files:
        try:
            all_values.append(Decoder(path).decode())
        except Exception as exc:
            all_values.append({"file": str(path), "error": str(exc)})
    print(json.dumps(all_values, ensure_ascii=False, indent=None if args.compact else 2))


if __name__ == "__main__":
    main()

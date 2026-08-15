#!/usr/bin/env python3
"""Fail when a C/C++ source contains a raw newline in a string/char literal."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
FILES = sorted([*ROOT.glob("*.cpp"), *ROOT.glob("*.h"), *ROOT.glob("src/**/*.cpp"), *ROOT.glob("src/**/*.h")])
errors = []

for path in FILES:
    text = path.read_text(encoding="utf-8", errors="replace")
    state = "code"
    i = 0
    line = 1
    start_line = 0
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == "/" and n == "/":
                state = "line"
                i += 2
                continue
            if c == "/" and n == "*":
                state = "block"
                i += 2
                continue
            if c == '"':
                state, start_line = "string", line
                i += 1
                continue
            if c == "'":
                state, start_line = "char", line
                i += 1
                continue
        elif state == "line":
            if c == "\n":
                state = "code"
        elif state == "block":
            if c == "*" and n == "/":
                state = "code"
                i += 2
                continue
        elif state in ("string", "char"):
            quote = '"' if state == "string" else "'"
            if c == "\\":
                i += 2
                continue
            if c == quote:
                state = "code"
                i += 1
                continue
            if c == "\n":
                errors.append(f"{path.relative_to(ROOT)}:{start_line}: raw newline in {state} literal")
                state = "code"
        if c == "\n":
            line += 1
        i += 1
    if state in ("string", "char", "block"):
        errors.append(f"{path.relative_to(ROOT)}:{start_line}: unterminated {state}")

if errors:
    print("\n".join(errors))
    sys.exit(1)
print(f"C++ literal check: OK ({len(FILES)} files)")

#!/usr/bin/env bash
# Сверка умолчаний: код ↔ эталонный ini (85.67).
#
# Повод. PARKED §2 держал пять ключей, где код читал ключ с одним умолчанием, а
# эталонный ddda_ai_overhaul.default.ini обещал другое. На живом ini это не видно
# (ключи заданы), но на ЧИСТОЙ установке код сам впишет в создаваемую ини не то,
# что обещает справочник — то есть владелец увидит одно, а получит другое.
#
# Что проверяется: для каждого вызова config.getBool/getInt/getUInt/getFloat с
# ЛИТЕРАЛЬНЫМИ секцией, ключом и умолчанием — умолчание из кода равно значению
# из эталонного ини. Ключи, читаемые через переменные, пропускаются: их сверять
# нечем, и делать вид, что мы их проверили, нельзя.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "$ROOT" <<'PY'
import io
import os
import re
import sys

root = sys.argv[1]

# Осознанные исключения: ключ → причина. Пустой словарь — цель, а не случайность.
ALLOWED = {}


def norm_bool(x):
    x = x.strip().lower()
    if x in ('true', 'on', '1'):
        return 'on'
    if x in ('false', 'off', '0'):
        return 'off'
    return None


def norm_num(x):
    try:
        if x.strip().lower() in ('true', 'on'):
            return 1
        if x.strip().lower() in ('false', 'off'):
            return 0
        f = float(x.rstrip('fF'))
        return int(f) if f == int(f) else round(f, 6)
    except Exception:
        return None


def ini_values(path):
    out, sec = {}, ''
    for line in io.open(path, encoding='utf-8'):
        s = line.strip()
        if not s or s[0] in '#;':
            continue
        if s.startswith('[') and s.endswith(']'):
            sec = s[1:-1].lower()
            continue
        if '=' in s:
            k, v = s.split('=', 1)
            out[(sec, k.strip().lower())] = v.strip()
    return out


reference = ini_values(os.path.join(root, 'ddda_ai_overhaul.default.ini'))
pattern = re.compile(r'get(Bool|Int|UInt|Float)\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*([^)]+?)\s*\)')

calls, skipped, checked = [], 0, 0
for d, dirs, fs in os.walk(root):
    dirs[:] = [x for x in dirs
               if x not in ('.git', 'builds', 'ImGui', 'MinHook', 'resources', 'docs', 'tools')]
    for n in fs:
        if not n.endswith(('.cpp', '.h')):
            continue
        p = os.path.join(d, n)
        for i, line in enumerate(io.open(p, encoding='utf-8', errors='replace').read().split('\n')):
            for m in pattern.finditer(line):
                calls.append((m.group(1), m.group(2).lower(), m.group(3).lower(),
                              m.group(4), os.path.relpath(p, root), i + 1))

mismatch, undoc = [], set()
for kind, sec, key, fb, path, line in calls:
    if (sec, key) not in reference:
        undoc.add((sec, key))
        continue
    if (sec, key) in ALLOWED:
        skipped += 1
        continue
    ini = reference[(sec, key)]
    if kind == 'Bool':
        a, b = norm_bool(ini), norm_bool(fb)
    else:
        a, b = norm_num(ini), norm_num(fb)
    if a is None or b is None:
        continue
    checked += 1
    if a != b:
        mismatch.append((sec, key, ini, fb, path, line))

for sec, key, ini, fb, path, line in mismatch:
    print('  [%s] %s: эталон обещает %r, код по умолчанию даёт %r   (%s:%d)'
          % (sec, key, ini, fb, path, line))
if mismatch:
    print('умолчания ини: РАСХОЖДЕНИЙ %d из %d сверенных' % (len(mismatch), checked))
    sys.exit(1)
print('умолчания ини: %d сверено, %d исключено осознанно, расхождений 0'
      % (checked, skipped))
if undoc:
    print('  (к сведению: код читает %d ключей, которых нет в эталоне — это отдельная '
          'задача, PARKED §2)' % len(undoc))
PY

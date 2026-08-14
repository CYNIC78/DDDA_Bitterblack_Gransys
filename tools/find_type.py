#!/usr/bin/env python3
"""find_type.py — быстрый поиск рычагов в TypeAtlas.

Рабочий цикл проекта: «хочу вмешаться в X» -> найти тип -> найти поле -> писать.
Этот скрипт закрывает первый шаг за секунды, без запуска игры.

    python3 tools/find_type.py taunt howl          # по подстрокам имени
    python3 tools/find_type.py --em 0100 --cat taunt
    python3 tools/find_type.py --em 0100 --cat death
    python3 tools/find_type.py --size 320          # найти тип по sizeof
    python3 tools/find_type.py --re 'cThink.*Target'

Имена в MT Framework НЕ обфусцированы, поэтому поиск по смыслу работает.
"""
import re, sys, argparse

ATLAS = 'src/TypeAtlas.Generated.h'
ROW = re.compile(r'\{ "([^"]+)", (0x[0-9A-F]+), (0x[0-9A-F]+), (\d+), (\d+) \}')

CATS = [
    ('taunt',  ('Howl', 'Threat', 'PartyDance', 'Horn', 'JumpJoy', 'Appeal')),
    ('death',  ('Die', 'DeadBody', 'Dead')),
    ('damage', ('Dmg', 'Damage')),
    ('attack', ('Atck', 'Attack', 'Kick', 'Throw', 'Thrust', 'Swing', 'Assassin')),
    ('wait',   ('Wait', 'Idle')),
    ('move',   ('Walk', 'Dash', 'Run', 'Jump', 'Tumble', 'Move', 'Turn')),
    ('guard',  ('Grd', 'Guard', 'Sld')),
    ('spawn',  ('Layout', 'Lot', 'Appear', 'Popup')),
    ('think',  ('Think', 'AIPriority', 'Evaluation', 'CheckSituation')),
    ('target', ('Target', 'LockOn', 'Lockon')),
    ('motion', ('Motion',)),
]

def cat_of(name):
    for c, keys in CATS:
        if any(k in name for k in keys):
            return c
    return 'other'

def load(path=ATLAS):
    out = []
    for m in ROW.finditer(open(path, encoding='utf-8').read()):
        n, fr, vt, sz, tid = m.groups()
        out.append({'name': n, 'factoryRVA': fr, 'vtRVA': vt,
                    'size': int(sz), 'typeId': int(tid), 'cat': cat_of(n)})
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('terms', nargs='*')
    ap.add_argument('--em'); ap.add_argument('--cat')
    ap.add_argument('--size', type=int); ap.add_argument('--re')
    ap.add_argument('--atlas', default=ATLAS)
    ap.add_argument('-n', type=int, default=60)
    a = ap.parse_args()

    rows = load(a.atlas)
    if a.em:
        rows = [r for r in rows if ('cEm%s' % a.em) in r['name']]
    if a.cat:
        rows = [r for r in rows if r['cat'] == a.cat]
    if a.size:
        rows = [r for r in rows if r['size'] == a.size]
    if a.re:
        rx = re.compile(a.re, re.I)
        rows = [r for r in rows if rx.search(r['name'])]
    for t in a.terms:
        rows = [r for r in rows if t.lower() in r['name'].lower()]

    print('%-52s %-10s %-10s %7s  %s' %
          ('TYPE', 'vtRVA', 'factory', 'size', 'cat'))
    for r in rows[:a.n]:
        print('%-52s %-10s %-10s %7d  %s' %
              (r['name'], r['vtRVA'], r['factoryRVA'], r['size'], r['cat']))
    print('\n%d найдено%s' % (len(rows), '' if len(rows) <= a.n else
                              ' (показано %d)' % a.n))

if __name__ == '__main__':
    main()

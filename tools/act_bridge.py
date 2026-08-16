#!/usr/bin/env python3
"""act_bridge.py — построить мост factory-vtable -> instance-vtable для Act-классов.

Проблема: ActMap.Generated.h собран из колонки factoryVtRVA атласа. В живой
памяти объекты несут instance vtable — другой адрес. Отсюда 0 совпадений.

Идея моста: и в атласе, и в памяти Act-классы одного вида лежат ОДНОЙ решёткой
с шагом 0x48 в том же порядке. Значит достаточно одной опорной пары
(любой известный живой Act + его имя), чтобы вычислить сдвиг для всего вида:

    instance_va = atlas_rva - atlas_base + live_base

где atlas_base/live_base — адреса одного и того же класса в двух системах.

Опорные пары взяты из подтверждённых runtime-наблюдений (см. docs/SOURCE_OF_TRUTH.md):
  * тело uEm0100: atlas 0x11A0474 <-> live 0x015852A8
  * слот +0x2DC8 меняется при смерти -> это указатель на Act

Использование:
    python3 tools/act_bridge.py dump_alive.json [dump_dead.json ...]
"""
import json, re, sys, bisect, collections

ATLAS = 'src/TypeAtlas.Generated.h'
BASE  = 0x400000


def load_atlas():
    src = open(ATLAS, encoding='utf-8').read()
    rows = []
    for n, f, v, s, t in re.findall(
            r'\{ "([^"]+)", (0x[0-9A-F]+), (0x[0-9A-F]+), (\d+), (\d+) \}', src):
        rows.append((n, int(f, 16), int(v, 16), int(s), int(t)))
    return rows


def act_lattice(rows, em):
    """Все Act-классы одного вида, отсортированные по factoryVtRVA."""
    pre = 'cEm%s' % em
    sel = [r for r in rows
           if r[0].startswith(pre + 'Act') or r[0] == pre + 'Action']
    return sorted(sel, key=lambda r: r[2])


def main(*dumps):
    rows = load_atlas()
    bodies = {r[0]: r for r in rows}

    # живые тела из дампов: kind -> instance vt
    live_bodies = {}
    observed = collections.defaultdict(dict)   # kind -> {off: set(vt)}
    for path in dumps:
        d = json.load(open(path, encoding='utf-8', errors='replace'))
        for a in d.get('actors', []):
            k = a.get('kind')
            if not k or k == '?':
                continue
            live_bodies[k] = int(a['vt'], 16)
            for r in a.get('raw', []):
                off = int(r['off'], 16)
                observed[k].setdefault(off, set()).add(int(r['vt'], 16))

    print('=== тела ===')
    for k, vt in sorted(live_bodies.items()):
        atlas = bodies.get(k)
        if not atlas:
            print('  %-10s live %08X  (нет в атласе)' % (k, vt))
            continue
        delta = atlas[2] - (vt - BASE)
        print('  %-10s live %08X  atlasRVA %07X  delta %+#x' % (k, vt, atlas[2], delta))

    # слоты, которые меняли значение между дампами = кандидаты в Act
    print('\n=== слоты с меняющимся значением (кандидаты Act) ===')
    for k in sorted(observed):
        changing = {o: v for o, v in observed[k].items() if len(v) > 1}
        if not changing:
            continue
        for off, vts in sorted(changing.items()):
            print('  %-10s +%04X : %s' % (k, off, ' '.join('%08X' % x for x in sorted(vts))))

    # мост по решётке
    print('\n=== мост (по решётке 0x48) ===')
    for k in sorted(live_bodies):
        em = k[3:] if k.startswith('uEm') else None
        if not em:
            continue
        lat = act_lattice(rows, em)
        if not lat:
            continue
        body_atlas = bodies.get(k)
        if not body_atlas:
            continue
        # предполагаем: порядок и шаг совпадают, тело = якорь
        live_base = live_bodies[k]
        atlas_base = body_atlas[2]
        print('  %s: %d Act-классов в атласе, якорь atlas %07X -> live %08X'
              % (k, len(lat), atlas_base, live_base))
        for off, vts in sorted(observed[k].items()):
            for vt in sorted(vts):
                guess_rva = (vt - BASE) + (atlas_base - (live_base - BASE))
                i = bisect.bisect_left([r[2] for r in lat], guess_rva)
                for j in (i - 1, i):
                    if 0 <= j < len(lat) and abs(lat[j][2] - guess_rva) <= 0x48:
                        print('     +%04X %08X -> %-30s (%+d)'
                              % (off, vt, lat[j][0], lat[j][2] - guess_rva))
                        break


if __name__ == '__main__':
    main(*sys.argv[1:])

#!/usr/bin/env python3
"""RST (charparam stat table) dumper — DDDA / MT Framework.

Cracked 14.08.2026 on em0100/0101/0102. Validated against the community
wiki: HP 1000 / 2000 / 6000 for Goblin / Hobgoblin / Grimgoblin — exact.

  0x00  u8[4]  magic 30 09 11 20   (version stamp, identical across enemies)
  0x04  u32    recordCount (2)
  0x10  u32    variantCount (2)
  0x1C  records, stride 0x3C (60 bytes), 4 total:
          +0x00 f32  maxHP
          +0x04 f32  A   (knockdown / blow-away durability)
          +0x08 f32  B   (recovery rate, 0.3 or 1.0/1.5)
          +0x0C f32  C   (flinch durability)
          +0x10 f32  D   (recovery rate)
          +0x14 f32  E   1.0
          +0x18 f32  F   1.0 / 10.0 on rec1
          +0x1C..    ints: 1, 0, 280, <f32 75.0>, <f32 30.0>

Records 0/1 = normal difficulty pair, records 2/3 = Hard Mode pair
(em0101: 650/450 vs 500/300 — Hard Mode has the LOWER durability values).
"""
import struct, sys

# Record starts are NOT a constant stride: 0x1C, 0x54, 0x98, 0xD0
# (deltas 0x38, 0x44, 0x38). Locate them by the maxHP float instead.
OFFSETS = (0x1C, 0x54, 0x98, 0xD0)

def parse(path):
    d = open(path, 'rb').read()
    if d[:4] != bytes.fromhex('30091120'):
        raise ValueError('%s: not RST (magic %s)' % (path, d[:4].hex()))
    out = {'file': path, 'size': len(d), 'records': []}
    for k, p in enumerate(OFFSETS):
        if p + 28 > len(d):
            break
        hp, a, b, c, e, f, g = struct.unpack_from('<7f', d, p)
        out['records'].append({
            'index': k, 'at': '0x%02X' % p, 'maxHP': hp,
            'knockdownDurability': a, 'knockdownRecover': b,
            'flinchDurability': c, 'flinchRecover': e,
            'e': f, 'f': g})
    return out

def hp_signature(path):
    """20-byte little-endian scan pattern: maxHP + the 4 durability floats."""
    r = parse(path)['records'][0]
    vals = (r['maxHP'], r['knockdownDurability'], r['knockdownRecover'],
            r['flinchDurability'], r['flinchRecover'])
    return ' '.join('%02X' % c for c in b''.join(struct.pack('<f', v) for v in vals))

if __name__ == '__main__':
    for a in sys.argv[1:]:
        o = parse(a)
        print('=' * 72)
        print('%s  (%d B)' % (o['file'], o['size']))
        for r in o['records']:
            tag = 'normal' if r['index'] < 2 else 'hard  '
            print('  rec%d @%s [%s]  HP=%-8g  KD=%-7g/%-4g  flinch=%-7g/%-4g' %
                  (r['index'], r['at'], tag, r['maxHP'],
                   r['knockdownDurability'], r['knockdownRecover'],
                   r['flinchDurability'], r['flinchRecover']))
        print('  HP scan signature (20B): %s' % hp_signature(a))

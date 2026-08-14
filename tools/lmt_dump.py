#!/usr/bin/env python3
"""LMT (motion list) dumper — MT Framework / DDDA.

Cracked 14.08.2026 on the em0100 (Goblin) motion set.

  0x00  char[4]  'LMT\0'
  0x04  u16      version (66 on DDDA)
  0x06  u16      slotCount
  0x08  u32[slotCount]  motionOffset[]   0 = empty slot

  motion header @ motionOffset:
    +0x00  u32  boneTrackOffset
    +0x04  u32  boneCount     (27 for the goblin body, 37 for weapon motions)
    +0x08  u32  frameCount    <-- duration; divide by 30 for seconds
    +0x0C  s32  loopFrame     (-1 = no loop)
    +0x10..     float bounds / blend data

The slot INDEX is the motion id referenced elsewhere (e.g. the `.eap`
action table stores a motion id at record +0xC0). Slot arrays are sparse
on purpose: Capcom reserved wide id ranges per category.
"""
import struct, sys

def parse(path):
    d = open(path, 'rb').read()
    if d[:4] != b'LMT\0':
        raise ValueError('%s: not LMT' % path)
    ver, cnt = struct.unpack_from('<HH', d, 4)
    offs = struct.unpack_from('<%dI' % cnt, d, 8)
    out = {'file': path, 'version': ver, 'slotCount': cnt, 'motions': []}
    for i, o in enumerate(offs):
        if not o or o + 16 > len(d):
            continue
        _, bones, frames, loop = struct.unpack_from('<IIIi', d, o)
        out['motions'].append({'id': i, 'at': '0x%06X' % o, 'bones': bones,
                               'frames': frames, 'seconds': round(frames / 30.0, 2),
                               'loopFrame': loop})
    return out

if __name__ == '__main__':
    for a in sys.argv[1:]:
        o = parse(a)
        print('=' * 72)
        print('%s  ver=%d  %d/%d слотов занято' %
              (o['file'], o['version'], len(o['motions']), o['slotCount']))
        for mo in o['motions']:
            loop = '' if mo['loopFrame'] < 0 else '  loop@%d' % mo['loopFrame']
            print('  id=%-4d %s  %4d кадров  %5.2f с  bones=%d%s' %
                  (mo['id'], mo['at'], mo['frames'], mo['seconds'],
                   mo['bones'], loop))

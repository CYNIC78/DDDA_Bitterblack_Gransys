#!/usr/bin/env python3
"""sn2_dump.py — декодер сенсоров SNR2 (зрение/слух врага).

Формат: 'SNR2', u16 ver(18), u16 count, заголовок 0x10,
далее count записей по 0x50 байт:
    +0x00 u32 type      1=зрение(конус), 2=ближняя/круговая, 3=слух/присутствие
    +0x18 f32 радиус 1  основная дальность
    +0x1C f32 радиус 2  вторичная дальность
    +0x30 f32 угол      в градусах (60 = конус зрения, 360 = круг)

Проверено: em0100A vs em0101A отличаются одним полем (150 -> 170),
что подтверждает разметку.
"""
import struct, sys

def dump(path):
    d = open(path, 'rb').read()
    if d[:4] != b'SNR2':
        print('%s: не SNR2' % path); return
    ver, n = struct.unpack_from('<HH', d, 4)
    print('%s  ver=%d записей=%d' % (path, ver, n))
    print('  #  тип   радиус1   радиус2   угол   трактовка')
    kind = {1: 'ЗРЕНИЕ (конус)', 2: 'ближняя/круговая', 3: 'слух/присутствие'}
    for i in range(n):
        o = 0x10 + i * 0x50
        if o + 0x50 > len(d): break
        t = struct.unpack_from('<I', d, o)[0]
        r1 = struct.unpack_from('<f', d, o + 0x18)[0]
        r2 = struct.unpack_from('<f', d, o + 0x1C)[0]
        ang = struct.unpack_from('<f', d, o + 0x30)[0]
        print('  %d  %4d  %8.0f  %8.0f  %5.0f   %s'
              % (i, t, r1, r2, ang, kind.get(t, '?')))

if __name__ == '__main__':
    for p in sys.argv[1:]:
        dump(p)

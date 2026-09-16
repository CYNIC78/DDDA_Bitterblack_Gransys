#!/usr/bin/env python3
# stq_dump.py — дамп таблицы STRQ (.stq), формат муз.очереди DDDA.
#
# Раскладка вскрыта якорным методом (docs/AUDIO_MUSIC_RECON.md, R0):
#   header: "STRQ" | ver(0x1B) | count | ? | 16b reserved | tbl2ofs | ...
#   main table @0x3C, записи по 24 байта:
#     +0x00 u32 ofs имени (абсолютный, в строковом блобе; без расширения)
#     +0x04 u32 размер файла в байтах
#     +0x08 u32 всего сэмплов (длительность = /48000)
#     +0x0C u32 каналы (1/2/6)
#     +0x10 u32 луп-старт, сэмплы (-1 = одноразовый)
#     +0x14 u32 луп-энд,   сэмплы (-1 = одноразовый)
#
# Использование:
#   python3 tools/stq_dump.py <file.stq> [--md]
import struct, sys

VER_EXPECT = 0x1B
FMT_HZ = 48000

def parse(path):
    d = open(path, 'rb').read()
    if d[:4] != b'STRQ':
        sys.exit(f"{path}: не STRQ (magic={d[:4]!r})")
    ver, cnt = struct.unpack_from('<II', d, 0x04)
    if ver != VER_EXPECT:
        print(f"! версия {ver} != {VER_EXPECT}", file=sys.stderr)
    rows = []
    for i in range(cnt):
        b = 0x3C + i * 24
        np, size, samples, ch, f10, f14 = struct.unpack_from('<6I', d, b)
        end = d.index(b'\x00', np)
        name = d[np:end].decode('ascii', 'replace')
        rows.append(dict(i=i, name=name, size=size, samples=samples,
                         ch=ch, loopIn=None if f10 == 0xFFFFFFFF else f10,
                         loopOut=None if f14 == 0xFFFFFFFF else f14,
                         dur=samples / FMT_HZ))
    return rows

def main():
    rows = parse(sys.argv[1])
    md = '--md' in sys.argv
    if md:
        print("| # | path | size | dur | ch | loop in→out |")
        print("|--:|---|---:|---:|--:|---|")
        for r in rows:
            lp = "—" if r['loopIn'] is None else \
                 f"{r['loopIn']/FMT_HZ:.3f} → {r['loopOut']/FMT_HZ:.3f}"
            print(f"| {r['i']} | `{r['name']}` | {r['size']:,} | "
                  f"{r['dur']:.1f}s | {r['ch']} | {lp} |")
    else:
        for r in rows:
            lp = "oneshot" if r['loopIn'] is None else \
                 f"loop {r['loopIn']/FMT_HZ:7.3f}s → {r['loopOut']/FMT_HZ:7.3f}s"
            print(f"[{r['i']:>3}] {r['name']:34} {r['size']:>10,} B "
                  f"{r['dur']:7.1f}s {r['ch']}ch {lp}")

if __name__ == '__main__':
    main()

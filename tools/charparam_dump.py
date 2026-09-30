#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Дамп боевого блока карточки врага из данных игры (resources/extracted_assets).

Зачем. Мы трижды подряд спорили, «какая база настоящая»: движок держит боевые поля
объекта между перезапусками DLL, а код умеет молча «восстанавливать ваниль» делением.
Файл игры — единственный непредвзятый источник: если прочитанное из памяти не
совпадает с файлом, значит прочитали не ваниль.

Примеры:
    python3 tools/charparam_dump.py em0100          # гоблин
    python3 tools/charparam_dump.py em0101 em0102   # хоб и «третий» из той же папки
    python3 tools/charparam_dump.py --search 126.2  # есть ли такое число в данных вообще
"""
import os
import struct
import sys
import zipfile

ZIP = os.path.join('resources', 'extracted_assets', 'em', 'enemies.zip')
# Смещения боевого блока внутри *_cmn.prp. Подтверждено полем 85.57: в памяти
# гоблина лежали ровно эти числа (250 / 75 / 80 / 75), а игра берёт их отсюда.
STAT_OFFSETS = [('atk', 0x0C64), ('def', 0x0C6C), ('matk', 0x0C74), ('mdef', 0x0C7C)]


def read_block(zf, path):
    data = zf.read(path)
    out = {}
    for name, off in STAT_OFFSETS:
        out[name] = struct.unpack_from('<f', data, off)[0] if off + 4 <= len(data) else None
    return out, data


def main(argv):
    if not os.path.exists(ZIP):
        print('нет файла', ZIP)
        return 2
    zf = zipfile.ZipFile(ZIP)

    if len(argv) >= 2 and argv[0] == '--search':
        target = float(argv[1])
        needle = struct.pack('<f', target)
        found = 0
        for n in zf.namelist():
            if not n.endswith(('.prp', '.rst')):
                continue
            d = zf.read(n)
            if needle in d:
                print(f'  найдено в {n} на 0x{d.find(needle):04X}')
                found += 1
        print('итог: файлов с таким числом —', found)
        return 0

    want = argv or ['em0100']
    for em in want:
        # Карточка вида лежит НЕ обязательно в своей папке: хобгоблин (em0101)
        # лежит внутри em0100. Поэтому ищем файл по имени во всём архиве.
        cand = [n for n in zf.namelist() if n.endswith(f'{em}_cmn.prp')]
        if not cand:
            print(f'{em}: карточки {em}_cmn.prp в данных не найдено')
            continue
        for n in sorted(cand):
            blk, data = read_block(zf, n)
            line = ' '.join(f'{k} {v:.1f}' if v is not None else f'{k} ?'
                            for k, v in blk.items())
            print(f'{n}  ({len(data)} байт)  ->  {line}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

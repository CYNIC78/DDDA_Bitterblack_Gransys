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


CODEGEN_HEAD = """// EnemyFileBase.h — АВТОГЕНЕРАЦИЯ, не править руками.
//
// Источник: resources/extracted_assets/em/enemies.zip, файлы charparam/em/*_cmn.prp
// (боевой блок по смещениям 0x0C64 atk / 0x0C6C def / 0x0C74 matk / 0x0C7C mdef).
// Все файлы одного формата и размера (3742 байта), смещения проверены полем:
// в логе 85.57 из памяти гоблина прочитаны ровно числа файла 250/75/80/75.
//
// Зачем таблица в коде. Старый код УГАДЫВАЛ базу («значение выше порога — значит
// наша прошлая запись, делим»): порог абсолютный, числа видов различаются в сорок
// раз, поэтому однажды уже изменённое значение было поделено второй раз и вид
// получил вдвое слабые статы (85.58). Файл игры — непредвзятый источник, гадать
// больше не о чем.
//
// Виды с суффиксом (em5101_00 и т. п.) в таблицу НЕ входят: наша строка вида
// разбирается по цифрам, и вариант столкнулся бы с базовым видом. Для таких
// остаётся прежний путь (таблица видов из памяти, затем оценка).
//
// Перегенерация:  python3 tools/charparam_dump.py --codegen > src/runtime/EnemyFileBase.h
"""
CODEGEN_TAIL = """
static const int kSpeciesFileBaseCount =
    (int)(sizeof(kSpeciesFileBase) / sizeof(kSpeciesFileBase[0]));

static const SpeciesFileBase* FindSpeciesFileBase(unsigned short emId)
{
    for (int i = 0; i < kSpeciesFileBaseCount; ++i)
        if (kSpeciesFileBase[i].emId == emId) return &kSpeciesFileBase[i];
    return 0;
}

// Годится ли файловая база в дело.
//
// Отсев нужен ровно для двух случаев, и оба видны глазами: нулевые поля
// (em1200/em1201/em6003 — это не бойцы) и «иммунные» круглые тысячи
// (em5100 mdef 10000, em5101 def 10000/mdef 20000 — это маркеры, а не статы).
// Потолок 9000 выбран так, чтобы НЕ задеть настоящие боевые виды: у em7001
// атака 8500 — реальная, и она должна попадать в работу. Прежний путь
// (таблица видов из памяти, затем оценка) остаётся для отсеянных.
static bool SpeciesFileBaseSane(const SpeciesFileBase* b)
{
    if (!b) return false;
    if (b->atk <= 0.0f || b->defC <= 0.0f || b->mAtk <= 0.0f || b->mDefC <= 0.0f) return false;
    if (b->atk > 9000.0f || b->defC > 9000.0f || b->mAtk > 9000.0f || b->mDefC > 9000.0f) return false;
    return true;
}
"""


def codegen():
    zf = zipfile.ZipFile(ZIP)
    rows = []
    seen = set()
    for n in sorted(zf.namelist()):
        if not n.endswith('_cmn.prp'):
            continue
        em = n.split('/')[-1].replace('_cmn.prp', '')
        if '_' in em:
            continue
        eid = int(em[2:])
        # Одна и та же карточка лежит в нескольких папках (например em5401_cmn.prp
        # есть и в em5401/, и в em5402/). Имя файла — ключ, значения совпадают,
        # поэтому повтор пропускаем, а не спорим с ним.
        if eid in seen:
            continue
        seen.add(eid)
        d = zf.read(n)
        vals = [struct.unpack_from('<f', d, off)[0] for off in (0x0C64, 0x0C6C, 0x0C74, 0x0C7C)]
        rows.append((eid, em, vals))
    print(CODEGEN_HEAD)
    print('struct SpeciesFileBase { unsigned short emId; float atk, defC, mAtk, mDefC; };')
    print('')
    print('static const SpeciesFileBase kSpeciesFileBase[] = {')
    for eid, em, v in rows:
        print(f'    {{ 0x{eid:04X}, {v[0]:7.1f}f, {v[1]:7.1f}f, {v[2]:7.1f}f, {v[3]:7.1f}f }},  // {em}')
    print('};')
    print(CODEGEN_TAIL, end='')


def main(argv):
    if not os.path.exists(ZIP):
        print('нет файла', ZIP)
        return 2
    zf = zipfile.ZipFile(ZIP)

    if argv and argv[0] == '--codegen':
        codegen()
        return 0

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

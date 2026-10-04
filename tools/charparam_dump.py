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

// ── 85.64: ПОЛИТИКА ПО ПОЛЯМ, А НЕ ОТСЕВ ВИДА ────────────────────────────────
//
// Было (85.60): вид с любым «небоевым» полем выбрасывался целиком и уходил на
// прежний путь (таблица видов из памяти, затем оценка). Перепись видов
// (docs/SPECIES_CENSUS.md) показала, кого именно это выбросило: Голем (em5100),
// Металлический голем (em5101) и Death (em6003). Это не «не бойцы»: у Death в
// файле НОЛЬ в атаке (у него нет обычной атаки), а 10000/20000 у големов стоят в
// ЗАЩИТЕ и означают иммунитет (обычный голем не берётся магией, металлический —
// физикой).
//
// Стало: вид принимается, если карточка вообще похожа на бойца, а режим решается
// ПО КАЖДОМУ ПОЛЮ:
//
//   write  — обычное боевое число: умножаем на множители вида и пишем (как было);
//   immune — маркер иммунитета (>= 9000): НЕ пишем и НЕ умножаем. Это свойство
//            вида, а не стат: умножить 10000 на ×3 значит сломать иммунитет;
//   absent — ноль: у вида нет такого поля (Death без атаки). Тоже не пишем:
//            ноль в атаке — не «слабый удар», а «нет удара», и подставлять туда
//            оценку значит выдумывать механику, которой у босса нет.
//
// Смысл правки: файл остаётся единственным источником базы и для этих трёх
// боссов. Старый путь «память -> оценка» теперь только для видов, которых в
// таблице нет вовсе (нет карточки или нет класса в exe).
enum SpeciesFieldMode {
    kSpeciesWrite  = 0,   // обычное боевое число
    kSpeciesImmune = 1,   // маркер иммунитета (9000+)
    kSpeciesAbsent = 2    // ноль: поля у вида нет
};

static SpeciesFieldMode SpeciesFieldModeOf(float v)
{
    if (!(v > 0.0f)) return kSpeciesAbsent;
    if (v >= 9000.0f) return kSpeciesImmune;
    return kSpeciesWrite;
}

static const char* SpeciesFieldModeName(SpeciesFieldMode m)
{
    return m == kSpeciesImmune ? "immune" : (m == kSpeciesAbsent ? "absent" : "write");
}

// Годится ли карточка в дело: все четыре поля конечны и не выходят далеко за
// пределы боевых (50000 — тот же потолок, что у клампа записи), и хотя бы одно
// поле обычное. Иначе это не карточка бойца, и работает прежний путь.
static bool SpeciesFileBaseUsable(const SpeciesFileBase* b)
{
    if (!b) return false;
    const float v[4] = { b->atk, b->defC, b->mAtk, b->mDefC };
    int usable = 0;
    for (int i = 0; i < 4; ++i) {
        if (!(v[i] == v[i])) return false;          // NaN
        if (v[i] < 0.0f || v[i] > 50000.0f) return false;
        if (SpeciesFieldModeOf(v[i]) == kSpeciesWrite) ++usable;
    }
    return usable > 0;
}

// Политика вида одной строкой для лога: `policy=mdef:immune`, у Death —
// `policy=atk:absent matk:absent`. Если все четыре поля обычные, строки нет:
// в логе ничего не меняется для 87 видов из 92.
static void SpeciesPolicyText(const SpeciesFileBase* b, char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (!b) return;
    const float v[4]  = { b->atk, b->defC, b->mAtk, b->mDefC };
    const char* nm[4] = { "atk", "def", "matk", "mdef" };
    int n = 0;
    // Без sprintf_s и вообще без библиотек: заголовок подключается и фикстурой,
    // у которой нет ни windows.h, ни stdio. Строки короткие, копируем вручную.
    for (int i = 0; i < 4; ++i) {
        const SpeciesFieldMode m = SpeciesFieldModeOf(v[i]);
        if (m == kSpeciesWrite) continue;
        const char* parts[4] = { n ? " " : "", nm[i], ":", SpeciesFieldModeName(m) };
        for (int p = 0; p < 4; ++p)
            for (const char* c = parts[p]; *c && n < cap - 1; ++c) out[n++] = *c;
        out[n] = 0;
    }
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

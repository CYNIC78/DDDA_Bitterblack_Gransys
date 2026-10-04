#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bestiary_index.py — указатель «бестиарий мода» одним документом.

ЗАЧЕМ. Знание о видах лежало в четырёх местах: карточки архива (перепись),
доказательства опознания (охота), карта архивов и таблица баз в коде. Чтобы понять
«что это за монстр и что мод с ним делает», приходилось открывать их по очереди.
Здесь — одна страница, собранная СКРИПТОМ из тех же источников, что и код:

  * карточки  — `resources/extracted_assets/em/enemies.zip` (4 боевых поля);
  * имена     — `src/BestiaryData.h`, `resources/fluffy_em.txt`, `docs/ARC_MAP.txt`
                (тот же сбор имён, что у переписи: `tools/species_census.py`);
  * что тюним — `src/runtime/EnemyFileBase.h` (есть ли вид в файл-базе),
                `src/runtime/WorldScan.cpp` (живность/детали),
                классы без em-номера (`uEm5500B/C` → тюнинг невозможен).

HP в этот указатель НЕ берётся: карточка его не содержит (HP лежит в `emXXXX.rst`,
см. docs/SPECIES_HUNT.md). Указатель честно печатает только то, что видит.

Запуск:  python3 tools/bestiary_index.py --md docs/BESTIARY.md
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import species_census as census          # noqa: E402  (переиспользуем сбор карточек и имён)


def file_base_ids():
    """Виды, у которых есть строка в сгенерированной таблице баз."""
    path = os.path.join(ROOT, 'src', 'runtime', 'EnemyFileBase.h')
    txt = io.open(path, encoding='utf-8', errors='replace').read()
    out = {}
    # Строка таблицы: { 0x0064, 250.0f, ... },  // em0100 — номер вида стоит
    # в КОММЕНТАРИИ справа (так генератор печатает и hex, и человекочитаемый em).
    for line in txt.split('\n'):
        m = re.search(r'//\s*(em\d{4})\s*$', line)
        if m and re.match(r'\s*\{\s*0x', line):
            out[m.group(1)] = True
    return out


def world_lists():
    """Живность и детали — из одного списка продукта (WorldScan.cpp)."""
    path = os.path.join(ROOT, 'src', 'runtime', 'WorldScan.cpp')
    txt = io.open(path, encoding='utf-8', errors='replace').read()
    def block(name):
        m = re.search(name + r'\[\]\s*=\s*\{(.*?)\};', txt, re.S)
        return re.findall(r'"(uEm\d{4})"', m.group(1)) if m else []
    # В списках лежат имена классов (uEm8200) — приводим к номерам вида (em8200).
    harmless = ['em' + x[3:] for x in block('kHarmlessKinds')]
    structural = ['em' + x[3:] for x in block('kStructuralKinds')]
    return harmless, structural


def tuning_verdict(em, harmless, structural, base, vals):
    if em in structural:
        return 'НЕ трогаем (деталь составного врага или предмет окружения)'
    if em in harmless:
        return 'живность: угроза выключена, масштаб сохранён'
    if em not in base:
        return 'вне файл-базы (аварийные пути, подписаны в логе)'
    modes = []
    if any(v <= 0.0 for v in vals.values()):
        modes.append('поле-ноль не пишем')
    if any(v >= 9000.0 for v in vals.values()):
        modes.append('иммунитет не перемножаем')
    return 'база из ФАЙЛА' + ('; ' + ', '.join(modes) if modes else '')


def build():
    rows = census.table_rows()
    base = file_base_ids()
    harmless, structural = world_lists()
    by_em = {r['em']: r for r in rows}

    out = []
    out.append('# BESTIARY — бестиарий мода одним указателем (генерируется)')
    out.append('')
    out.append('> Автогенерация: `python3 tools/bestiary_index.py --md docs/BESTIARY.md`.')
    out.append('> Собирается из тех же источников, что и код: карточки архива, словари имён,')
    out.append('> таблица баз `src/runtime/EnemyFileBase.h` и списки видов в `WorldScan.cpp`.')
    out.append('> Руками не править.')
    out.append('')
    out.append('## Как читать')
    out.append('')
    out.append('**Карточка** — четыре боевых числа из `emXXXX_cmn.prp`: atk / def / маг.атака /')
    out.append('маг.защита (смещения 0x0C64/0x6C/0x74/0x7C). HP здесь нет: он лежит отдельно,')
    out.append('в `emXXXX.rst` (float по 0x1C) — см. `docs/SPECIES_HUNT.md`.')
    out.append('')
    out.append('**Отпечаток вида** — пятёрка HP + эти четыре числа. Именно так опознаются арки,')
    out.append('у которых нет имени в словарях: совпадение всех пяти с таблицей существ =')
    out.append('доказательство. Полные таблицы — `docs/SPECIES_HUNT.md` и `docs/SPECIES_CENSUS.md`.')
    out.append('')
    out.append('**Что тюним** — три исхода: `база из ФАЙЛА` (вид в таблице баз, ступени и множители')
    out.append('применяются), `живность` (угроза выключена, масштаб сохранён), `НЕ трогаем`')
    out.append('(деталь составного врага или предмет окружения).')
    out.append('')
    out.append('## Что за чем смотреть')
    out.append('')
    out.append('| Вопрос | Документ |')
    out.append('|---|---|')
    out.append('| Кто это за номером `emNNNN` и что у него в карточке | этот файл, `docs/SPECIES_CENSUS.md` |')
    out.append('| Почему вид назван именно так (доказательства) | `docs/SPECIES_HUNT.md` |')
    out.append('| Как устроен архив вида (`emXXXX.arc`) | `docs/ARC_MAP.txt` |')
    out.append('| Тело гоблина по косточкам (офсеты, действия) | `docs/ANATOMY_EM0100.md` |')
    out.append('| Офсеты и поля живого тела | `docs/FIELD_MAP.md` |')
    out.append('')
    out.append('## Все виды с карточкой в архиве')
    out.append('')
    out.append('| em | Имя | Семейство | gid | atk | def | маг.атк | маг.зщт | Что тюним |')
    out.append('|---|---|---|---:|---:|---:|---:|---:|---|')
    for r in rows:
        gid = ('0x%02X' % r['gid']) if r.get('gid') else '—'
        v = r['vals']
        out.append('| `%s` | %s | %s | %s | %g | %g | %g | %g | %s |' % (
            r['em'], r['name'] or '_нет имени_', r['fam'] or '—', gid,
            v['atk'], v['def'], v['matk'], v['mdef'],
            tuning_verdict(r['em'], harmless, structural, base, v)))
    out.append('')
    missing = [e for e in sorted(base) if e not in by_em]
    if missing:
        out.append('Виды в таблице баз, но без карточки в архиве: %s.'
                   % ', '.join('`%s`' % e for e in missing))
        out.append('')
    out.append('## Классы без em-номера (тюнинг невозможен по построению)')
    out.append('')
    out.append('Имя класса манитера и его семьи кончается буквой (`uEm5500C`, `uEm5500B`),')
    out.append('поэтому номер вида из имени не читается: нет ни ini-секции, ни строки в')
    out.append('таблице баз, ни ступени, ни размера. Тюнер выходит и **подписывает пропуск**')
    out.append('в логе один раз на вид — так «не видели» отличается от «не тронули».')
    out.append('')
    out.append('* `uEm5500C` — Манитер (строка бестиария 67 «Maneaters», `gid 0xAF`,')
    out.append('  карточка `em5503_00` = 15000/3800/500/2000/700). Включить тюнинг = строка-алиас')
    out.append('  в `EmIdFromKind` + строка в `EnemyFileBase.h` — решение владельца.')
    out.append('* `uEm5500B` — вторая голова семьи (gid 0x41).')
    out.append('')
    out.append('## Честные «не опознан»')
    out.append('')
    out.append('Файлами эти тела не опознаются: отпечаток не сходится ни с одной строкой таблиц')
    out.append('существ. Нужен взгляд на модель (ARCtool) или на поведение в игре:')
    out.append('')
    out.append('* `em8500` — мелкая фауна (карточка 1/1/1/1);')
    out.append('* `em8600`, `em8602` — летающая мелочь (действия `ActFly*`; у 8602 своей карточки нет);')
    out.append('* `em8601` — мелочь без анимационных классов;')
    out.append('* `em9000` — 1 HP, `ActShell`/`ActWait`, сенсор тот же, что у червя (`em9000A.sn2`);')
    out.append('* `em9807` — пустышка: arc есть, внутри при распаковке пусто.')
    return '\n'.join(out).rstrip() + '\n', len(rows)


def main(argv):
    text, n = build()
    if len(argv) >= 2 and argv[0] == '--md':
        io.open(argv[1], 'w', encoding='utf-8').write(text)
        print('записано %s | видов: %d' % (argv[1], n))
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Перепись видов: что за монстр за каждым emID и что у него в карточке игры.

ЗАЧЕМ. Таблица баз (src/runtime/EnemyFileBase.h) рождается из карточек игры, а
работает с ними по ПОЛИТИКЕ ПОЛЕЙ (85.64): значение 0 — поле не пишем, значение
>= 9000 — иммунитет, поле не перемножаем, остальное пишем как обычно. Раньше (до
85.64) правило было грубее: вид с нулём или с «круглой тысячей» выбрасывался из
файл-базы целиком. Какие именно виды так терялись и что это за виды — видно не
было. Здесь каждый emID сводится с ИМЕНЕМ из наших же словарей
(src/BestiaryData.h, resources/fluffy_em.txt, docs/ARC_MAP.txt) и с карточкой
(enemies.zip::charparam/em/*_cmn.prp), чтобы каждое решение было видно в лицо.

--check сверяет не «список выброшенных», а СООТВЕТСТВИЕ: значения карточек,
политику в СГЕНЕРИРОВАННОМ заголовке и этот документ. Отдельно видно виды с
особой политикой (Голем, Металлический голем, Death, два не-бойца) и вид,
у которого карточка есть только в вариантной форме (em5503_00).

Поля карточки — ровно те, что берёт боевой код: atk 0x0C64, def 0x0C6C,
matk 0x0C74, mdef 0x0C7C (шаг 8 байт, между значениями нулевая прокладка).
HP в этом файле нет: она лежит отдельно, поэтому в переписи её нет.

Запуск:  python3 tools/species_census.py            # таблица в stdout
         python3 tools/species_census.py --md docs/SPECIES_CENSUS.md
"""
import os
import re
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZIP = os.path.join(ROOT, 'resources', 'extracted_assets', 'em', 'enemies.zip')
BESTIARY = os.path.join(ROOT, 'src', 'BestiaryData.h')
FLUFFY = os.path.join(ROOT, 'resources', 'fluffy_em.txt')
ARC = os.path.join(ROOT, 'docs', 'ARC_MAP.txt')
FILEBASE = os.path.join(ROOT, 'src', 'runtime', 'EnemyFileBase.h')

OFF = [('atk', 0x0C64), ('def', 0x0C6C), ('matk', 0x0C74), ('mdef', 0x0C7C)]
SANITY_LO, SANITY_HI = 0.0, 9000.0   # те же границы, что в SpeciesFieldModeOf (85.64)


def load_names():
    """Имена и семьи из наших словарей: бестиарий -> флаффи -> карта архивов."""
    names, fams, src = {}, {}, {}
    if os.path.exists(BESTIARY):
        text = open(BESTIARY, encoding='utf-8').read()
        for m in re.finditer(r'\{\s*-?\d+,\s*0x[0-9A-Fa-f]+,\s*-?\d+,\s*"([^"]+)",'
                             r'\s*"([^"]+)",\s*"(uEm\w+)"', text):
            nm, fam, uem = m.group(1), m.group(2), m.group(3)
            em = 'em' + uem[3:]
            names.setdefault(em, nm)
            fams.setdefault(em, fam)
            src.setdefault(em, 'BestiaryData.h')
    if os.path.exists(FLUFFY):
        for ln in open(FLUFFY, encoding='utf-8'):
            m = re.match(r'\s*(em\d{4}(?:_\d+)?)\s*-\s*(.+?)\s*$', ln)
            if m and m.group(1) not in names:
                names[m.group(1)] = m.group(2)
                src[m.group(1)] = 'fluffy_em.txt'
    if os.path.exists(ARC):
        for ln in open(ARC, encoding='utf-8'):
            m = re.match(r'[^\w]*\s*(em\d{4}(?:_\d+)?)\.arc\s*#\s*(.+?)\s*$', ln)
            if m:
                em, nm = m.group(1), m.group(2).split('(')[0].strip()
                if nm and (em not in names or '?' in names[em] or 'Unknown' in names[em]):
                    names[em] = nm
                    src[em] = 'ARC_MAP.txt'
    return names, fams, src


def load_gids():
    """emXXXX -> groupId из resources/types.tsv (класс uEmXXXX).

    Нужен, чтобы отделить «вид существует в exe» от «карточка лежит в данных».
    Если класса нет вовсе, тело такого вида в логе не появится никогда, и вся
    возня с его базой — теория.
    """
    gids = {}
    tsv = os.path.join(ROOT, 'resources', 'types.tsv')
    if not os.path.exists(tsv):
        return gids
    for ln in open(tsv, encoding='utf-8', errors='ignore'):
        parts = ln.rstrip('\n').split('\t')
        if len(parts) < 6:
            continue
        name = parts[2] if len(parts) > 2 else ''
        m = re.match(r'uEm(\d{4})', name)
        if m:
            try:
                gids.setdefault('em' + m.group(1), int(parts[5]))
            except ValueError:
                pass
    return gids


def cards():
    """Все карточки архива: emId -> (em, значения, путь). Один emId — одна карточка."""
    zf = zipfile.ZipFile(ZIP)
    out = {}
    for n in sorted(zf.namelist()):
        if n.endswith('/'):
            continue
        m = re.match(r'(em\d{4})(?:_\d+)?_cmn\.prp$', os.path.basename(n))
        if not m:
            continue
        em, eid = m.group(1), int(m.group(1)[2:], 16)
        d = zf.read(n)
        vals = {name: struct.unpack_from('<f', d, off)[0] for name, off in OFF}
        out.setdefault(eid, (em, vals, n))
    return out


def verdict(v):
    """Что тюнер делает с полями вида.

    Границы — те же, что в политике 85.64 (SpeciesFieldModeOf в EnemyFileBase.h):
        значение = 0        -> поле НЕ ПИШЕТСЯ (absent);
        значение >= 9000    -> поле ИММУННОЕ (immune), не перемножается;
        иначе               -> пишется как обычно.
    Слово «отсеян» здесь больше не уместно: вид с такими полями остаётся в таблице
    и работает, меняется только обращение с его полями. Раньше вид с нулём или
    маркером выбрасывался целиком — это правило отменено в 85.64."""
    zeros = [k for k, x in v.items() if x <= SANITY_LO]
    marks = [k for k, x in v.items() if x >= 9000.0]
    if zeros and marks:
        return 'ПОЛИТИКА: не пишем (%s), иммунитет (%s)' % (','.join(zeros), ','.join(marks))
    if zeros:
        return 'ПОЛИТИКА: не пишем (%s) — не боевые поля' % ','.join(zeros)
    if marks:
        return 'ПОЛИТИКА: иммунитет (%s)' % ','.join(marks)
    return 'в работе'


def no_card_list(rows):
    """Виды, которые есть в наших словарях/на диске, но карточки в архиве нет."""
    have = {r['em'] for r in rows}
    listed = []
    if os.path.exists(ARC):
        for ln in open(ARC, encoding='utf-8'):
            m = re.match(r'[^\w]*\s*(em\d{4}(?:_\d+)?)\.arc', ln)
            if m:
                em = m.group(1)
                if em not in have and re.match(r'em\d{4}$', em):
                    listed.append(em)
    return sorted(set(listed))


def table_rows():
    names, fams, src = load_names()
    gids = load_gids()
    data = cards()
    rows = []
    for eid in sorted(data):
        em, v, path = data[eid]
        rows.append(dict(em=em, eid=eid, vals=v, path=path,
                         name=names.get(em, ''), fam=fams.get(em, ''),
                         src=src.get(em, ''), verdict=verdict(v),
                         gid=gids.get(em)))
    return rows


def suffixed_cards():
    """Карточки с суффиксом варианта (em5101_00 и подобные) — почему не виды."""
    zf = zipfile.ZipFile(ZIP)
    out = {}
    for n in sorted(zf.namelist()):
        m = re.match(r'(em\d{4})_(\d+)_cmn\.prp$', os.path.basename(n))
        if m:
            out.setdefault(m.group(1), set()).add(os.path.basename(n))
    return out


def as_markdown(rows):
    out = ['# Перепись видов: имя, карточка, вердикт', '',
           'Автогенерация: `python3 tools/species_census.py --md docs/SPECIES_CENSUS.md`.',
           '',
           'Числа — из карточек игры (`resources/extracted_assets/em/enemies.zip`,',
           '`charparam/em/*_cmn.prp`, поля 0x0C64/0x0C6C/0x0C74/0x0C7C — те же, что берёт боевой код).',
           'Имена — из наших словарей: `src/BestiaryData.h` (72 вида), `resources/fluffy_em.txt`',
           '(снимок FluffyQuack), `docs/ARC_MAP.txt` (карта архивов). Колонка «источник» говорит,',
           'откуда именно взялось имя (одно и то же имя из двух словарей — приоритет у бестиария).',
           '',
           '«Вердикт» — что тюнер делает с ПОЛЯМИ вида (политика 85.64, `SpeciesFieldModeOf`',
           'в `src/runtime/EnemyFileBase.h`): значение 0 — поле не пишем (в карточке это «нет такого',
           'поля», а не «ноль урона»); значение >= 9000 — иммунитет, поле не перемножаем; остальное',
           'пишется как обычно. Вид при этом остаётся в работе: правило «вид с нулём или с маркером',
           'выбрасываем целиком» отменено в 85.64.',
           '',
           'Проверка: `python3 tools/species_census.py --check` — сверяет политику в',
           'сгенерированном заголовке с карточками и с этим документом (шаг гейта 2i6/10).',
           '',
           '| emID | имя | семейство | класс в exe | atk | def | matk | mdef | вердикт | источник имени |',
           '|---|---|---|---|---:|---:|---:|---:|---|---|']
    for r in rows:
        out.append('| %s | %s | %s | %s | %.0f | %.0f | %.0f | %.0f | %s | %s |' % (
            r['em'], r['name'] or '—', r['fam'] or '—',
            ('uEm%s (gid %d)' % (r['em'][2:], r['gid'])) if r['gid'] is not None
            else 'НЕТ КЛАССА',
            r['vals']['atk'], r['vals']['def'], r['vals']['matk'], r['vals']['mdef'],
            r['verdict'], r['src'] or '—'))
    # ── группы ─────────────────────────────────────────────────────────────
    work = [r for r in rows if r['verdict'] == 'в работе']
    zeros = [r for r in rows if r['verdict'].startswith('ПОЛИТИКА: не пишем')]
    marks = [r for r in rows if r['verdict'].startswith('ПОЛИТИКА: иммунитет')]

    out += ['', '## Группы', '',
            '* **в работе** — %d видов: все четыре поля в границах, поля пишутся как обычно, '
            'файл-база умножается на ступень ранга;' % len(work),
            '* **не пишем** — %d: %s. Нули в карточке — не «ноль урона», а «нет такого поля»: '
            'у Death нет обычной атаки, у em1200/em1201 нет боевых полей вовсе. Тюнер такие '
            'поля не пишет — что было в теле, то и остаётся;'
            % (len(zeros), ', '.join(r['em'] for r in zeros)),
            '* **иммунитет** — %d: %s. Круглые тысячи у Голема и Металлического голема — это '
            'маркер неуязвимости в самой игре, а не «очень много защиты»: перемножать их нельзя, '
            'иначе иммунитет станет бесконечностью. Тюнер оставляет такие поля как есть.'
            % (len(marks), ', '.join(r['em'] for r in marks)),
            '',
            '### Виды без карточки', '']
    nocard = no_card_list(rows)
    if nocard:
        out.append('Есть в `docs/ARC_MAP.txt`, но `*_cmn.prp` в архиве нет — в таблицу баз такие '
                   'виды не попадут, для них работает прежний путь (таблица видов из памяти, '
                   'затем оценка):')
        out.append('')
        out.append('`' + '`, `'.join(nocard) + '`')
    else:
        out.append('Нет.')
    out += ['', '### Карточки с суффиксом варианта — в таблицу НЕ берутся', '']
    suf = suffixed_cards()
    out.append('Список: ' + ', '.join('`%s` (%s)' % (em, ', '.join(sorted(v)))
                                      for em, v in sorted(suf.items())) + '.')
    out.append('')
    out.append('Причина: ключ вида в логе разбирается как `uEm<цифры>`, и суффиксная карточка '
               'может принадлежать под-телу составного врага (диск Металлического голема, '
               'части Химеры и Гидры) или неиспользуемому варианту: у `em5503_00` нет и класса '
               'в exe (`uEm5503` в `types.tsv` отсутствует). Если такой вид однажды появится '
               'в поле, вернёмся к этому списку.')
    out.append('')
    out += ['### Виды без имени в наших словарях', '']
    unnamed = [r for r in rows if not r['name']]
    if unnamed:
        out.append('Имя не найдено ни в `src/BestiaryData.h`, ни в `resources/fluffy_em.txt`, '
                   'ни в `docs/ARC_MAP.txt`. Это не ошибка переписи: у части из них и класса '
                   'в exe нет, а у остальных номер отсутствует и в публичном маппинге '
                   '`groupId -> имя` (72 записи). Имена можно добыть из игровых текстов — '
                   'отдельная задача, на бой не влияет:')
        out.append('')
        for r in unnamed:
            out.append('* `%s` — %s, карточка `%s`' % (
                r['em'],
                ('uEm%s (gid %d)' % (r['em'][2:], r['gid'])) if r['gid'] is not None
                else 'класса в exe нет',
                r['path']))
    else:
        out.append('Нет.')
    out.append('')
    out += ANALYSIS
    return '\n'.join(out) + '\n'


ANALYSIS = [
    '', '## Что это значит', '',
    '1. **Нули — это НЕ живность.** У всей живности карточки с настоящими, хоть и крошечными',
    '   числами: олень `1/1/1/1`, заяц `1/1/1/1`, кабан `120/20/1/10`, лагерная мелочь',
    '   `10/10/10/10`. Ноль стоит ровно у трёх видов: `em1200` и `em1201` (gid 55/56, имени нет',
    '   ни в одном словаре; у обоих def/mdef 75 при нулевых атаке и магии — похоже на не-бойца,',
    '   пугало/мишень) и `em6003` = **Death** (босс Bitterblack: `0/666/0/666`). У Death нет',
    '   обычной атаки — он убивает механикой, поэтому в карточке ноль, а не «мало».',
    '2. **«Огромный урон» — это не 5100.** У `em5100` (Golem) `10000` стоит в *магической',
    '   защите*, у `em5101` (Metal Golem) — `10000` в физической и `20000` в магической: это',
    '   маркеры иммунитета (обычный голем не берётся магией, металлический — физикой), а не',
    '   статы. Атака у них обычная боссовая: 950 и 1100.',
    '3. **Настоящий гигант по урону — `em7001` Awakened Daimon (форма 2): 8500 атаки, 5500',
    '   магии.** Он в таблице и работает. Следом: `em5906` Cursed Dragon 5600, `em5903`',
    '   Firedrake 4750, `em7000` Daimon 4600, `em0204` Garm 3900.',
    '4. **Голем, Металлический голем и Death раньше выбрасывались целиком** — так работало',
    '   правило «вид либо в файл-базе, либо нет»: нули и маркеры резали вид. В 85.64 это правило',
    '   заменено пофайловой политикой: иммунные поля (>= 9000) не перемножаются, нулевые (= 0) не',
    '   пишутся, остальные поля вида берутся из файла как обычно. Все три босса теперь идут по',
    '   файл-базе, как и все прочие.',
    '5. **Живность в фильтрах врагов — исправлено в 85.64.** `KindIsEnemy` знал только два имени',
    '   (лагерная мелочь `uEm8000` и заяц `uEm8600`), поэтому олень, лань, змея, мышь/ворона и',
    '   кабан проходили как враги: они попадали в тактические счётчики, в дистанции рывка пешек,',
    '   в агрессию и в допуск пачек. Теперь список мирных видов явный (`WorldScan.cpp`',
    '   `kHarmlessKinds`), а масштаб живности при этом сохраняется намеренно: существо — да,',
    '   угроза — нет.',
    '6. **Части составных врагов — исправлено в 85.64.** `em8100` (ambient prop),',
    '   `em8200`/`em8201` (части драконов), `em8300` (тело Проклятого дракона) и `em7002`',
    '   (драконья голова на груди Даймона) лежат в таблице как обычные виды, но собственными',
    '   телами не являются: теперь они в списке `kStructuralKinds` — их не тюнят, не считают',
    '   угрозой и не берут в допуск пачек. Один список на весь продукт: `Runtime::KindIsStructural`.',
    '',
    '## Что осталось из предложений переписи', '',
    '* **Имена для оставшихся 8 видов** (`em0104`, `em0405`…`em0408`, `em1200`, `em1201`,',
    '  `em5503`, `em9000`): достать из игровых текстов. На бой не влияет — только на читаемость',
    '  лога. Отдельная задача, в 85.64 не входила.',
]


# Виды с ОСОБОЙ ПОЛИТИКОЙ ПОЛЕЙ (85.64). Список — не «так надо», а факт,
# записанный в docs/SPECIES_CENSUS.md: у Death нет обычной атаки (нули), у двух
# не-бойцов em1200/em1201 нет боевых полей вовсе, «круглые тысячи» у Голема
# (иммунитет к магии) и Металлического голема (иммунитет к физике).
#
# ВАЖНО: это НЕ отсев. До 85.64 такие виды выбрасывались из файл-базы целиком;
# теперь они в таблице и работают, а меняется только обращение с их полями —
# поэтому проверка сверяет не «список выброшенных», а СООТВЕТСТВИЕ значений
# карточек политике из EnemyFileBase.h.
EXPECTED_SPECIAL = ['em1200', 'em1201', 'em5100', 'em5101', 'em6003']
# Виды, у которых карточка есть ТОЛЬКО в вариантной форме (em5503_00): по
# устоявшемуся правилу такие в таблицу баз не берутся (см. раздел документа
# «Карточки с суффиксом варианта»). Сейчас такой один — Cursed Dragon em5503,
# тот самый, у которого нет и класса в exe. Если список изменится (кто-то
# потеряет базовую карточку или, наоборот, получит её), проверка скажет.
EXPECTED_VARIANT_ONLY = ['em5503']
EXPECTED_TABLE = 91   # строк в kSpeciesFileBase


import os as _os   # имя файла карточки — в проверке (вариантные карточки)


def _policy_modes(vals):
    """Что сделает тюнер с полями вида: та же арифметика, что SpeciesFieldModeOf."""
    out = {}
    for k, x in vals.items():
        if x <= SANITY_LO:      out[k] = 'absent'
        elif x >= 9000.0:       out[k] = 'immune'
        else:                   out[k] = 'write'
    return out


def _header_modes():
    """Поля видов ИЗ СГЕНЕРИРОВАННОГО заголовка (а не из карточек): то, что
    реально уедет в сборку. Значащие нули после запятой читаются как есть."""
    import re as _re
    text = open(FILEBASE, encoding='utf-8').read()
    rows = {}
    for m in _re.finditer(r'^\s*\{ 0x([0-9A-F]{4}),\s*'
                          r'([-0-9.eE+]+)f,\s*([-0-9.eE+]+)f,\s*'
                          r'([-0-9.eE+]+)f,\s*([-0-9.eE+]+)f \},', text, _re.M):
        eid, vals = int(m.group(1), 16), [float(m.group(i)) for i in range(2, 6)]
        rows[eid] = _policy_modes(dict(zip(('atk', 'def', 'matk', 'mdef'), vals)))
    return rows


def check():
    rows = table_rows()
    special = sorted(r['em'] for r in rows if r['verdict'].startswith('ПОЛИТИКА'))
    if special != EXPECTED_SPECIAL:
        print('  виды с особой политикой изменились: было %s, стало %s'
              % (EXPECTED_SPECIAL, special))
        print('  -> обнови docs/SPECIES_CENSUS.md и EXPECTED_SPECIAL в tools/species_census.py')
        return 1

    modes = _header_modes()
    if len(modes) != EXPECTED_TABLE:
        print('  в таблице баз %d видов, ожидалось %d' % (len(modes), EXPECTED_TABLE))
        return 1

    # Главная проверка 85.64: значения в СГЕНЕРИРОВАННОМ заголовке дают ровно ту
    # политику, которую обещает документ. Так ловится и «перегенерация стёрла
    # политику», и «карточка поехала», и «генератор поменял границы».
    bad, detail, variant_only = [], [], []
    for r in rows:
        # НОМЕРА ВИДОВ — ДЕСЯТИЧНЫЕ: em5100 это 5100, а в таблице он лежит как
        # 0x13EC (5100 в шестнадцатеричном виде). int(...,16) здесь дал бы 20736.
        eid = int(r['em'][2:])
        got = modes.get(eid)
        if got is None:
            # Нет строки — законно только для вариантной карточки (em5503_00):
            # такие виды в таблицу не берутся по правилу. Всё прочее — ошибка.
            if re.match(r'em\d{4}_\d+_cmn\.prp$', _os.path.basename(r['path'])):
                variant_only.append(r['em'])
            else:
                bad.append('%s: нет строки в %s' % (r['em'], FILEBASE))
            continue
        want = _policy_modes(r['vals'])
        if got != want:
            bad.append('%s: политика в заголовке %s, а по карточке %s'
                       % (r['em'], got, want))
        if r['em'] in EXPECTED_SPECIAL:
            detail.append('%s %s' % (r['em'], ','.join(
                '%s:%s' % (k, v) for k, v in sorted(got.items()) if v != 'write') or '-'))
    for b in bad:
        print(' ', b)
    if bad:
        print('  -> пересобери заголовок: python3 tools/charparam_dump.py --codegen')
        return 1

    if sorted(variant_only) != EXPECTED_VARIANT_ONLY:
        print('  виды только с вариантной карточкой изменились: было %s, стало %s'
              % (EXPECTED_VARIANT_ONLY, sorted(variant_only)))
        return 1

    names = sum(1 for r in rows if r['name'])
    print('  cards=%d in-table=%d special=%s variant-only=%s named=%d'
          % (len(rows), len(modes), ','.join(special),
             ','.join(variant_only), names))
    print('  policy matches cards: %s' % '; '.join(detail))
    return 0


def main(argv):
    if argv and argv[0] == '--check':
        return check()
    rows = table_rows()
    if len(argv) >= 2 and argv[0] == '--md':
        text = as_markdown(rows)
        open(argv[1], 'w', encoding='utf-8').write(text)
        print('записано', argv[1], '| видов с карточкой:', len(rows))
        return 0
    for r in rows:
        print('%-8s %-22s %6.0f %6.0f %6.0f %6.0f  %s' % (
            r['em'], (r['name'] or '—')[:22],
            r['vals']['atk'], r['vals']['def'], r['vals']['matk'], r['vals']['mdef'],
            r['verdict']))
    print('итого видов с карточкой:', len(rows))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

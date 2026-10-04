#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""doc_check.py — «доки ↔ код»: то, что проверяет гейт, не гниёт.

ЗАЧЕМ. Аудит 30.09 нашёл главную болезнь документации проекта: два файла, объявленные
источником истины (`docs/SOURCE_OF_TRUTH.md`) и точкой входа (`PROJECT_HUB.md`), отстали
на 44–54 билда. Это не лень, а отсутствие контура: канон проверялся только глазами, а
глаза в смене заняты кодом. Здесь тот же приём, что и у кода: правило, которое нельзя
забыть, должно проверяться машиной.

Что проверяем (все пункты — «правило проекта», а не вкусовщина):

  1. `PROJECT_HUB.md` §1 и `README.md` называют ТЕКУЩИЙ тег из `src/BuildTag.h`
     (правило владельца: README и хаб отслеживают текущий билд).
  2. `SOURCE_OF_TRUTH.md` называет текущий тег и содержит строки ключевых контрактов
     нынешнего продукта: файл-база, ранги, наборы, пакет лога, списки видов. Если
     контракт вырезали из канона — сборка встанет до того, как это увидит владелец.
  3. Каждый ключ из рабочих ини есть в `docs/INI_REFERENCE.md` (а он генерируется из
     комментариев ини — значит, ключ без описания становится виден сразу).
  4. Каждый `.cpp` из `src/` есть в `docs/CODE_MAP.md`.
  5. Каждый документ из `docs/` либо упомянут в `PROJECT_HUB.md`/`README.md`, либо
     является стабом-редиректом (короткая шапка со ссылкой на `archive/`).
  6. Все markdown-ссылки в доках и корневых файлах разрешаются в существующие файлы.

Запуск:  python3 tools/doc_check.py         # отчёт, код возврата 1 при нарушении
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, 'docs')

# Контракты, которые обязаны быть в каноне (строка → почему).
REQUIRED_IN_SOT = [
    ('EnemyFileBase', 'база боевых статов вида берётся из файлов игры (85.60)'),
    ('[ranks]', 'живые веса ступеней (85.56)'),
    ('[packs]', 'наборы пачек по месту (85.57)'),
    ('LogMem', 'контракт журнала и полевого пакета (85.63)'),
    ('KindIsHarmless', 'списки видов: живность и детали (85.64/85.65)'),
]

INI_FILES = ['ddda_entities.ini', 'ddda_ai_overhaul.ini',
             'ddda_ai_overhaul.default.ini', 'ddda_pawn_ai_profiles.ini']


def read(path):
    return io.open(path, encoding='utf-8', errors='replace').read()


def ini_keys(path):
    out, sec = set(), ''
    if not os.path.exists(path):
        return out
    for line in read(path).split('\n'):
        s = line.strip()
        if not s or s.startswith(('#', ';')):
            continue
        if s.startswith('[') and s.endswith(']'):
            sec = s[1:-1].lower()
            continue
        if '=' in s:
            out.add((sec, s.split('=', 1)[0].strip()))
    return out


def main():
    bad = []
    tag_m = re.search(r'#define\s+MOD_BUILD_TAG\s+"([^"]+)"', read(os.path.join(ROOT, 'src/BuildTag.h')))
    tag = tag_m.group(1) if tag_m else ''

    # 1. Текущий тег в хабе и README
    hub = read(os.path.join(ROOT, 'PROJECT_HUB.md'))
    readme = read(os.path.join(ROOT, 'README.md'))
    if tag not in hub:
        bad.append('PROJECT_HUB.md не называет текущий тег %s (правило: хаб отслеживает билд)' % tag)
    if tag not in readme:
        bad.append('README.md не называет текущий тег %s' % tag)

    # 2. Канон: тег + ключевые контракты
    sot = read(os.path.join(DOCS, 'SOURCE_OF_TRUTH.md'))
    if tag not in sot:
        bad.append('SOURCE_OF_TRUTH.md не называет текущий тег %s' % tag)
    for needle, why in REQUIRED_IN_SOT:
        if needle not in sot:
            bad.append('SOURCE_OF_TRUTH.md: нет контракта %r (%s)' % (needle, why))

    # 3. Ключи ини → справочник
    ref = read(os.path.join(DOCS, 'INI_REFERENCE.md'))
    missing = []
    for fn in INI_FILES:
        for sec, key in sorted(ini_keys(os.path.join(ROOT, fn))):
            if ('`%s`' % key) not in ref:
                missing.append('%s:[%s] %s' % (fn, sec, key))
    if missing:
        bad.append('INI_REFERENCE.md: нет %d ключей (примеры: %s) — перегенерировать '
                   'python3 tools/ini_reference.py --md docs/INI_REFERENCE.md'
                   % (len(missing), ', '.join(missing[:4])))

    # 4. Файлы src/*.cpp → карта кода
    cmap = read(os.path.join(DOCS, 'CODE_MAP.md'))
    src_cpp = []
    for root, _d, names in os.walk(os.path.join(ROOT, 'src')):
        for n in names:
            if n.endswith('.cpp'):
                rel = os.path.relpath(os.path.join(root, n), os.path.join(ROOT, 'src')).replace('\\', '/')
                src_cpp.append(rel)
    not_in_map = [r for r in src_cpp if ('`%s`' % r) not in cmap]
    if not_in_map:
        bad.append('CODE_MAP.md: нет %d файлов (примеры: %s) — перегенерировать '
                   'python3 tools/code_map.py --md docs/CODE_MAP.md'
                   % (len(not_in_map), ', '.join(sorted(not_in_map)[:4])))

    # 5. Каждый док — либо в оглавлении, либо стаб-редирект
    index = hub + readme
    orphans = []
    for n in sorted(os.listdir(DOCS)):
        if not n.endswith('.md'):
            continue
        text = read(os.path.join(DOCS, n))
        if n in index:
            continue
        if len(text.split('\n')) <= 8 and 'archive/' in text:
            continue          # стаб-редирект: так и задумано
        if n.startswith(('generated/', 'changelog/')):
            continue
        orphans.append(n)
    if orphans:
        bad.append('доков нет в оглавлении (PROJECT_HUB/README) и это не редирект: %s'
                   % ', '.join(orphans[:6]))

    # 6. Ссылки разрешаются
    link_re = re.compile(r'\[([^\]]+)\]\(([^)\s]+)\)')
    broken = []
    targets = [os.path.join(DOCS, n) for n in os.listdir(DOCS) if n.endswith(('.md', '.txt'))]
    targets += [os.path.join(ROOT, 'README.md'), os.path.join(ROOT, 'PROJECT_HUB.md'),
                os.path.join(ROOT, 'CHANGELOG.md')]
    for path in targets:
        if not os.path.exists(path):
            continue
        base = os.path.dirname(path)
        for m in link_re.finditer(read(path)):
            tgt = m.group(2)
            if tgt.startswith(('http://', 'https://', '#', 'mailto:')):
                continue
            rel = tgt.split('#')[0]
            if not rel:
                continue
            if not os.path.exists(os.path.normpath(os.path.join(base, rel))):
                broken.append('%s -> %s' % (os.path.relpath(path, ROOT), tgt))
    if broken:
        bad.append('битые ссылки (%d): %s' % (len(broken), '; '.join(broken[:5])))

    for b in bad:
        print(' ', b)
    if bad:
        print('docs: НАРУШЕНИЙ %d' % len(bad))
        return 1
    print('docs: канон на теге %s, ключи ини %d, файлы кода %d, ссылки целы'
          % (tag, sum(len(ini_keys(os.path.join(ROOT, f))) for f in INI_FILES), len(src_cpp)))
    return 0


if __name__ == '__main__':
    sys.exit(main())

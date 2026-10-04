#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ini_reference.py — справочник по всем ключам ини, СОБРАННЫЙ ИЗ САМИХ ИНИ.

ЗАЧЕМ. В проекте три рабочих ини-файла и 273 ключа; из них больше сотни не были
описаны ни в одном доку. Ключ, который никто не помнит, — это мина: он либо
не работает, либо работает не так, как думает владелец. Руками такой справочник
не поддерживать (он устареет на первом же билде), поэтому он ГЕНЕРИРУЕТСЯ:
описания берутся из комментариев самого ини (там они есть и пишутся по-русски),
живость секции — из таблицы LIVE ниже, значения — из файла.

Живость. ЖИВОЕ = перечитывается на ходу (сторож mtime): `ddda_entities.ini`
целиком, плюс секции `[ranks]`, `[packs]`, `[log]` в `ddda_ai_overhaul.ini`.
Всё остальное читается при загрузке DLL.

Запуск:  python3 tools/ini_reference.py --md docs/INI_REFERENCE.md
Проверка: шаг гейта 10f/10 требует, чтобы КАЖДЫЙ ключ из ини был в справочнике.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FILES = [
    ('ddda_entities.ini', 'боевые статы, восприятие, размеры (LIVE, 500 мс)'),
    ('ddda_ai_overhaul.ini', 'главный конфиг модуля'),
    ('ddda_ai_overhaul.default.ini', 'тот же главный конфиг в поставке (в зипе)'),
    ('ddda_pawn_ai_profiles.ini', 'профили приоритетов пешек (прибор, не применяется автоматически)'),
]

# Секция → живость. Ключ ищется по имени секции в нижнем регистре.
LIVE_SECTIONS = {
    'ranks': 'живой (85.56)',
    'packs': 'живой (85.57)',
    'log': 'живой (план LOG_DIET, ещё не построен)',
}


def parse(path):
    """Возвращает список секций: (имя, [(ключ, значение, описание)]) в порядке файла."""
    if not os.path.exists(path):
        return None
    sections = []
    cur = None
    pending = []          # комментарии над ключом
    for raw in io.open(path, encoding='utf-8', errors='replace'):
        line = raw.rstrip('\n')
        s = line.strip()
        if not s:
            pending = []
            continue
        if s.startswith('[') and s.endswith(']'):
            cur = [s[1:-1], []]
            sections.append(cur)
            pending = []
            continue
        if s.startswith('#') or s.startswith(';'):
            txt = s.lstrip('#;').strip()
            if txt:
                pending.append(txt)
            continue
        if '=' in s and cur is not None:
            k, v = s.split('=', 1)
            note = ' '.join(pending)
            note = re.sub(r'\s+', ' ', note)
            cur[1].append((k.strip(), v.strip(), note))
            pending = []
    return sections


def liveness(file_name, sec_name):
    if file_name == 'ddda_entities.ini':
        return 'живой (500 мс)'
    if sec_name.lower() in LIVE_SECTIONS:
        return LIVE_SECTIONS[sec_name.lower()]
    return 'на загрузке'


def short(text, cap=150):
    if len(text) <= cap:
        return text
    cut = text[:cap]
    if ' ' in cut:
        cut = cut[:cut.rfind(' ')]
    return cut + '…'


def build():
    out = []
    out.append('# INI_REFERENCE — все ключи ини (генерируется)')
    out.append('')
    out.append('> Автогенерация: `python3 tools/ini_reference.py --md docs/INI_REFERENCE.md`.')
    out.append('> Руками не править: правка теряется при следующей генерации. Описания берутся')
    out.append('> из комментариев самих ини — то есть из единственного места, где они гарантированно')
    out.append('> рядом с ключом. Шаг гейта `10f/10` следит, чтобы ни один ключ не пропал')
    out.append('> из справочника (значит, и из комментариев).')
    out.append('')
    out.append('**Живой** = перечитывается на ходу (сторож времени файла). Остальное читается')
    out.append('при загрузке DLL. Правило целиком — `docs/INI_CHEATSHEET.md`.')
    out.append('')
    total = 0
    tmap = []
    for fn, what in FILES:
        secs = parse(os.path.join(ROOT, fn))
        if secs is None:
            continue
        n = sum(len(k) for _, k in secs)
        total += n
        tmap.append((fn, what, n, len(secs)))
    out.append('## Карта файлов')
    out.append('')
    out.append('| Файл | Что это | Ключей | Секций |')
    out.append('|---|---|---:|---:|')
    for fn, what, n, ns in tmap:
        out.append('| `%s` | %s | %d | %d |' % (fn, what, n, ns))
    out.append('| | **всего** | **%d** | |' % total)
    out.append('')
    for fn, what in FILES:
        secs = parse(os.path.join(ROOT, fn))
        if secs is None:
            continue
        out.append('---')
        out.append('')
        out.append('## `%s`' % fn)
        out.append('')
        out.append('%s. Ключей: %d.' % (what, sum(len(k) for _, k in secs)))
        out.append('')
        for name, keys in secs:
            out.append('### `[%s]` — %s' % (name, liveness(fn, name)))
            out.append('')
            if not keys:
                out.append('_(ключей нет — секция-заглушка)_')
                out.append('')
                continue
            out.append('| Ключ | Значение в файле | Что делает (из комментария рядом) |')
            out.append('|---|---|---|')
            for k, v, note in keys:
                out.append('| `%s` | `%s` | %s |' % (k, v, short(note) if note else '—'))
            out.append('')
    return '\n'.join(out).rstrip() + '\n', total


def main(argv):
    text, total = build()
    if len(argv) >= 2 and argv[0] == '--md':
        io.open(argv[1], 'w', encoding='utf-8').write(text)
        print('записано %s | ключей: %d' % (argv[1], total))
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

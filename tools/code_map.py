#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""code_map.py — карта кода, СОБРАННАЯ ИЗ ГЛАВ КОММЕНТАРИЕВ САМИХ ФАЙЛОВ.

ЗАЧЕМ. В `src/` больше сотни файлов, а в архитектурном доке упоминались семь.
Первый день нового человека (или мой собственный через месяц) начинается с
грепа, а не с карты. При этом в проекте есть правило: каждый файл начинается
шапкой «зачем он существует». Значит, карту не надо писать руками — её надо
СОБРАТЬ из этих шапок и пересобирать тем же скриптом. Тогда она не устареет.

Что попадает в карту:
  * роль файла  — первый абзац шапки (до пустой строки или до 400 символов);
  * кто включает — сколько .cpp/.h в проекте делают `#include "имя"` (цена связи);
  * размер       — строк (грубая оценка веса).

Запуск:  python3 tools/code_map.py --md docs/CODE_MAP.md
Проверка: шаг гейта 10f/10 требует, чтобы каждый .cpp из src/ был в карте.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')

GROUPS = [
    ('runtime', 'Продуктовый слой (`src/runtime/`) — работает всегда, без DevTools'),
    ('monsterai', 'Сторона монстров (`src/monsterai/`) — директор, карточки видов, приборы'),
    ('pawnai', 'Сторона пешек (`src/pawnai/`) — доктрины, ускорение, Possession'),
    ('devtools', 'Исследование (`src/devtools/`) — не существует в релизном поведении'),
    ('tcomp-root', 'Корень `src/` — точки входа, UI, шины'),
]


def files():
    out = []
    for root, _dirs, names in os.walk(SRC):
        for n in sorted(names):
            if not n.endswith(('.cpp', '.h')):
                continue
            if n.endswith('.Generated.h'):
                continue
            rel = os.path.relpath(os.path.join(root, n), SRC).replace('\\', '/')
            out.append(rel)
    return sorted(out)


def purpose(path):
    """Первый абзац шапки файла: строки `//`/`/* */` в начале, до первой пустой."""
    try:
        lines = io.open(path, encoding='utf-8', errors='replace').read().split('\n')
    except OSError:
        return ''
    got = []
    started = False
    # Если шапки в самом начале нет (файл начинается с #include), ищем первый
    # комментарий в первых 40 строках — правило проекта «файл объясняет себя»
    # действует и там, просто чуть ниже.
    # Блок считается шапкой, только если это >= 3 строк комментария подряд:
    # одиночные пометки в теле (например, у константы) шапкой не считаем —
    # иначе карта начинает врать чужим текстом.
    if not any(l.strip().startswith(('//', '/*')) for l in lines[:6]):
        run = 0
        for i, l in enumerate(lines[:45]):
            if l.strip().startswith(('//', '/*')):
                run += 1
                if run >= 3:
                    lines = lines[i - run + 1:]
                    break
            else:
                run = 0
    for ln in lines[:80]:
        s = ln.strip()
        if not started:
            if s.startswith(('//', '/*', '*', '#')) or s == '':
                if s.startswith(('//', '/*', '*')):
                    started = True
                    body = s.lstrip('/').lstrip('*').strip()
                    if body:
                        got.append(body)
                continue
            break
        else:
            if s.startswith(('*/',)):
                break
            body = s.lstrip('/').lstrip('*').strip()
            if not body:
                break
            got.append(body)
            if sum(len(x) for x in got) > 400:
                break
    text = ' '.join(got)
    text = re.sub(r'\s+', ' ', text).strip()
    # Убрать служебные первые слова вроде "Файл: ..." — оставляем как есть.
    return text


def inbound_index():
    idx = {}
    for rel in files():
        path = os.path.join(SRC, rel)
        txt = io.open(path, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'#include\s+"([^"]+)"', txt):
            inc = m.group(1).replace('\\', '/')
            base = inc.split('/')[-1]
            idx[base] = idx.get(base, 0) + 1
    return idx


def group_of(rel):
    if '/' not in rel:
        return 'tcomp-root'
    top = rel.split('/')[0]
    for key, _ in GROUPS:
        if key == top:
            return key
    return 'tcomp-root'


def build():
    inbound = inbound_index()
    out = ['# CODE_MAP — кто за что отвечает (генерируется)', '',
           '> Автогенерация: `python3 tools/code_map.py --md docs/CODE_MAP.md`.',
           '> Роль файла берётся из его собственной шапки — то есть карта не может разойтись',
           '> с кодом: она и есть комментарии кода, собранные в одно место. Шаг гейта `10f/10`',
           '> требует, чтобы каждый `.cpp` из `src/` был в этой карте.',
           '',
           '**Колонка «включают»** — сколько файлов проекта делают `#include` на этот файл.',
           'Большое число означает «менять осторожно»: правка задевает много мест.',
           '']
    allrel = files()
    for key, title in GROUPS:
        rels = [r for r in allrel if group_of(r) == key]
        if not rels:
            continue
        out.append('## %s' % title)
        out.append('')
        out.append('| Файл | Строк | Включают | Роль (из шапки файла) |')
        out.append('|---|---:|---:|---|')
        for rel in rels:
            path = os.path.join(SRC, rel)
            n = len(io.open(path, encoding='utf-8', errors='replace').read().split('\n'))
            base = rel.split('/')[-1]
            out.append('| `%s` | %d | %d | %s |' % (rel, n, inbound.get(base, 0),
                                                   purpose(path) or '—'))
        out.append('')
    nope = [r for r in allrel if not purpose(os.path.join(SRC, r))]
    out.append('---')
    out.append('')
    out.append('## Файлы без шапки (кандидаты на комментарий)')
    out.append('')
    if nope:
        out.append('Правило проекта: файл объясняет себя первой строкой. Эти %d — исключение;'
                   % len(nope))
        out.append('роль в карте стоит «—», то есть узнать её можно только по коду:')
        out.append('')
        for rel in nope:
            out.append('* `%s`' % rel)
    else:
        out.append('Нет — каждый файл объясняет себя.')
    out.append('')
    out.append('Файлов в карте: %d (без `*.Generated.h`: те генерируются и описаны в шапках).'
               % len(allrel))
    return '\n'.join(out).rstrip() + '\n', allrel


def main(argv):
    text, rels = build()
    if len(argv) >= 2 and argv[0] == '--md':
        io.open(argv[1], 'w', encoding='utf-8').write(text)
        print('записано %s | файлов: %d' % (argv[1], len(rels)))
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

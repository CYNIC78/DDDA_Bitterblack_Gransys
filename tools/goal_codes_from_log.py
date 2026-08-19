#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Собрать таблицу «код приоритета -> цель» из лога кнопки `Dump planner GOALS`.

ЗАЧЕМ ИНСТРУМЕНТ, А НЕ РУЧНАЯ ТАБЛИЦА. Набор целей у пешки НЕ фиксирован:
в дампе Страйдера слоты 4, 66, 68 пусты, а `WpnBowAtk2` вообще пришла из
дополнения (путь `tu2\\...`). Значит пешка другой вокации даст другой
набор, и таблицу придётся пересобирать. Руками — значит с ошибками.

Скрипт умеет читать оба формата лога:
  старый (Build 73.26):  `    +0x158  AI\\Goap\\Cmc\\DashFollow`
  новый  (Build 73.27):  `    code  84  slot +0x158  DashFollow`

Формула:  code = (slotOffset - 8) / 4

ПРОВЕРКИ, КОТОРЫЕ СКРИПТ ДЕЛАЕТ САМ (предел инструмента обязан быть виден
в его выводе — правило, выученное на `walk finished, 192 nodes`):
  * смещения кратны 4 и начинаются с 0x08;
  * старший код совпадает с ёмкостью массива PlanCtrl (91 слот);
  * известные коды-якоря совпадают с именами.
Любое расхождение печатается как FAIL, а не замалчивается.

Запуск:
    python3 tools/goal_codes_from_log.py ddda_ai_overhaul.log
    python3 tools/goal_codes_from_log.py log.txt -o docs/generated/PAWN_GOAL_CODES.md
"""

import argparse
import re
import sys

SLOT_BASE = 0x08          # первый слот массива ресурсов целей
SLOT_STEP = 4
PLANNER_BYTES = 25264     # sizeof cAIGoalPlanning (TypeAtlas)
PLANCTRL_BASE = 0x190
PLANCTRL_STRIDE = 0x110

# Якоря: коды, подтверждённые независимо от этой таблицы, в прошлых сборках.
ANCHORS = {
    1:  ("Follow",       "наблюдали живьём, Build 40"),
    15: ("Air",          "kGuardianModifiers, CONFIRMED"),
    54: ("WpnDaggerAtk", "главный рычаг Guardian, CONFIRMED"),
    60: ("Em0600Cover",  "kGuardianModifiers, CONFIRMED"),
}

# ХВОСТ СТРОКИ НЕ ЯКОРИМ. Первая версия кончалась на `$`, и строки с
# пометкой `<<< DASH` просто не совпадали — таблица молча потеряла ровно
# те две цели, ради которых её и писали. Поймал перекрёстный счёт ниже.
RE_OLD = re.compile(r'^\s*\+0x([0-9a-fA-F]+)\s+(\S+)')
RE_NEW = re.compile(r'^\s*code\s+(\d+)\s+slot\s+\+0x([0-9a-fA-F]+)\s+(\S+)')


def tail(path):
    """Хвост пути ресурса: AI\\Goap\\Cmc\\DashFollow -> DashFollow."""
    return re.split(r'[\\/]', path)[-1]


RE_TOTAL = re.compile(r'total goal resources:\s*(\d+),\s*dash-related:\s*(\d+)')


def parse(lines):
    """Вернуть {code: (name, fullpath, slot)}, замечания и итоги самой игры."""
    goals, notes = {}, []
    stated = None
    inside = False
    for raw in lines:
        line = raw.rstrip('\n')
        if 'loaded goals of planner' in line:
            inside = True
            goals, notes = {}, []       # берём ПОСЛЕДНИЙ дамп в логе
            continue
        if not inside:
            continue
        m = RE_TOTAL.search(line)
        if m:
            stated = (int(m.group(1)), int(m.group(2)))
            inside = False
            continue

        m = RE_NEW.match(line)
        if m:
            code, slot, name = int(m.group(1)), int(m.group(2), 16), m.group(3)
            declared = (slot - SLOT_BASE) // SLOT_STEP
            if declared != code:
                notes.append('FAIL: строка "%s" — код %d, а слот даёт %d'
                             % (line.strip(), code, declared))
            goals[code] = (tail(name), name, slot)
            continue

        m = RE_OLD.match(line)
        if m:
            slot, path = int(m.group(1), 16), m.group(2)
            if slot < SLOT_BASE or (slot - SLOT_BASE) % SLOT_STEP:
                notes.append('FAIL: смещение +0x%X не лежит в сетке слотов' % slot)
                continue
            code = (slot - SLOT_BASE) // SLOT_STEP
            goals[code] = (tail(path), path, slot)

    # ПЕРЕКРЁСТНЫЙ СЧЁТ. Игра сама печатает, сколько целей она насчитала.
    # Если парсер получил другое число — он потерял строки, и вся таблица
    # ниже недостоверна. Молчать об этом нельзя.
    if stated is not None:
        n, ndash = stated
        if len(goals) != n:
            notes.append('FAIL: игра насчитала %d целей, распознано %d '
                         '— парсер теряет строки' % (n, len(goals)))
        got_dash = sum(1 for v in goals.values() if 'Dash' in v[0])
        if got_dash != ndash:
            notes.append('FAIL: игра насчитала %d целей с рывком, распознано %d'
                         % (ndash, got_dash))
    else:
        notes.append('WARN: в логе нет строки "total goal resources" '
                     '— перекрёстный счёт невозможен')
    return goals, notes


def audit(goals):
    """Самопроверка таблицы. Возвращает (строки отчёта, всё_ли_сошлось)."""
    out, ok = [], True
    if not goals:
        return ['FAIL: в логе не найден блок "loaded goals of planner"'], False

    slots = (PLANNER_BYTES - PLANCTRL_BASE) // PLANCTRL_STRIDE
    top = max(goals)
    out.append('целей загружено: %d, старший код: %d' % (len(goals), top))
    out.append('массив PlanCtrl: (%d - 0x%X) / 0x%X = %d слотов, коды 0..%d'
               % (PLANNER_BYTES, PLANCTRL_BASE, PLANCTRL_STRIDE, slots, slots - 1))
    if top == slots - 1:
        out.append('OK: старший код совпал с ёмкостью массива PlanCtrl')
    else:
        out.append('FAIL: старший код %d, а слотов %d — раскладку надо пересмотреть'
                   % (top, slots - 1))
        ok = False

    for code, (want, why) in sorted(ANCHORS.items()):
        got = goals.get(code, (None,))[0]
        if got == want:
            out.append('OK: код %d = %s (%s)' % (code, want, why))
        elif got is None:
            out.append('WARN: код %d пуст в этом наборе, якорь %s не проверен'
                       % (code, want))
        else:
            out.append('FAIL: код %d ожидался %s, а в логе %s' % (code, want, got))
            ok = False

    gaps = [c for c in range(top + 1) if c not in goals]
    out.append('пустых слотов в диапазоне 0..%d: %d%s'
               % (top, len(gaps),
                  (' -> ' + ', '.join(str(g) for g in gaps)) if gaps else ''))
    return out, ok


def render(goals, report):
    L = ['# Коды приоритета пешки: слот ресурса цели = код',
         '',
         'Сгенерировано `tools/goal_codes_from_log.py` из лога кнопки',
         '`Dump planner GOALS`. Руками не править.',
         '',
         '```',
         'code = (slotOffset - 8) / 4',
         '```',
         '',
         '## Самопроверка',
         '']
    L += ['* ' + r for r in report]
    L += ['', '## Таблица', '',
          '| код | слот | цель | пометка |',
          '|----:|------|------|---------|']
    for code in sorted(goals):
        name, full, slot = goals[code]
        mark = ''
        if code in ANCHORS and ANCHORS[code][0] == name:
            mark = 'подтверждён независимо: ' + ANCHORS[code][1]
        elif 'Dash' in name:
            mark = '**рывок**'
        elif full.lower().startswith('tu2'):
            mark = 'из дополнения (`%s`)' % full
        L.append('| %d | `+0x%03X` | `%s` | %s |' % (code, slot, name, mark))
    L.append('')
    return '\n'.join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('-o', '--out', default='docs/generated/PAWN_GOAL_CODES.md')
    a = ap.parse_args()

    with open(a.log, encoding='utf-8', errors='replace') as f:
        goals, notes = parse(f.readlines())

    report, ok = audit(goals)
    report = notes + report
    for line in report:
        print(line)

    with open(a.out, 'w', encoding='utf-8') as f:
        f.write(render(goals, report))
    print('-> ' + a.out)
    return 0 if ok and not notes else 1


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""analyze_goblin_roles.py — разбор гоблиньей пачки по логу мода.

ЗАЧЕМ. Поймать вожака (короносца) и назвать его числа. Прямых глаз у нас
нет, но у лога есть всё, чтобы связать смерть с телом: строка DEATH (85.39),
а для старых сборок — уход тела с предсмертным действием (cEmActDmgShrink).

ЧТО ПЕЧАТАЕТ.
  1. Смерти и уходы: тело, вид, последнее действие, вердикт, размер, дистанция.
  2. Карточку убитого: ванильные атака/защита/матк/мдеф (то, что было ДО наших
     множителей), наш размер и его ванильная база, роли за жизнь.
  3. Компоненты вида (uEm0100_*): к какому телу привязаны и с кем исчезли —
     так ищется корона, если она отдельная деталь.
  4. Состав пачки на конец встречи (строки dump).

ЗАПУСК:  python3 tools/analyze_goblin_roles.py путь/к/ddda_ai_overhaul.log
"""
import re
import sys
from collections import OrderedDict

# ── строки лога ────────────────────────────────────────────────────────────
R_DEATH_NEW = re.compile(
    r'DEATH @0x([0-9a-f]+) kind=(\S+) lastAct=(\S+) role=(\S+) scaleH=(-?[\d.]+)'
    r' dist=(-?[\d.]+)m lived=(\d+)ms conf=(\w+) hint=(\w+) mates=(\d+)'
    r' nearComp=(\S+)(?: compDist=(-?[\d.]+)m)?')
R_LEAVE_NEW = re.compile(
    r'LEAVE @0x([0-9a-f]+) kind=(\S+) lastAct=(\S+) role=(\S+) scaleH=(-?[\d.]+)'
    r' dist=(-?[\d.]+)m lived=(\d+)ms why=(\S+)')
R_LEAVE_OLD = re.compile(
    r'LEAVE @0x([0-9a-f]+) lastAct=(\S+) role=(\S+) lived=(\d+)ms')
R_JOIN = re.compile(
    r'JOIN @0x([0-9a-f]+) kind=(\S+) act=(\S+) role=(\S+) scaleH=(-?[\d.]+)'
    r' dist=(-?[\d.]+)m')
R_ROLE = re.compile(r'ROLE @0x([0-9a-f]+) (\S+) act=(\S+)')
R_HORN = re.compile(r'(HORN|CHARGE) @0x([0-9a-f]+) act=(\S+) n=(\d+)')
R_IGNORE = re.compile(r'IGNORE-LEADER @0x([0-9a-f]+) act=(\S+)')
R_FLEE_LL = re.compile(r'FLEE @0x([0-9a-f]+) after-leader-lost (\d+)ms')
R_CAND = re.compile(r'LEADER-CAND @0x([0-9a-f]+) reason=(\S+)')
R_LOST = re.compile(r'LEADER-LOST @0x([0-9a-f]+) was=(\S+)')
R_FALL = re.compile(r'LEADER-FALL confirmed @0x([0-9a-f]+)')
R_SKIP = re.compile(r'SKIP (\S+) \(component, not full-body uEm0100\)'
                    r'(?: nearest=0x([0-9a-f]+) dist=(-?[\d.]+)m pos=(\S+))?')
R_COMP_GONE = re.compile(r'COMPONENT-GONE (\S+) \(last nearest=0x([0-9a-f]+)'
                         r' dist=(-?[\d.]+)m\)')
R_DUMP = re.compile(r'PackObserve dump: n=(\d+) composition=(\S+) leader=(\S+)'
                    r' cand=0x([0-9a-f]+) horn=(\d) charge=(\d) ignore=(\d)'
                    r' fall=(\d) shield=(\d) caller=(\d) flee=(\d)')
R_DUMP_MEM = re.compile(r'PackObserve: member @0x([0-9a-f]+) role=(\S+)'
                        r' act=(\S+) scaleH=(-?[\d.]+) dist=(-?[\d.]+)'
                        r' horn=(\d+) charge=(\d+) escape=(\d+) ignore=(\d+)')
# 85.42: и новые строки (rank), и старые (ladder со step=) — лог 85.40/85.41 читается тем же разборщиком
R_RESIST = re.compile(r'EnemyTuner: resist (\S+) 0x([0-9a-fA-F]+) elem (.*?) \| (.*?) \| flinch ([\d.]+) kdown ([\d.]+)')
R_RESIST_OLD = re.compile(r'EnemyTuner: resist (\S+) 0x([0-9a-fA-F]+) fire ([\d.]+) \|(.*?)\| flinch ([\d.]+) kdown ([\d.]+)')
R_RESIST_APP = re.compile(r'EnemyTuner: resist applied (\S+) 0x([0-9a-fA-F]+) res x([\d.]+) stand x([\d.]+) wrote (\d+)')
R_RANK = re.compile(r'EnemyTuner: (?:rank|ladder) (\S+) (?:step=)?(\w+)\((\d)\) size ([-\d.]+) atk x([\d.]+) -> 0x([0-9a-f]+)', re.I)
R_TSCALE = re.compile(r'EnemyTuner: scale (-?[\d.]+) \(base (-?[\d.]+) (\w+)\)'
                      r' -> (\S+) 0x([0-9a-f]+) was=(-?[\d.]+) applies=(\d+)'
                      r' engineReverts=(\d+)')
R_TSTATS = re.compile(
    r'EnemyTuner: CombatStats x([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)'
    r' \(roll ([\d.]+)/([\d.]+)/([\d.]+)/([\d.]+)\) -> (\S+) 0x([0-9a-f]+)'
    r' +atk (-?[\d.]+)->(-?[\d.]+) def (-?[\d.]+)->(-?[\d.]+)'
    r' matk (-?[\d.]+)->(-?[\d.]+) mdef (-?[\d.]+)->(-?[\d.]+)')

DEATHISH = ('DmgShrink', 'Die', 'Dead')


def looks_dead(act):
    return any(k in act for k in DEATHISH)


def parse(path):
    bodies = OrderedDict()      # addr -> dict
    events = []                 # (lineno, text) по порядку
    resfail = []                # отказы/неудачные попытки чтения крепости
    comps = OrderedDict()       # kind -> dict
    dumps = []
    stats_seen = {}
    with open(path, encoding='utf-8', errors='replace') as f:
        for i, line in enumerate(f, 1):
            line = line.rstrip('\n')
            # адрес тела у каждой строки — в СВОЁМ поле, не всегда первое
            m = R_RANK.search(line)
            if m:
                a = ('0x' + m.group(6)).lower()
                b = bodies.setdefault(a, {'events': []})
                b['events'].append((i, 'RANK', line))
                b['rank'] = (m.group(2), float(m.group(4)), float(m.group(5)))

            m = R_RESIST.search(line)
            if m:
                a = '0x' + m.group(2).lower()
                b = bodies.setdefault(a, {'events': []})
                b['events'].append((i, 'RESIST', line))
                b['resist'] = (m.group(1), m.group(3).strip(), m.group(4).strip(),
                               m.group(5), m.group(6))
                b['resist_new'] = True
            elif R_RESIST_OLD.search(line):
                m = R_RESIST_OLD.search(line)
                a = '0x' + m.group(2).lower()
                b = bodies.setdefault(a, {'events': []})
                b['events'].append((i, 'RESIST', line))
                b['resist'] = (m.group(1), 'fire ' + m.group(3), m.group(4).strip(),
                               m.group(5), m.group(6))
            if 'resist' in line and ('map rejected' in line or 'not ready' in line):
                resfail.append((i, line))
            m = R_RESIST_APP.search(line)
            if m:
                a = '0x' + m.group(2).lower()
                b = bodies.setdefault(a, {'events': []})
                b['resapp'] = (m.group(3), m.group(4), m.group(5))

            m = R_DEATH_NEW.search(line)
            if m:
                a = ('0x' + m.group(1)).lower()
                bodies.setdefault(a, {'events': []})['events'].append(
                    (i, 'DEATH', line))
            else:
                m = R_LEAVE_NEW.search(line)
                if m:
                    a = ('0x' + m.group(1)).lower()
                    bodies.setdefault(a, {'events': []})['events'].append(
                        (i, 'LEAVE', line))
                else:
                    m = R_TSCALE.search(line)
                    if m:
                        a = ('0x' + m.group(5)).lower()
                        bodies.setdefault(a, {'events': []})['events'].append(
                            (i, 'TSCALE', line))
                    else:
                        m = R_TSTATS.search(line)
                        if m:
                            a = ('0x' + m.group(10)).lower()
                            bodies.setdefault(a, {'events': []})['events'].append(
                                (i, 'TSTATS', line))

            m = R_LEAVE_OLD.search(line)
            if m:
                a = '0x' + m.group(1).lower()
                bodies.setdefault(a, {'events': []})
                bodies[a]['events'].append((i, 'LEAVE-OLD', line))

            m = R_JOIN.search(line)
            if m:
                a = '0x' + m.group(1).lower()
                b = bodies.setdefault(a, {'events': []})
                b['events'].append((i, 'JOIN', line))
                b['join_scale'] = float(m.group(5))
                b['join_dist'] = float(m.group(6))

            m = R_ROLE.search(line)
            if m:
                a = '0x' + m.group(1).lower()
                bodies.setdefault(a, {'events': []})['events'].append(
                    (i, 'ROLE', line))
            m = R_HORN.search(line)
            if m:
                a = '0x' + m.group(2).lower()
                bodies.setdefault(a, {'events': []})['events'].append(
                    (i, m.group(1), line))
            m = R_IGNORE.search(line)
            if m:
                a = '0x' + m.group(1).lower()
                bodies.setdefault(a, {'events': []})['events'].append(
                    (i, 'INSUB', line))
            if R_CAND.search(line) or R_LOST.search(line) or R_FALL.search(line):
                events.append((i, line))

            m = R_SKIP.search(line)
            if m:
                c = comps.setdefault(m.group(1), {})
                c['skip_line'] = line
                if m.group(2):
                    c['near'] = '0x' + m.group(2).lower()
                    c['dist'] = float(m.group(3))
                    c['pos'] = m.group(4)
            m = R_COMP_GONE.search(line)
            if m:
                c = comps.setdefault(m.group(1), {})
                c['gone'] = line
                c['gone_near'] = '0x' + m.group(2).lower()
                c['gone_dist'] = float(m.group(3))

            m = R_DUMP.search(line)
            if m:
                dumps.append((i, m.groups()))
            m = R_DUMP_MEM.search(line)
            if m:
                # члены пачки идут строкой ниже dump
                bodies.setdefault('0x' + m.group(1).lower(),
                                  {'events': []})['member'] = m.groups()
            if R_FLEE_LL.search(line):
                events.append((i, line))

            # повторные STATS/SCALE одного тела (движок откатил) — оставляем все,
            # но дедуп по точному тексту
            if 'EnemyTuner:' in line:
                stats_seen[(i, line)] = True
    return bodies, comps, dumps, events, resfail


def tuner_card(body_entry):
    """Ванильные числа тела (то, что было ДО наших множителей) и наш размер."""
    out = {'vanilla': None, 'ours': None, 'scale': None, 'base': None}
    stats = [(i, l) for i, t, l in body_entry['events'] if t == 'TSTATS']
    scales = [(i, l) for i, t, l in body_entry['events'] if t == 'TSCALE']
    if stats:
        i, l = stats[-1]
        m = R_TSTATS.search(l)
        out['mult'] = m.group(1) + '/' + m.group(2) + '/' + m.group(3) + '/' + m.group(4)
        out['slots'] = 'x%s roll %s/%s/%s/%s' % (out['mult'], m.group(5), m.group(6),
                                                 m.group(7), m.group(8))
        out['vanilla'] = 'atk %s def %s matk %s mdef %s' % (m.group(11), m.group(13),
                                                            m.group(15), m.group(17))
        out['ours'] = 'atk %s def %s matk %s mdef %s' % (m.group(12), m.group(14),
                                                         m.group(16), m.group(18))
    ranks = [(i, l) for i, t, l in body_entry['events'] if t == 'RANK']
    if ranks:
        seen = []
        for i, l in ranks:
            m = R_RANK.search(l)
            s_ = '%s(%s) рост %s атака x%s' % (m.group(2), m.group(3), m.group(4), m.group(5))
            if s_ not in seen:
                seen.append(s_)
        out['rank'] = ' | '.join(seen[:3]) + (' | ...' if len(seen) > 3 else '')
        if len(seen) > 1:
            out['rank'] += '  (выдач %d — адрес мог быть переиспользован движком)' % len(seen)
    if scales:
        i, l = scales[-1]
        m = R_TSCALE.search(l)
        out['scale'] = float(m.group(1))
        out['base'] = float(m.group(2))
        out['applies'] = int(m.group(7))
        out['reverts'] = int(m.group(8))
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    bodies, comps, dumps, events, resfail = parse(path)

    print("=" * 78)
    print("1. СМЕРТИ И УХОДЫ (кто перестал существовать в кадре)")
    print("=" * 78)
    deaths = []
    for a, b in bodies.items():
        for i, tag, line in b['events']:
            if tag == 'DEATH':
                deaths.append((i, a, line, True))
            elif tag == 'LEAVE':
                near = re.search(r'dist=(-?[\d.]+)m', line)
                act = re.search(r'lastAct=(\S+)', line)
                conf = 'likely' if (act and looks_dead(act.group(1))
                                    and near and float(near.group(1)) >= 0
                                    and float(near.group(1)) <= 60.0) else 'unlikely'
                deaths.append((i, a, line, conf == 'likely'))
            elif tag == 'LEAVE-OLD':
                act = re.search(r'lastAct=(\S+)', line)
                if act and looks_dead(act.group(1)):
                    deaths.append((i, a, line + '   <- 85.38: DEATH-строки нет, '
                                   'улика = предсмертное действие', True))
    deaths.sort()
    if not deaths:
        print("  смертей/уходов не видно")
    for i, a, line, is_death in deaths:
        note = ''
        if 'nearComp=' in line and 'compDist=' not in line:
            m = re.search(r'nearComp=(\S+)', line)
            if m and m.group(1) != 'none':
                note = ('   <- деталь %s была НЕ на теле (в строке нет compDist,'
                        ' это далёкий объект)' % m.group(1))
        print(f"  {i:5d}  {'СМЕРТЬ ' if is_death else 'уход   '} {line[:150]}{note}")

    print()
    print("=" * 78)
    print("2. КАРТОЧКИ УБИТЫХ (ванильные числа = то, что было ДО наших множителей)")
    print("=" * 78)
    for i, a, line, is_death in deaths:
        if not is_death:
            continue
        card = tuner_card(bodies[a])
        roles = [(j, t, l) for j, t, l in bodies[a]['events']
                 if t in ('JOIN', 'ROLE', 'HORN', 'CHARGE', 'INSUB')]
        print(f"\n  тело {a}")
        print(f"    строка: {line[:160]}")
        if card['vanilla']:
            print(f"    ваниль: {card['vanilla']}")
            print(f"    наложено ({card['slots']}): {card['ours']}")
        else:
            print("    ваниль: строк EnemyTuner по этому телу нет")
        if card.get('rank'):
            print("    ранг: " + card['rank'])
        rs = bodies[a].get('resist')
        if rs:
            if bodies[a].get('resist_new'):
                print("    крепость (ваниль из игры): elem %s | %s | flinch %s kdown %s"
                      % (rs[1], rs[2], rs[3], rs[4]))
            else:
                print("    крепость (ваниль из игры): fire %s | %s | flinch %s kdown %s"
                      % (rs[1], rs[2], rs[3], rs[4]))
        ra = bodies[a].get('resapp')
        if ra:
            print("    крепость правили: сопротивления x%s, сбивание x%s, записей %s"
                  % (ra[0], ra[1], ra[2]))
        if card['scale'] is not None:
            print(f"    размер: наш {card['scale']} (база вида {card['base']}, "
                  f"наложений {card.get('applies')}, откатов {card.get('reverts')})")
        if roles:
            print("    жизнь:")
            for j, t, l in roles:
                print(f"      {j:5d} [{t}] {l.strip()[:130]}")
        else:
            print("    жизнь: строк ролей нет (тело появилось и умерло между тиками)")

    print()
    print("=" * 78)
    print("3. КОМПОНЕНТЫ ВИДА (uEm0100_*): к кому привязаны и с кем исчезли")
    print("=" * 78)
    if not comps:
        print("  компонентов в логе нет")
    for kind, c in comps.items():
        near = c.get('near', '?')
        dist = c.get('dist')
        tail = f"ближайшее тело {near}" + (f", {dist} м" if dist is not None else "")
        print(f"  {kind}: {tail}")
        if 'gone' in c:
            print(f"      исчез: {c['gone'][:130]}")

    print()
    print("=" * 78)
    print("4. СОСТАВ ПАЧКИ НА КОНЕЦ ВСТРЕЧ")
    print("=" * 78)
    if not dumps:
        print("  строк dump в логе нет (сборка до 85.39 или dump не вызвался)")
    for i, g in dumps:
        print(f"  {i:5d}  n={g[0]} {g[1]} leader={g[2]} horn={g[4]} charge={g[5]} "
              f"ignore={g[6]} fall={g[7]} shield={g[8]} caller={g[9]} flee={g[10]}")

    print()
    print("=" * 78)
    print("5. КРЕПОСТЬ: ЧТО ИГРА САМА ДАЁТ ГОБЛИНАМ (ванильные числа, 85.44)")
    print("=" * 78)
    sets = {}
    for a, b in sorted(bodies.items()):
        rs = b.get('resist')
        if not rs:
            continue
        head = "elem" if b.get('resist_new') else "fire"
        key = "%s %s | %s | flinch %s kdown %s" % (head, rs[1], rs[2], rs[3], rs[4])
        sets.setdefault(key, []).append(a)
    nrej = len([1 for i, l in resfail if 'map rejected' in l])
    ntry = len([1 for i, l in resfail if 'not ready' in l])
    if nrej or ntry:
        print("  ВНИМАНИЕ: тел с отказом чтения %d, первых неудачных попыток %d"
              % (nrej, ntry))
        print("    (до 85.46 отказ был навсегда — первая пачка после загрузки зоны")
        print("     могла остаться без крепости; от 85.46 код пробует ещё)")
    if not sets:
        print("  строк нет (сборка до 85.44 либо крепость не читалась)")
    elif len(sets) == 1:
        k = list(sets.keys())[0]
        print("  у всех %d тел ОДИН и тот же набор:" % len(sets[k]))
        print("    " + k)
    else:
        print("  наборов: %d (тела одного вида обязаны совпадать!)" % len(sets))
        for k, v in sets.items():
            print("    [%d тел] %s" % (len(v), k))

    print()
    print("=" * 78)
    print("КАК ЧИТАТЬ")
    print("=" * 78)
    print("""  1) Убил короносца ПЕРВЫМ — смотри пункт 1: первая смерть и есть его тело.
  2) Пункт 2 даёт его ванильные числа: это то, что мы НЕ трогали (атака,
     защита), и наш размер рядом с ванильной базой вида.
  3) Пункт 3: если корона — отдельная деталь, она исчезнет в тот же тик, что и
     носитель; строка COMPONENT-GONE назовёт это тело. Так корона находится
     без глаз и без разгадывания.
  4) Побег стаи после смерти вожака виден строками ROLE ... flee (85.39)
     и счётчиком flee в dump. До 85.39 побег был виден только в окне 2 с
     после потерянного кандидата.""")


if __name__ == "__main__":
    sys.exit(main())

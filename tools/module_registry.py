#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""module_registry.py — реестр модулей: файл ↔ сборка ↔ точка подключения.

ЗАЧЕМ. Подключение модуля в этом проекте — три ручных шага (ARCHITECTURE §5.1):
`#include` → поле в оркестраторе/вызов в цепочке → строка в `.vcxproj`. Три шага на
33 файла слоёв `pawnai/` + `monsterai/` — это три места, где забывают. Пока проект
рос, забывать было почти негде; с ростом мода это первое, что начнёт тихо расходиться.
Первый же прогон это доказал: контракт журнала `LogMemSession.h` (11 включений) и
`DoctrineAnnounce.h` в сборке объявлены не были — solution врал, компилятор молчал.

Здесь тот же приём, что и в doc_check: правило, которое нельзя забыть, проверяет машина.
Но проверка тут не «похоже/непохоже», а контракт:

  1. ФАЙЛЫ ↔ СБОРКА. Каждый файл кода из `src/` объявлен в `ddda-ai-overhaul.vcxproj`
     и каждая запись сборки существует на диске. Односторонняя проверка не ловит
     главный случай: файл написали, а в сборку не добавили.
  2. ТОЧКА ПОДКЛЮЧЕНИЯ. У каждого `.cpp` должен быть входящий вызов из другого файла
     проекта (цепочка `PawnAI.cpp`, оркестратор, точка входа `dinput8.cpp`, режиссёр
     монстров и т.д.). Файл без входящих ссылок обязан быть в списке исключений
     с причиной — тогда решение видно глазами, а не ищется в логах линкера.
  3. ЦЕПОЧКА ЖИВА. Каждый вызов из списка `UpdatePawnAI()` разрешается в файл, где
     функция определена. Так ловится переименование: цепочку поправили не везде.
  4. ХЕДЕРЫ. Заголовок, который никто не включает, — предупреждение (не отказ):
     это кандидат на удаление, а не поломка. Живые исключения — в списке ниже.

Символы ищем эвристикой (регексп + порог уникальности), поэтому отчёт печатает
ФАКТ («вызывается из src/pawnai/AcquisitorManager.cpp»), а не оценку.

Запуск:
  python3 tools/module_registry.py                # проверка, код возврата 1 при нарушении
  python3 tools/module_registry.py --md docs/MODULE_REGISTRY.md   # реестр таблицей
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
VCXPROJ = os.path.join(ROOT, 'ddda-ai-overhaul.vcxproj')
CODE_EXT = ('.cpp', '.h', '.inl')

# --- Решения, которые проверка обязана уважать -------------------------------
# Каждый список — не «костыли», а запись решения: причина обязательна.

# .cpp без входящих вызовов: так и задумано.
EXCEPTIONS_CPP = {
    'dinput8.cpp':       'точка входа DLL: InitHooks/детуры, зовёт всех остальных',
    'PawnAI.cpp':        'оркестратор: цепочка UpdatePawnAI и UI-панель',
}

# Файлы кода, которых НЕТ в сборке намеренно.
NOT_IN_BUILD = {
    'src/MonsterCards.Generated.h':
        'каталог карт (84.68): справочник, код его не включает, читается инструментами',
}

# Хедеры без потребителей: так и задумано (иначе — предупреждение).
EXCEPTIONS_HDR = {
    'PawnAI_Common.h':      'общий словарь слоя pawnai, тянут модульные хедеры',
    'RuntimeInternal.h':    'внутренний контракт рантайма: публичный Runtime.h тянет он сам',
    'EnemyFileBase.h':      'данные: константы файловой базы видов (85.60)',
    'stdafx.h':             'прекомпилированный заголовок MSVC',
    'Bestiary.Generated.h':
        'шим генератора: пишется всегда, код тянет BestiaryData.h напрямую',
    'MonsterCards.Generated.h':
        'каталог карт (84.68): справочник, код его не включает — см. NOT_IN_BUILD',
}
# БЫЛО (до 85.67): сюда не вносили src/pawnai/FieldMap.h намеренно — мёртвый файл,
# ссылавшийся на исчезнувшую секцию [offsets]. Решение принято: файл удалён,
# знания о причинах — docs/PARKED.md §3.

NOISE = {
    'Tick', 'Init', 'Shutdown', 'Update', 'Render', 'RenderUI', 'Main', 'Log',
    'Reset', 'Clear', 'Apply', 'Parse', 'Load', 'Save', 'Get', 'Set', 'OnEvent',
    'operator', 'printf', 'assert', 'enable', 'enabled', 'delta', 'target',
}


def read(path):
    return io.open(path, encoding='utf-8', errors='replace').read()


def src_files():
    out = []
    for d, _dirs, names in os.walk(SRC):
        for n in sorted(names):
            if n.endswith(CODE_EXT):
                out.append(os.path.relpath(os.path.join(d, n), SRC).replace('\\', '/'))
    return sorted(out)


def vcxproj_entries():
    text = read(VCXPROJ)
    got = set()
    for m in re.finditer(r'<Cl(?:Compile|Include)\s+Include="([^"]+)"', text):
        got.add(m.group(1).replace('\\', '/'))
    return got


def definitions(text):
    """Имена функций/типов, объявленные в файле (эвристика).

    Возвращаем пару: (имена без шума, ВСЕ имена). Второй набор нужен, чтобы
    разрешать вызовы вида 'MonsterAI::Tick()' — там последнее имя ('Tick')
    из первого набора вычищено как шум.
    """
    every = set()
    for m in re.finditer(r'^[A-Za-z_][\w:<>,\s\*&]*?\b([A-Za-z_]\w*(?:::[A-Za-z_]\w+)*)\s*\(', text, re.M):
        every.add(m.group(1))
    for m in re.finditer(r'^\s*(?:struct|class|namespace|union|enum)\s+([A-Za-z_]\w*)', text, re.M):
        every.add(m.group(1))
    clean = {s for s in every
             if ('::' in s) or (s not in NOISE and len(s) >= 4)}
    return clean, every


def namespaces(text):
    """Объявленные в файле namespace'ы (для разрешения 'A::B::Tick')."""
    return set(re.findall(r'^\s*namespace\s+([A-Za-z_]\w*)', text, re.M))


def orchestrated_types(text):
    """Типы полей оркестратора: '    PresetManager      presets;' → PresetManager."""
    return set(re.findall(r'^\s{2,}([A-Z]\w+)\s+[a-z]\w*\s*;', text, re.M))


def tokens(text):
    return set(re.findall(r'\b[A-Za-z_]\w*\b', text))


def chain_calls(text):
    """Вызовы внутри списка UpdatePawnAI(): 'A::B()', 'A_B()'."""
    out, base = [], None
    start = text.find('void UpdatePawnAI()')
    if start >= 0:
        base = text[start:]
        end = base.find('\nvoid ', 10)
        if end > 0:
            base = base[:end]
    if not base:
        return out
    for m in re.finditer(r'__try\s*\{\s*([A-Za-z_][\w:]*)\(', base):
        out.append(m.group(1))
    return out


def resolve(call, files, texts, defs, defs_all=None, ns=None):
    """Файл, где определён вызов цепочки.

    1. Идём от ближайшего префикса наружу: 'Runtime::PartyStatus::Tick' → ищем,
       кто объявляет PartyStatus (namespace/struct/class). Так работают и
       'EntityCfg::Tick' (namespace в хедере), и 'MonsterAI::PackObserveTick'.
    2. Свободные функции без префикса ('CombatIntel_Tick') ищем по определениям.
    """
    parts = call.split('::')
    qual = '::'.join(parts[-2:])
    last = parts[-1]
    pref = parts[-2] if len(parts) >= 2 else ''
    if defs_all and ns:
        # 1. точное составное определение ('PartyStatus::Tick', 'Possession::Tick')
        hits = [f for f in files if qual in defs_all[f]]
        if hits:
            cpp = [f for f in hits if f.endswith('.cpp')]
            return sorted(cpp or hits)[0]
        # 2. имя внутри своего namespace/типа ('MonsterAI::Tick', 'PawnAI::Foo')
        hits = [f for f in files
                if pref and last in defs_all[f]
                and (pref in ns[f] or pref in defs[f])]
        if hits:
            cpp = [f for f in hits if f.endswith('.cpp')]
            return sorted(cpp or hits)[0]
    for pref in reversed(parts[:-1]):
        if len(pref) < 4:
            continue
        # Только настоящее объявление в начале строки: «using namespace Runtime;»
        # объявлением не является (на этом инструмент уже один раз обжёгся).
        pat = re.compile(r'^\s*(?:namespace|struct|class|union)\s+' + re.escape(pref) + r'\b', re.M)
        hits = [f for f in files if pat.search(texts[f])]
        if hits:
            return sorted(hits)[0]
    hits = [f for f in files if last in defs[f]]
    if hits:
        cpp = [f for f in hits if f.endswith('.cpp')]
        return sorted(cpp or hits)[0]
    return None


def main():
    md_path = None
    if '--md' in sys.argv:
        md_path = sys.argv[sys.argv.index('--md') + 1]

    files = src_files()
    build = vcxproj_entries()
    # Сборка перечисляет файлы путями от корня (src/, ImGui/, корень). Сравниваем
    # полными путями: иначе «файл есть, а в сборку не добавлен» не поймать.
    disk_all = set()
    for d, dirs, names in os.walk(ROOT):
        dirs[:] = [x for x in dirs
                   if x not in ('.git', 'builds', 'docs', 'tools', 'resources')]
        for n in names:
            if n.endswith(CODE_EXT):
                disk_all.add(os.path.relpath(os.path.join(d, n), ROOT).replace('\\', '/'))
    build_src = {b for b in build if b.startswith('src/')}

    texts = {f: read(os.path.join(SRC, f)) for f in files if not f.endswith('.inl')}
    _pair = {f: definitions(texts[f]) for f in texts}
    defs = {f: _pair[f][0] for f in texts}
    defs_all = {f: _pair[f][1] for f in texts}
    ns = {f: namespaces(texts[f]) for f in texts}
    toks = {f: tokens(texts[f]) for f in texts}
    code_files = sorted(texts)

    spread = {}
    for f in code_files:
        for sym in defs[f]:
            spread[sym] = spread.get(sym, 0) + 1
    unique_defs = {f: {s for s in defs[f] if spread.get(s, 9) <= 3} for f in code_files}

    bad, warns, notes = [], [], []

    orch_types = orchestrated_types(texts.get('pawnai/PawnAI_BusOrchestrator.h', ''))

    # Цепочка UpdatePawnAI: какой вызов какому файлу принадлежит (нужно и для
    # «точки подключения», и для проверки живости цепочки ниже).
    calls = chain_calls(texts['PawnAI.cpp'])
    chain_host, unresolved = {}, []
    for c in calls:
        f = resolve(c, code_files, texts, defs, defs_all, ns)
        if f is None:
            unresolved.append(c)
        else:
            chain_host.setdefault(f, []).append(c)

    # 1. Файлы ↔ сборка
    not_in_build = sorted(f for f in disk_all - build if not f.startswith('src/'))
    if not_in_build:
        bad.append('в сборке (.vcxproj) нет %d файлов: %s'
                   % (len(not_in_build), ', '.join(not_in_build[:6])))
    missing_src = sorted(src_files() and (set(f for f in disk_all if f.startswith('src/')) - build_src))
    for m in missing_src:
        if m in NOT_IN_BUILD:
            notes.append('%s — не в сборке намеренно: %s' % (m, NOT_IN_BUILD[m]))
        else:
            bad.append('в .vcxproj нет файла src/: %s' % m)
    ghost = sorted(build_src - disk_all)
    if ghost:
        bad.append('в .vcxproj есть %d записей, которых нет на диске: %s'
                   % (len(ghost), ', '.join(ghost[:6])))

    # 2. Точка подключения: кто ссылается на модуль
    attached = {}
    for f in code_files:
        if not f.endswith('.cpp'):
            continue
        base = os.path.basename(f)[:-4]
        syms = set(unique_defs[f]) | {base}
        # Собственный хедер модуля — не «вызывающий»: 'Foo.h' упоминает 'Foo' всегда.
        own = {g for g in code_files if g != f and os.path.basename(g) == base + '.h'}
        callers = [g for g in code_files if g != f and g not in own and (syms & toks[g])]
        if callers:
            own_h = os.path.basename(f)[:-4] + '.h'
            attached[f] = {
                'chain': chain_host.get(f),
                'orch': os.path.basename(f)[:-4] in orch_types,
                'entry': 'dinput8.cpp' in callers,
                'cpp': [g for g in callers if g.endswith('.cpp')],
                'hdr': [g for g in callers
                        if g.endswith('.h') and not g.endswith('.Generated.h')
                        and os.path.basename(g) != own_h],
            }
        elif f in EXCEPTIONS_CPP:
            attached[f] = None
        else:
            bad.append('src/%s без точки подключения (нет входящих вызовов и нет '
                       'в исключениях)' % f)

    # 3. Цепочка UpdatePawnAI разрешается в определения
    if unresolved:
        bad.append('вызовы цепочки никуда не разрешаются (%d): %s'
                   % (len(unresolved), ', '.join(unresolved[:6])))

    # 4. Хедеры без потребителей — предупреждение
    for h in sorted(f for f in code_files if f.endswith('.h')):
        if h in EXCEPTIONS_HDR:
            continue
        base = os.path.basename(h)
        if not any(g != h and base in texts[g] for g in code_files):
            warns.append(h)

    for n in notes:
        print('  принято: %s' % n)
    for w in warns:
        print('  предупреждение: хедер без потребителей: src/%s' % w)
    for b in bad:
        print(' ', b)
    if bad:
        print('реестр модулей: НАРУШЕНИЙ %d' % len(bad))
        return 1
    cpp_n = sum(1 for f in code_files if f.endswith('.cpp'))
    print('реестр модулей: %d файлов, сборка сходится, точек подключения %d .cpp, '
          'цепочка UpdatePawnAI: вызовов под SEH %d/%d, хедеров без потребителей %d'
          % (len(files), cpp_n, len(calls) - len(unresolved), len(calls), len(warns)))

    if md_path:
        write_md(md_path, files, attached, len(calls), warns)
    return 0


def write_md(path, files, attached, calls_n, warns):
    L = []
    L.append('# MODULE_REGISTRY — реестр модулей (генерируется)')
    L.append('')
    L.append('> Сгенерировано `python3 tools/module_registry.py --md docs/MODULE_REGISTRY.md`.')
    L.append('> Смысл: у каждого файла кода есть машинно проверенная точка подключения — либо')
    L.append('> входящий вызов, либо строка в списке исключений с причиной. Шаг сборки `10f/10`')
    L.append('> следит, чтобы реестр не разошёлся с кодом.')
    L.append('')
    L.append('Подключение модуля — три ручных шага (`ARCHITECTURE.md` §5.1 и §8): `#include` →')
    L.append('вызов в цепочке/оркестраторе → строка в `.vcxproj`. Реестр показывает, что второй')
    L.append('шаг у каждого файла есть; первый и третий проверяются машиной в том же прогоне.')
    L.append('')
    layers = {}
    for f in files:
        layers.setdefault(f.split('/')[0] if '/' in f else '(корень)', []).append(f)
    L.append('## Сводка по слоям')
    L.append('')
    L.append('| Слой | Файлов кода |')
    L.append('|---|---|')
    for layer in sorted(layers):
        L.append('| `src/%s` | %d |' % (layer, len(layers[layer])))
    L.append('')
    L.append('## Точки подключения')
    L.append('')
    L.append('| Файл | Подключён через |')
    L.append('|---|---|')
    for f in files:
        if not f.endswith('.cpp'):
            continue
        a = attached.get(f)
        if a is None:
            L.append('| `%s` | исключение: %s |' % (f, EXCEPTIONS_CPP.get(f, '')))
            continue
        how = []
        if a['chain']:
            cs = sorted(a['chain'], key=lambda c: (0 if c.endswith('Tick') else 1, c))
            shown = ', '.join('`%s()`' % c for c in cs[:2])
            more = '' if len(cs) <= 2 else ' и ещё %d' % (len(cs) - 2)
            how.append('**цепочка** `UpdatePawnAI`: %s%s' % (shown, more))
        if a['orch']:
            how.append('**оркестратор**: поле `%s` в `PawnAI_BusOrchestrator.h`'
                       % os.path.basename(f)[:-4])
        if a['entry']:
            how.append('**точка входа** `dinput8.cpp`')
        cpp = [c for c in a['cpp'] if c != 'dinput8.cpp']
        for c in cpp[:3]:
            how.append('вызывается из `%s`' % c)
        if len(cpp) > 3:
            how.append('и ещё %d .cpp' % (len(cpp) - 3))
        if not how and a['hdr']:
            how.append('только объявления (хедеры): %s'
                       % ', '.join('`%s`' % h for h in a['hdr'][:2]))
        L.append('| `%s` | %s |' % (f, '; '.join(how)))
    L.append('')
    L.append('Цепочка `UpdatePawnAI()`: %d вызовов, все разрешаются в определения.' % calls_n)
    L.append('')
    if warns:
        L.append('Хедеры без потребителей (кандидаты на удаление, каждый — задача, не смена):')
        L.append('')
        for w in warns:
            L.append('* `src/%s`' % w)
        L.append('')
    L.append('Файлы, которых нет в сборке намеренно:')
    L.append('')
    for k, v in sorted(NOT_IN_BUILD.items()):
        L.append('* `%s` — %s' % (k, v))
    L.append('')
    io.open(os.path.join(ROOT, path), 'w', encoding='utf-8', newline='\n').write('\n'.join(L))


if __name__ == '__main__':
    sys.exit(main())

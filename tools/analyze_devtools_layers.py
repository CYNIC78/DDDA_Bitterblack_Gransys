#!/usr/bin/env python3
"""
Страж слоёв: DevTools.cpp обязан оставаться ИССЛЕДОВАТЕЛЬСКИМ модулем.

Что делает:
  1. Разбирает DevTools.cpp на функции верхнего уровня (парсер скобок, с
     пропуском строк/символов/комментариев/препроцессора).
  2. Строит граф вызовов и карту обращений к файловым статикам.
  3. Берёт список DevTools::-символов, которые реально дёргает продуктовый код
     (src/*.cpp вне src/devtools/), и считает ТРАНЗИТИВНОЕ ЗАМЫКАНИЕ по графу
     вызовов — это ровно тот код, который обязан переехать в src/runtime/.
  4. Всё остальное — исследовательский слой / собственно DevTools.

После Build 69 продуктовый слой живёт в src/runtime/ и не зависит от DevTools.
Инвариант: НИ ОДИН продуктовый модуль не вызывает DevTools:: напрямую.
Если инвариант нарушен, скрипт падает с кодом 1 — значит, слои снова поехали
и в DevTools.cpp опять заводится продуктовый код.

Запуск:  python3 tools/analyze_devtools_layers.py
Вывод:   docs/DEVTOOLS_LAYER_MAP.md  +  docs/generated/devtools_layers.json
Код 0 — слои чистые, 1 — нарушение.
"""
from __future__ import annotations

import json
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "src" / "devtools" / "DevTools.cpp"
PRODUCT_GLOBS = ["src/*.cpp", "src/pawnai/*.cpp", "*.cpp"]
EXCLUDE_DIRS = {"devtools", "ImGui"}

KEYWORDS = {
    "if", "for", "while", "switch", "return", "else", "do", "case", "sizeof",
    "catch", "new", "delete", "static_cast", "reinterpret_cast", "const_cast",
    "dynamic_cast", "defined", "and", "or", "not",
}


def strip_noise(text: str) -> str:
    """Заменяет содержимое строк/символов/комментариев пробелами, сохраняя \n."""
    out = []
    i, n = 0, len(text)
    state = "code"
    while i < n:
        c = text[i]
        nx = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nx == "/":
                state, i = "line", i + 2
                out.append("  ")
                continue
            if c == "/" and nx == "*":
                state, i = "block", i + 2
                out.append("  ")
                continue
            if c == '"':
                state, i = "str", i + 1
                out.append(" ")
                continue
            if c == "'":
                state, i = "chr", i + 1
                out.append(" ")
                continue
            out.append(c)
            i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
            i += 1
        elif state == "block":
            if c == "*" and nx == "/":
                state, i = "code", i + 2
                out.append("  ")
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
        else:  # str / chr
            if c == "\\":
                out.append("  ")
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = "code"
            out.append("\n" if c == "\n" else " ")
            i += 1
    return "".join(out)


def blank_preprocessor(text: str) -> str:
    """Гасит препроцессор, СОХРАНЯЯ длину — иначе смещения перестают
    совпадать с сырым текстом и извлечение кода режет не по границам."""
    lines = text.split("\n")
    return "\n".join(" " * len(ln) if ln.lstrip().startswith("#") else ln
                      for ln in lines)


SIG_RE = re.compile(
    r"(?:^|[\s*&])(?:(?P<ns>[A-Za-z_]\w*)\s*::\s*)?(?P<name>[A-Za-z_]\w*)\s*"
    r"\((?P<args>[^;{}]*)\)\s*(?:const\s*)?(?:noexcept\s*)?$",
    re.S,
)


def parse_functions(clean: str, raw: str):
    """Функции верхнего уровня: depth 0 -> 1 на '{'. Возвращает список dict."""
    funcs = []
    depth = 0
    i, n = 0, len(clean)
    stmt_start = 0
    while i < n:
        c = clean[i]
        if c == "{":
            if depth == 0:
                sig = clean[stmt_start:i]
                m = SIG_RE.search(sig.strip())
                body_start = i
                d, j = 0, i
                while j < n:
                    if clean[j] == "{":
                        d += 1
                    elif clean[j] == "}":
                        d -= 1
                        if d == 0:
                            break
                    j += 1
                body_end = j
                if m and m.group("name") not in KEYWORDS:
                    head = sig.strip()
                    is_type = bool(re.match(r"^(struct|class|union|enum|namespace)\b", head))
                    if not is_type:
                        funcs.append({
                            "name": m.group("name"),
                            "qualifier": m.group("ns") or "",
                            "line_start": clean[:stmt_start].count("\n") + 1,
                            "line_end": clean[:body_end].count("\n") + 1,
                            # strip_noise сохраняет длину, поэтому смещения
                            # одинаково валидны для clean и для сырого текста
                            "off_start": stmt_start,
                            "off_end": body_end + 1,
                            "body": clean[body_start:body_end],
                        })
                i = body_end + 1
                stmt_start = i
                depth = 0
                continue
            depth += 1
        elif c == "}":
            depth = max(0, depth - 1)
            if depth == 0:
                stmt_start = i + 1
        elif c == ";" and depth == 0:
            stmt_start = i + 1
        i += 1
    return funcs


def find_file_statics(clean: str):
    """Глобальные static-переменные файла (g_*).

    Ловит и `static float g_x = 0, g_y = 0, g_z = 0;` — один оператор,
    три переменные. Раньше видели только первую, и карта врала.
    """
    statics = {}
    # Важно: шаблон НЕ должен пересекать '{' — иначе он затягивает тело функции
    # и объявляет её локальные обращения к глобалам «определением».
    pats = (
        r"^\s*(?:static|extern)\s+[^;{}#/][^;{}]*?;",          # обычное объявление
        r"^\s*(?:static|extern)\s+[^;{}#/][^;{}]*?=\s*\{[^;]*?\};",  # со списком инициализации
    )
    for pat in pats:
        for m in re.finditer(pat, clean, re.M):
            if "(" in m.group(0).split("=")[0]:
                continue                                        # это функция
            for gm in re.finditer(r"\b(g_\w+)\b", m.group(0)):
                statics.setdefault(gm.group(1), clean[:m.start()].count("\n") + 1)
    for m in re.finditer(r"^\s*static\s+(?:const\s+)?[A-Za-z_][\w:<>,\s\*&\[\]]*?\b(g_\w+)",
                         clean, re.M):
        statics[m.group(1)] = clean[:m.start()].count("\n") + 1
    for m in re.finditer(r"^\s*(?!static)(?:const\s+)?[A-Za-z_][\w:<>,\s\*&\[\]]*?\b(g_\w+)\s*(?:=|\[|;)",
                         clean, re.M):
        statics.setdefault(m.group(1), clean[:m.start()].count("\n") + 1)
    return statics


def product_api_usage():
    """Какие DevTools::X зовёт продуктовый код (вне src/devtools)."""
    usage = defaultdict(list)
    seen = set()
    for pattern in PRODUCT_GLOBS:
        for f in sorted(ROOT.glob(pattern)):
            rel = f.relative_to(ROOT)
            if any(part in EXCLUDE_DIRS for part in rel.parts) or rel in seen:
                continue
            seen.add(rel)
            text = strip_noise(f.read_text(encoding="utf-8", errors="replace"))
            for m in re.finditer(r"DevTools\s*::\s*(\w+)", text):
                usage[m.group(1)].append(str(rel))
    return usage


RESEARCH_HINTS = ("probe", "hunt", "audit", "leashab", "recon", "census", "dump",
                  "scan_", "trace", "experiment", "json", "csv")

# Точки входа, которые продуктовый код зовёт, но продуктом НЕ являются:
# это research-кнопки, ошибочно вшитые в продуктовый UI (src/PawnAI.cpp).
# Судьба каждой зафиксирована в docs/REFACTOR_TASK.md §4.
# Функции с «исследовательскими» именами, которые на деле — продукт
# (проверено чтением кода: это обход списка актёров и live-состояния).
PRODUCT_OVERRIDES = {
    "DumpActorsFrom",           # снимок списка актёров в g_act — ядро WorldScan
    "ScanActSlot",              # разбор слота действия актёра
    "ReadLiveAct",              # живое имя состояния
    "ActAt",                    # резолв Act по оффсету
    "PartyRuntimeProbeName",    # «probe» здесь = live-объект стамины/здоровья
    "PartyRuntimeProbePriority",
    "PartyAddRuntimeProbe",
}

# Предлагаемая раскладка продуктового кода по будущим файлам src/runtime/.
# Порядок важен: первое совпадение выигрывает.
RUNTIME_BUCKETS = [
    ("PriorityPlatform", r"^(PartyPriorityProfile|GuardianFix|PartyKnown)"),
    ("PartyRecon",       r"^(Party|Pawn|Arisen)"),
    ("WorldScan",        r"^(World|Enemy|Publish|Rewalk|DumpActors|ScanActSlot|"
                         r"ReadLiveAct|ActAt|ActName|Seed|Poll|LiveUnit|Kind)"),
    ("MemProbe",         r"^(Rd|Read|Looks|In|Image|Init|Name|Dti|Type|Identify|"
                         r"Module|Rebase|Ms|Vt|Sec)"),
]

PROBE_ENTRY_POINTS = {
    "GuardianPenaltyAudit": "удалить — ответ в SOURCE_OF_TRUTH §3.5",
    "TargetSelectionAudit": "удалить — ответ: цель = тело врага",
    "FollowProbe":          "удалить — ответ в §5.2 (поводка как поля нет)",
    "LeashAbSet":           "удалить — багованый rollback, гипотеза отвергнута",
    "LeashAbIsApplied":     "удалить — часть Leash A/B",
    "LeashAbStatus":        "удалить — часть Leash A/B",
    "GuardianIntentHunt":   "держать до поимки code 4/66, затем удалить",
}


def main() -> int:
    raw = TARGET.read_text(encoding="utf-8", errors="replace")
    clean = blank_preprocessor(strip_noise(raw))

    funcs = parse_functions(clean, raw)
    statics = find_file_statics(clean)
    names = defaultdict(list)
    for f in funcs:
        names[f["name"]].append(f)

    # граф вызовов + обращения к статикам
    for f in funcs:
        body = f["body"]
        called = set()
        for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body):
            cand = m.group(1)
            if cand in KEYWORDS or cand == f["name"]:
                continue
            if cand in names:
                called.add(cand)
        f["calls"] = sorted(called)
        f["globals"] = sorted({g for g in statics if re.search(rf"\b{re.escape(g)}\b", body)})
        f["lines"] = f["line_end"] - f["line_start"] + 1

    def is_research(name: str) -> bool:
        low = name.lower()
        return any(h in low for h in RESEARCH_HINTS)

    usage = product_api_usage()
    # все точки входа, реально определённые в этом файле
    entries_all = sorted(a for a in usage if a in names)
    # настоящий продукт = без research-кнопок, вшитых в продуктовый UI
    entries = [e for e in entries_all if e not in PROBE_ENTRY_POINTS]
    probe_entries = [e for e in entries_all if e in PROBE_ENTRY_POINTS]

    def close(seeds):
        seen, stack = set(), list(seeds)
        while stack:
            cur = stack.pop()
            if cur in seen:
                continue
            seen.add(cur)
            for fn in names.get(cur, []):
                for c in fn["calls"]:
                    if c not in seen:
                        stack.append(c)
        return seen

    closure_raw = close(entries)
    probe_closure = close(probe_entries) - closure_raw  # уйдёт вместе с пробами

    # «Пассажиры»: research-код, физически лежащий в продуктовом пути (его зовут
    # из продуктового тика под research-гейтом g_researchDump/g_intentTrace).
    # Это места разреза: продукт их не должен тянуть за собой в runtime.
    passengers_seed = {n for n in closure_raw
                       if n not in entries and is_research(n)
                       and n not in PRODUCT_OVERRIDES}

    def close_cut(seeds, cut):
        seen, stack = set(), list(seeds)
        while stack:
            cur = stack.pop()
            if cur in seen:
                continue
            seen.add(cur)
            if cur in cut:
                continue          # не идём вглубь пассажира
            for fn in names.get(cur, []):
                for c in fn["calls"]:
                    if c not in seen:
                        stack.append(c)
        return seen

    closure = close_cut(entries, passengers_seed) - passengers_seed
    passengers = closure_raw - closure

    for f in funcs:
        n = f["name"]
        if n in entries:
            f["layer"] = "PRODUCT-API"
        elif n in closure:
            f["layer"] = "PRODUCT-DEP"
        elif n in passengers:
            f["layer"] = "PASSENGER"
        elif n in probe_entries:
            f["layer"] = "PROBE-API"
        elif n in probe_closure:
            f["layer"] = "PROBE-DEP"
        elif is_research(n):
            f["layer"] = "RESEARCH"
        else:
            f["layer"] = "DEVTOOLS"

    total = sum(f["lines"] for f in funcs)
    by_layer = defaultdict(lambda: {"funcs": 0, "lines": 0})
    for f in funcs:
        by_layer[f["layer"]]["funcs"] += 1
        by_layer[f["layer"]]["lines"] += f["lines"]

    # статики, которые трогает продуктовое замыкание И devtools-слой одновременно
    g_product, g_other = defaultdict(set), defaultdict(set)
    for f in funcs:
        bucket = g_product if f["layer"].startswith("PRODUCT") else g_other
        for g in f["globals"]:
            bucket[g].add(f["name"])
    shared = sorted(set(g_product) & set(g_other))

    out_json = ROOT / "docs" / "generated" / "devtools_layers.json"
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps({
        "file": str(TARGET.relative_to(ROOT)),
        "total_lines": raw.count("\n") + 1,
        "functions": [{k: v for k, v in f.items() if k != "body"} for f in funcs],
        "statics": statics,
        "product_entry_points": entries,
        "probe_entry_points": probe_entries,
        "product_closure": sorted(closure),
        "research_passengers": sorted(passengers),
        "probe_only_closure": sorted(probe_closure),
        "shared_statics": shared,
        "product_api_usage": {k: sorted(set(v)) for k, v in sorted(usage.items())},
    }, ensure_ascii=False, indent=2), encoding="utf-8")

    lines = []
    A = lines.append
    A("# Карта слоёв `src/devtools/DevTools.cpp`")
    A("")
    A("> Сгенерировано `tools/analyze_devtools_layers.py`. Руками не править.")
    A("")
    A(f"Файл: **{raw.count(chr(10)) + 1} строк**, разобрано **{len(funcs)} функций** "
      f"верхнего уровня (**{total} строк** в телах), **{len(statics)} файловых статиков**.")
    A("")
    A("## Слои")
    A("")
    A("| Слой | Что это | Функций | Строк |")
    A("|---|---|---:|---:|")
    meaning = {
        "PRODUCT-API": "зовётся продуктовыми модулями напрямую → `src/runtime/`",
        "PRODUCT-DEP": "нужна продукту транзитивно → `src/runtime/`",
        "PASSENGER": "research-дамп в продуктовом тике → место разреза, в runtime не едет",
        "PROBE-API": "research-кнопка в продуктовом UI → вырезать из `PawnAI.cpp`",
        "PROBE-DEP": "живёт только ради проб → уйдёт вместе с ними",
        "RESEARCH": "пробы/аудиты/дампы → под `researchDump` или под нож",
        "DEVTOOLS": "инфраструктура DevTools → остаётся в `src/devtools/`",
    }
    for layer in ("PRODUCT-API", "PRODUCT-DEP", "PASSENGER", "PROBE-API", "PROBE-DEP",
                  "RESEARCH", "DEVTOOLS"):
        d = by_layer[layer]
        A(f"| `{layer}` | {meaning[layer]} | {d['funcs']} | {d['lines']} |")
    A("")
    prod_lines = by_layer["PRODUCT-API"]["lines"] + by_layer["PRODUCT-DEP"]["lines"]
    n_prod = by_layer["PRODUCT-API"]["funcs"] + by_layer["PRODUCT-DEP"]["funcs"]
    if n_prod:
        A(f"> **НАРУШЕНИЕ СЛОЁВ.** В `DevTools.cpp` снова завёлся продуктовый код: "
          f"{n_prod} функций, {prod_lines} строк. Продукт должен жить в `src/runtime/`.")
    else:
        A("> **Слои чистые.** Продуктового кода в `DevTools.cpp` нет: продукт "
          "живёт в `src/runtime/` и работает при `[devtools] enabled = off`. "
          "Этот файл — исследовательский инструмент, его можно отключить целиком.")
    probe_lines = by_layer["PROBE-API"]["lines"] + by_layer["PROBE-DEP"]["lines"]
    A("")
    A(f"**Исчезает вместе с пробами: {probe_lines} строк** "
      f"({by_layer['PROBE-API']['funcs'] + by_layer['PROBE-DEP']['funcs']} функций) — "
      "по таблице судьбы проб в `REFACTOR_TASK.md` §4.")
    A("")
    A("## Пассажиры: research-код в продуктовом тике")
    A("")
    A("Эти функции физически лежат в продуктовом пути вызовов, но по смыслу — "
      "исследовательские дампы (их зовут под `g_researchDump` / `g_intentTrace`). "
      "При распиле разрез идёт ПО НИМ: в `src/runtime/` они не едут, остаются в DevTools "
      "и вызываются из runtime через узкий хук.")
    A("")
    A("| Функция | Строки | Размер | Кто зовёт из продуктового пути |")
    A("|---|---|---:|---|")
    callers_of = defaultdict(set)
    for fn in funcs:
        for c in fn["calls"]:
            callers_of[c].add(fn["name"])
    for nm in sorted(passengers, key=lambda x: -sum(g["lines"] for g in names[x])):
        fn = names[nm][0]
        cl = ", ".join(f"`{c}`" for c in sorted(callers_of[nm]) if c in closure or c in entries)
        A(f"| `{nm}` | {fn['line_start']}–{fn['line_end']} | {fn['lines']} | {cl or '—'} |")
    A("")
    A("## Research-кнопки в продуктовом UI (нарушение слоёв)")
    A("")
    A("`src/PawnAI.cpp` рисует кнопки проб прямо в продуктовой панели. "
      "Из-за этого продукт формально зависит от research-кода. Вырезать первым делом.")
    A("")
    A("| `DevTools::` | Строки | Зовёт | Судьба |")
    A("|---|---|---|---|")
    for e in probe_entries:
        f = names[e][0]
        A(f"| `{e}` | {f['line_start']}–{f['line_end']} | {', '.join(sorted(set(usage[e])))} "
          f"| {PROBE_ENTRY_POINTS[e]} |")
    A("")
    A("## Точки входа продукта")
    A("")
    A("| `DevTools::` | Строки | Кто зовёт |")
    A("|---|---|---|")
    for e in entries:
        f = names[e][0]
        A(f"| `{e}` | {f['line_start']}–{f['line_end']} | {', '.join(sorted(set(usage[e])))} |")
    missing = sorted(a for a in usage if a not in names)
    if missing:
        A("")
        A(f"Объявлены в `DevTools.h`, но определены не в `DevTools.cpp`: "
          f"{', '.join('`' + m + '`' for m in missing)}.")
    A("")
    A("## Общие статики (главный тормоз распила)")
    A("")
    if shared:
        A("Эти переменные читает и продуктовое замыкание, и devtools/research-слой. "
          "Каждая — либо переезжает в состояние `runtime`-модуля с геттером, либо "
          "остаётся в DevTools и получает явный вызов из runtime.")
        A("")
        A("| Статик | Продуктовые функции | Прочие функции |")
        A("|---|---|---|")
        for g in shared:
            p = ", ".join(f"`{x}`" for x in sorted(g_product[g])[:4])
            o = ", ".join(f"`{x}`" for x in sorted(g_other[g])[:4])
            if len(g_product[g]) > 4:
                p += f" +{len(g_product[g]) - 4}"
            if len(g_other[g]) > 4:
                o += f" +{len(g_other[g]) - 4}"
            A(f"| `{g}` | {p} | {o} |")
    else:
        A("Нет — распил чистый.")
    A("")
    A("## Полный разбор по функциям")
    A("")
    A("| Строки | Размер | Слой | Функция | Зовёт | Статиков |")
    A("|---:|---:|---|---|---|---:|")
    for f in sorted(funcs, key=lambda x: x["line_start"]):
        q = f"{f['qualifier']}::" if f["qualifier"] else ""
        calls = ", ".join(f"`{c}`" for c in f["calls"][:5])
        if len(f["calls"]) > 5:
            calls += f" +{len(f['calls']) - 5}"
        A(f"| {f['line_start']}–{f['line_end']} | {f['lines']} | `{f['layer']}` | "
          f"`{q}{f['name']}` | {calls or '—'} | {len(f['globals'])} |")
    A("")

    # --- предлагаемая раскладка по файлам src/runtime/ ---
    buckets = defaultdict(list)
    for f in funcs:
        if not f["layer"].startswith("PRODUCT"):
            continue
        for bucket, rx in RUNTIME_BUCKETS:
            if re.match(rx, f["name"]):
                buckets[bucket].append(f)
                break
        else:
            buckets["(не разложено)"].append(f)

    A("## Предлагаемая раскладка `src/runtime/`")
    A("")
    A("Автогруппировка продуктового кода по именам. Это заготовка шагов 1–3 "
      "`REFACTOR_TASK.md`, а не догма — спорные функции видно в таблице ниже.")
    A("")
    A("| Файл | Функций | Строк | Что внутри |")
    A("|---|---:|---:|---|")
    order = ["WorldScan", "PartyRecon", "PriorityPlatform", "MemProbe", "(не разложено)"]
    desc = {
        "WorldScan": "обход списка актёров, враги, live-состояния, публикация WorldReport",
        "PartyRecon": "uPlayer/uCmc, тела партии, позиции, боевая цель",
        "PriorityPlatform": "транзакционные priority-профили + Guardian-фикс",
        "MemProbe": "низкоуровневое чтение памяти, секции образа, DTI/TypeAtlas — "
                    "общий фундамент, нужен и DevTools",
        "(не разложено)": "разложить вручную",
    }
    for b in order:
        if b not in buckets:
            continue
        fs = buckets[b]
        A(f"| `{b}` | {len(fs)} | {sum(x['lines'] for x in fs)} | {desc[b]} |")
    A("")
    for b in order:
        if b not in buckets:
            continue
        fs = sorted(buckets[b], key=lambda x: -x["lines"])
        A(f"<details><summary><b>{b}</b> — "
          f"{len(fs)} функций, {sum(x['lines'] for x in fs)} строк</summary>")
        A("")
        A("| Строки | Размер | Функция |")
        A("|---:|---:|---|")
        for f in fs:
            A(f"| {f['line_start']}–{f['line_end']} | {f['lines']} | `{f['name']}` |")
        A("")
        A("</details>")
        A("")

    out_md = ROOT / "docs" / "DEVTOOLS_LAYER_MAP.md"
    out_md.write_text("\n".join(lines), encoding="utf-8")

    print(f"функций: {len(funcs)}   статиков: {len(statics)}")
    for layer in ("PRODUCT-API", "PRODUCT-DEP", "PASSENGER", "PROBE-API", "PROBE-DEP",
                  "RESEARCH", "DEVTOOLS"):
        d = by_layer[layer]
        print(f"  {layer:<12} {d['funcs']:>4} функций  {d['lines']:>6} строк")
    print(f"общих статиков: {len(shared)}")
    print(f"-> {out_md.relative_to(ROOT)}")
    print(f"-> {out_json.relative_to(ROOT)}")

    violations = by_layer["PRODUCT-API"]["funcs"] + by_layer["PRODUCT-DEP"]["funcs"]
    print()
    if violations:
        print("=" * 68)
        print(f"НАРУШЕНИЕ СЛОЁВ: продуктовый код снова зовёт DevTools "
              f"({violations} функций, {prod_lines} строк).")
        print("Точки входа:", ", ".join(entries))
        print("Продукт обязан жить в src/runtime/ и работать при "
              "[devtools] enabled = off.")
        print("=" * 68)
        return 1
    print("Слои чистые: DevTools.cpp не содержит продуктового кода.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

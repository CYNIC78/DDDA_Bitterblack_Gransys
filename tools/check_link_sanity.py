#!/usr/bin/env python3
"""
«Линкер бедняка» — статическая проверка дерева без MSVC.

Что ловит (то, на чём реально горит рефакторинг в чужой ОС):
  1. дисбаланс скобок в каждом .cpp/.h;
  2. вызов функции, определение/объявление которой нигде не найдено;
  3. использование глобала `g_*`, который нигде не определён;
  4. одноимённые определения функций в разных единицах трансляции
     (кандидат на LNK2005 duplicate symbol);
  5. функция объявлена в .h, но не определена ни в одном .cpp (LNK2019);
  6. файл в дереве, но не в .vcxproj (не попадёт в сборку).

Не заменяет компилятор: типы и перегрузки не проверяются.
Задача — поймать грубые ошибки переноса кода между файлами.

Запуск:  python3 tools/check_link_sanity.py
Код возврата: 0 — чисто, 1 — есть ошибки.
"""
from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {"ImGui", "MinHook", ".git", "docs", "tools", "resources"}

CXX_KEYWORDS = {
    "if", "for", "while", "switch", "return", "else", "do", "case", "sizeof",
    "catch", "new", "delete", "static_cast", "reinterpret_cast", "const_cast",
    "dynamic_cast", "defined", "and", "or", "not", "try", "throw", "using",
    "namespace", "struct", "class", "union", "enum", "typedef", "template",
    "operator", "explicit", "friend", "virtual", "inline", "constexpr",
    "alignof", "decltype", "noexcept", "static_assert", "__try", "__except",
    "__finally", "__leave", "true", "false", "nullptr", "this",
}


def strip_noise(text: str) -> str:
    out, i, n, state = [], 0, len(text), "code"
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
            out.append("\n" if c == "\n" else " ")
            if c == "\n":
                state = "code"
            i += 1
        elif state == "block":
            if c == "*" and nx == "/":
                state, i = "code", i + 2
                out.append("  ")
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
        else:
            if c == "\\":
                out.append("  ")
                i += 2
                continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = "code"
            out.append("\n" if c == "\n" else " ")
            i += 1
    return "".join(out)


def namespace_at(clean: str, pos: int) -> str:
    """Имя объемлющего namespace для позиции pos (грубо, по глубине скобок)."""
    stack = []
    depth = 0
    for m in re.finditer(r"namespace\s+([A-Za-z_]\w*)\s*\{|\{|\}", clean[:pos]):
        tok = m.group(0)
        if tok.startswith("namespace"):
            stack.append((depth, m.group(1)))
            depth += 1
        elif tok == "{":
            depth += 1
        else:
            depth -= 1
            while stack and stack[-1][0] >= depth:
                stack.pop()
    return "::".join(n for _, n in stack)


def sources():
    for pat in ("*.cpp", "*.h", "src/**/*.cpp", "src/**/*.h", "src/**/*.inl"):
        for f in ROOT.glob(pat):
            if any(part in SKIP_DIRS for part in f.relative_to(ROOT).parts):
                continue
            yield f


DEF_RE = re.compile(
    r"(?:^|[\s*&>])(?:(?P<ns>[A-Za-z_]\w*)\s*::\s*)?(?P<name>[A-Za-z_]\w*)\s*"
    r"\((?P<args>[^;{}()]*)\)\s*(?:const\s*)?(?:noexcept\s*)?\{",
    re.M,
)
DECL_RE = re.compile(
    r"(?:^|[\s*&>])(?P<name>[A-Za-z_]\w*)\s*\([^;{}()]*\)\s*(?:const\s*)?"
    r"(?:noexcept\s*)?;",
    re.M,
)
CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
# Объявление глобалов: ловим ВСЕ декларторы в одном операторе,
# например  static float g_x = 0, g_y = 0, g_z = 0;
GLOBAL_STMT_RE = re.compile(
    r"^[ \t]*(?:static|extern)[ \t]+[^;()\n][^;]*?;",
    re.M,
)
GLOBAL_NAME_RE = re.compile(r"\b(g_\w+)\b")
GLOBAL_USE_RE = re.compile(r"\b(g_\w+)\b")




def _strip_comments_only(text: str) -> str:
    """Убрать // и /* */, сохранив содержимое строковых литералов."""
    out, i, n, state = [], 0, len(text), "code"
    while i < n:
        c = text[i]
        nx = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nx == "/":
                state, i = "line", i + 2
                continue
            if c == "/" and nx == "*":
                state, i = "block", i + 2
                continue
            if c == '"':
                state = "str"
            out.append(c)
            i += 1
        elif state == "str":
            out.append(c)
            if c == "\\":
                if i + 1 < n:
                    out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                state = "code"
            i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append(c)
            i += 1
        else:  # block
            if c == "*" and nx == "/":
                state, i = "code", i + 2
                continue
            if c == "\n":
                out.append(c)
            i += 1
    return "".join(out)


def _includes_of(path):
    """Прямые включения файла, только внутрипроектные."""
    import re as _re, os
    try:
        txt = open(path, encoding='utf-8', errors='replace').read()
    except Exception:
        return []
    # Комментарии убираем, СТРОКИ ОСТАВЛЯЕМ.
    #
    # Здесь проверка дважды обманула сама себя: сперва не убирала
    # комментарии (и закомментированный include считался включённым),
    # потом взяла общий strip_noise — а тот затирает и строковые
    # литералы, то есть съедает сам путь из #include "...".
    txt = _strip_comments_only(txt)
    out = []
    base = os.path.dirname(str(path))
    for m in _re.finditer(r'#\s*include\s+"([^"]+)"', txt):
        rel = m.group(1)
        # Пути ищем так же, как их видит компилятор: рядом с файлом,
        # от корня репозитория и от src (он в путях включения проекта).
        for cand in (os.path.normpath(os.path.join(base, rel)),
                     os.path.normpath(os.path.join(str(ROOT), rel)),
                     os.path.normpath(os.path.join(str(ROOT), 'src', rel))):
            if os.path.isfile(cand):
                out.append(cand)
                break
    return out


def _transitive_includes(path, seen=None):
    """Все заголовки, которые файл видит — прямо или через цепочку."""
    if seen is None:
        seen = set()
    for inc in _includes_of(path):
        if inc in seen:
            continue
        seen.add(inc)
        _transitive_includes(inc, seen)
    return seen


def check_runtime_symbols(files):
    """Вызов Runtime::Foo() должен быть ВИДЕН из этого файла.

    Добавлено после ошибки сборки «CrtInvalidParamCount не является членом
    Runtime». Функция была объявлена в публичном заголовке рантайма, а
    модуль включал только внутренний — и заметил это MSVC, а не мы.

    Проверка идёт по графу включений: собираем имена, объявленные в
    каждом заголовке src/runtime/, и требуем, чтобы нужный заголовок был
    достижим из файла хотя бы по цепочке.
    """
    import re as _re, os
    hdr_names = {}
    for h in (ROOT / 'src' / 'runtime').glob('*.h'):
        txt = h.read_text(encoding='utf-8', errors='replace')
        names = set(_re.findall(r'([A-Za-z_]\w*)\s*\(', txt))
        names |= set(_re.findall(r'\b(?:struct|class|enum)\s+([A-Za-z_]\w*)', txt))
        hdr_names[os.path.normpath(str(h))] = names

    errs = []
    for path in files:
        path = str(path)
        if not path.endswith('.cpp'):
            continue
        if os.path.normpath(path).startswith(os.path.normpath(str(ROOT / 'src' / 'runtime'))):
            continue                      # рантайм проверяет себя сам
        try:
            txt = open(path, encoding='utf-8', errors='replace').read()
        except Exception:
            continue
        calls = set(_re.findall(r'\bRuntime::(?:\w+::)*([A-Za-z_]\w*)\s*\(', txt))
        if not calls:
            continue

        visible = set()
        for inc in _transitive_includes(path):
            visible |= hdr_names.get(os.path.normpath(inc), set())

        for name in sorted(calls):
            if name in visible:
                continue
            where = [os.path.basename(h) for h, n in hdr_names.items() if name in n]
            if where:
                errs.append('[сборка] %s: Runtime::%s() объявлена в %s, но этот '
                            'заголовок отсюда не виден' %
                            (os.path.relpath(path, str(ROOT)), name, ', '.join(where)))
            else:
                errs.append('[сборка] %s: Runtime::%s() нет ни в одном заголовке '
                            'src/runtime/' % (os.path.relpath(path, str(ROOT)), name))
    return errs


def main() -> int:
    errors, warnings = [], []
    proj_text = (ROOT / "ddda-ai-overhaul.vcxproj").read_text(encoding="utf-8", errors="replace")
    texts = {}

    # --- 0. кто зовёт Runtime::, тот включает его заголовок ---
    errors.extend(check_runtime_symbols([str(f) for f in sorted(sources())]))

    # --- 1. скобки ---
    for f in sorted(sources()):
        raw = f.read_text(encoding="utf-8", errors="replace")
        clean = strip_noise(raw)
        texts[f] = clean
        rel = f.relative_to(ROOT)
        for op, cl, nm in (("{", "}", "фигурных"), ("(", ")", "круглых"), ("[", "]", "квадратных")):
            if clean.count(op) != clean.count(cl):
                errors.append(f"[скобки] {rel}: {nm} {clean.count(op)} против {clean.count(cl)}")

    # --- сбор определений/объявлений ---
    defs = defaultdict(list)     # имя -> [файл]
    static_defs = set()          # (имя, файл) для static-функций
    decls = defaultdict(list)
    globals_def = defaultdict(list)
    for f, clean in texts.items():
        rel = f.relative_to(ROOT)
        for m in DEF_RE.finditer(clean):
            name = m.group("name")
            if name in CXX_KEYWORDS:
                continue
            enclosing = namespace_at(clean, m.start())
            if m.group("ns"):
                qual = f'{m.group("ns")}::{name}'
            elif enclosing:
                qual = f"{enclosing}::{name}"
            else:
                qual = name
            defs[name].append(rel)
            if qual != name:
                defs.setdefault(qual, []).append(rel)
            # static-функции и функции в namespace линкеру снаружи не мешают
            head = clean[max(0, m.start() - 40):m.start()]
            if "static" in head.split("\n")[-1] or enclosing:
                static_defs.add((name, str(rel)))
        for m in DECL_RE.finditer(clean):
            name = m.group("name")
            if name in CXX_KEYWORDS:
                continue
            decls[name].append(rel)
        for m in GLOBAL_STMT_RE.finditer(clean):
            for gm in GLOBAL_NAME_RE.finditer(m.group(0)):
                globals_def[gm.group(1)].append(rel)
        # структурные поля/локальные static внутри функций тоже считаем определением
        for gm in re.finditer(r"\b(g_\w+)\s*(?:=|\[[^\]]*\]\s*=|;)", clean):
            globals_def.setdefault(gm.group(1), []).append(rel)

    known = set(defs) | set(decls)

    # --- 2. вызовы в пустоту (только внутри src/, чужие API игнорируем) ---
    external = re.compile(
        r"^(Rd|std|ImGui|Im|Win|Get|Set|Create|Read|Write|Close|Load|Free|Virtual|"
        r"Interlocked|lstr|str|mem|sprintf|swprintf|snprintf|printf|fprintf|fopen|"
        r"fclose|fputs|fgets|_|is|to|abs|fabs|sqrt|sin|cos|atan|floor|ceil|pow|min|max|"
        r"MH_|D3D|IDirect|Sleep|Query|Output|Message|Module|Process|Thread|Exception)")
    for f, clean in texts.items():
        if f.suffix != ".cpp":
            continue
        rel = f.relative_to(ROOT)
        for m in CALL_RE.finditer(clean):
            name = m.group(1)
            if name in CXX_KEYWORDS or name in known or external.match(name):
                continue
            # пропускаем вызовы через ::  и .  и ->
            pre = clean[max(0, m.start() - 2):m.start()]
            if pre.endswith((":", ".", ">")):
                continue
            if name[0].isupper() and name.isupper():
                continue          # макросы-константы
            if name in ("void", "char", "int", "float", "bool", "double", "long",
                        "short", "unsigned", "signed", "const"):
                continue          # указатели на функции в typedef
            line = clean[:m.start()].count("\n") + 1
            warnings.append(f"[вызов?] {rel}:{line} -> {name}()")

    # --- 3. глобалы в пустоту ---
    for f, clean in texts.items():
        rel = f.relative_to(ROOT)
        for m in GLOBAL_USE_RE.finditer(clean):
            g = m.group(1)
            if g in globals_def:
                continue
            line = clean[:m.start()].count("\n") + 1
            errors.append(f"[глобал] {rel}:{line} -> {g} нигде не определён")

    # --- 4. дубли определений между .cpp ---
    for name, files in sorted(defs.items()):
        if "::" in name:
            continue                       # методы классов разводятся квалификатором
        cpps = [f for f in files if f.suffix == ".cpp"]
        uniq = {str(f) for f in cpps}
        # выкидываем те, что объявлены static в своей единице трансляции
        uniq = {u for u in uniq if (name, u) not in static_defs}
        if len(uniq) > 1:
            # метод класса, определённый как Class::name — не конфликт
            if any(f"::{name}" in k for k in defs if k.endswith(f"::{name}")):
                continue
            errors.append(f"[дубль] {name}() определена в: {', '.join(sorted(uniq))}")

    # --- 5. объявлено в .h, не определено нигде ---
    for name, files in sorted(decls.items()):
        if name in defs:
            continue
        hs = [f for f in files if f.suffix == ".h"]
        if hs and name not in CXX_KEYWORDS:
            warnings.append(f"[нет тела?] {name}() объявлена в {hs[0]}, определения не найдено")

    # --- 5b. видимость глобалов внутри единицы трансляции ---
    # Ловит главную ошибку переноса кода между файлами: функция уехала,
    # а глобал, который она читает, остался в старом .cpp.
    def includes_of(f: Path, seen=None):
        seen = seen or set()
        res = set()
        for m in re.finditer(r'#include\s+"([^"]+)"', f.read_text(encoding="utf-8", errors="replace")):
            for cand in (f.parent / m.group(1), ROOT / m.group(1), ROOT / "src" / m.group(1)):
                cand = cand.resolve()
                if cand.exists() and cand not in seen:
                    seen.add(cand)
                    res.add(cand)
                    res |= includes_of(cand, seen)
                    break
        return res

    # Проверяем ВСЕ .cpp, а не только runtime/.
    #
    # Раньше проверка ограничивалась каталогом runtime — и пропустила
    # обращение к Runtime::g_act без квалификации из devtools/AnimProbe.cpp.
    # Заодно теперь учитывается пространство имён: глобал, объявленный
    # внутри `namespace Runtime`, виден без квалификации только если
    # файл открыл это пространство через using или свой блок namespace.
    for f, clean in texts.items():
        if f.suffix != ".cpp":
            continue
        visible = set()
        # Многодеклараторные строки (`static int a = 0, b = 0;`) раньше давали
        # ложные срабатывания: видели только первое имя. Берём ВСЕ имена из
        # операторов объявления, не пересекая границу тела функции.
        own = set()
        for stmt in re.finditer(r"^[ \t]*(?:static|extern)?[ \t]*[A-Za-z_][\w:<>,\s\*&]*?;",
                                clean, re.M):
            body = stmt.group(0)
            if "(" in body.split("=")[0]:
                continue
            own |= {g.group(1) for g in GLOBAL_NAME_RE.finditer(body)}
        own |= {m.group(1) for m in re.finditer(
            r"\b(g_\w+)\s*(?:=|(?:\[[^\]]*\])+\s*[=;])", clean)}
        visible |= own
        # какие пространства имён открыты в этом .cpp
        opened_ns = {m.group(1) for m in re.finditer(r"using\s+namespace\s+([\w:]+)\s*;", clean)}
        opened_ns |= {m.group(1) for m in re.finditer(r"^namespace\s+([A-Za-z_]\w*)\s*\{", clean, re.M)}
        # вложенные: using namespace Runtime::Mem открывает и Mem
        for ns in list(opened_ns):
            for part in ns.split("::"):
                opened_ns.add(part)

        for inc in includes_of(f):
            itext = strip_noise(inc.read_text(encoding="utf-8", errors="replace"))
            for m in re.finditer(r"\b(g_\w+)\b", itext):
                ns = namespace_at(itext, m.start())
                if not ns or ns.split("::")[0] in opened_ns:
                    visible.add(m.group(1))
        for m in GLOBAL_NAME_RE.finditer(clean):
            if m.group(1) in visible:
                continue
            # Обращение уже квалифицировано (Runtime::g_act) — вопрос снят.
            if clean[max(0, m.start() - 2):m.start()] == "::":
                continue
            line = clean[:m.start()].count("\n") + 1
            errors.append(f"[видимость] {f.relative_to(ROOT)}:{line} -> {m.group(1)} "
                          f"не виден без квалификации: он объявлен в чужом "
                          f"пространстве имён или не объявлен вовсе")

    # --- 5c. самодостаточность заголовков рантайма ---
    # Ловит ошибку Build 69: константа размера осталась в .cpp, а структура,
    # которая ей объявлена, уехала в заголовок -> C2065 во всех TU сразу.
    # Проверяются ВСЕ идентификаторы заголовка, включая тела структур.
    WIN_KNOWN = set("""void bool char int float double long short unsigned signed const
    static extern struct union class enum typedef namespace using return if else for while do
    switch case break continue sizeof true false nullptr new delete public private protected
    inline virtual template typename operator this explicit friend mutable volatile auto
    size_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t uintptr_t intptr_t
    BYTE WORD DWORD DWORD64 UINT UINT16 UINT32 UINT64 INT LONG ULONG BOOL CHAR WCHAR LPCSTR
    LPSTR LPVOID LPCVOID HANDLE HMODULE HWND FILE va_list wchar_t MAX_PATH pragma once include
    windows stdint h IMAGE_DOS_HEADER IMAGE_NT_HEADERS IMAGE_SECTION_HEADER PIMAGE_DOS_HEADER
    PIMAGE_NT_HEADERS std Runtime Mem ActMap TypeAtlas""".split())

    for hf in sorted((ROOT / "src/runtime").glob("*.h")):
        htext = strip_noise(hf.read_text(encoding="utf-8", errors="replace"))
        declared = set(WIN_KNOWN)
        declared |= {m.group(2) for m in re.finditer(r"\b(struct|union|enum|class)\s+(\w+)", htext)}
        declared |= {m.group(1) for m in re.finditer(
            r"^[A-Za-z_][\w:\*&<>\s]*?\b(\w+)\s*\([^;{}]*\)\s*;", htext, re.M)}
        declared |= {m.group(1) for m in re.finditer(r"\b(\w+)\s*(?:\[[^\]]*\])?\s*=", htext)}
        declared |= {m.group(1) for m in re.finditer(r"\b(g_\w+)\b", htext)}
        # поля структур и имена параметров объявлениями не считаем ошибкой
        for m in re.finditer(r"\{([^{}]*)\}", htext, re.S):
            declared |= {mm.group(1) for mm in re.finditer(r"\b(\w+)\s*(?:\[[^\]]*\])?\s*[;,]", m.group(1))}
        for m in re.finditer(r"\(([^()]*)\)", htext):
            declared |= {mm.group(1) for mm in re.finditer(r"\b(\w+)\s*(?:,|$)", m.group(1))}
        for inc in includes_of(hf):
            itext = strip_noise(inc.read_text(encoding="utf-8", errors="replace"))
            declared |= {m.group(2) for m in re.finditer(r"\b(struct|union|enum|class)\s+(\w+)", itext)}
            declared |= {m.group(1) for m in re.finditer(r"\b(\w+)\s*(?:\[[^\]]*\])?\s*=", itext)}
            declared |= {m.group(1) for m in re.finditer(r"\b(g_\w+|k[A-Z]\w*)\b", itext)}
            declared |= {m.group(1) for m in re.finditer(
                r"^[A-Za-z_][\w:\*&<>\s]*?\b(\w+)\s*\([^;{}]*\)\s*;", itext, re.M)}
        # интересуют только «константы размеров» и типы: они ломают сборку молча
        for m in re.finditer(r"\[\s*([A-Za-z_]\w*)\s*\]", htext):
            if m.group(1) not in declared:
                line = htext[:m.start()].count("\n") + 1
                errors.append(f"[заголовок] {hf.relative_to(ROOT)}:{line} -> размер массива "
                              f"{m.group(1)} нигде не объявлен (C2065 во всех TU)")

    # --- 5d. затенение системных заголовков ---
    # Build 69: собственный d3d9.h проекта затенял системный <d3d9.h>. В Debug
    # каталог проекта стоял в путях поиска первым, заголовок включал сам себя,
    # #pragma once гасил повтор — и конфигурация Debug не собиралась вообще.
    SYSTEM_HEADERS = {
        "windows.h", "d3d9.h", "d3d9caps.h", "d3dx9.h", "dinput.h", "dinput8.h",
        "dsound.h", "xinput.h", "stdio.h", "stdlib.h", "string.h", "math.h",
        "time.h", "memory.h", "assert.h", "stdint.h", "process.h", "io.h",
        "psapi.h", "tlhelp32.h", "shlobj.h", "commctrl.h", "gdiplus.h",
    }
    own_headers = {f.name.lower(): f for f in sources() if f.suffix == ".h"}
    for name, f in sorted(own_headers.items()):
        if name not in SYSTEM_HEADERS:
            continue
        rel = f.relative_to(ROOT)
        self_inc = re.search(rf"#include\s*<\s*{re.escape(name)}\s*>",
                             f.read_text(encoding="utf-8", errors="replace"), re.I)
        if self_inc:
            errors.append(f"[затенение] {rel} называется как системный <{name}> И включает его "
                          f"через <>. Если каталог проекта окажется в путях поиска раньше SDK, "
                          f"заголовок включит сам себя. Переименуй файл.")
        else:
            warnings.append(f"[затенение] {rel} называется как системный <{name}> — "
                            f"любой <{name}> в проекте может попасть в него вместо SDK")

    # порядок путей поиска в .vcxproj: SDK должен стоять раньше каталога проекта
    for m in re.finditer(r"<AdditionalIncludeDirectories>([^<]*)</AdditionalIncludeDirectories>", proj_text):
        dirs = [d.strip() for d in m.group(1).split(";") if d.strip()]
        try:
            i_proj = next(i for i, d in enumerate(dirs) if d.rstrip("\\/") == "$(ProjectDir)")
            i_sdk = next(i for i, d in enumerate(dirs) if "DXSDK" in d or "DirectX SDK" in d)
        except StopIteration:
            continue
        if i_proj < i_sdk:
            errors.append("[пути] в .vcxproj $(ProjectDir) стоит раньше каталога DirectX SDK — "
                          "системные <d3d9.h>/<dinput8.h> будут перехвачены заголовками проекта")

    # --- 5e. вложенные пространства имён без квалификации ---
    # Ловит ошибку Build 70.3: AnimProbe объявлен как DevTools::AnimProbe,
    # а вызывался из DevTools.cpp просто как AnimProbe:: — файл не находится
    # внутри namespace DevTools, поэтому имя не разрешается (C2653).
    nested = {}           # короткое имя -> полное
    for hf in sorted(sources()):
        if hf.suffix != ".h":
            continue
        htext = strip_noise(hf.read_text(encoding="utf-8", errors="replace"))
        stack = []
        for m in re.finditer(r"namespace\s+([A-Za-z_]\w*)\s*\{|\{|\}", htext):
            tok = m.group(0)
            if tok.startswith("namespace"):
                stack.append(m.group(1))
                if len(stack) > 1:
                    nested.setdefault(stack[-1], "::".join(stack))
            elif tok == "{":
                stack.append(None)
            else:
                if stack:
                    stack.pop()

    for f, clean in texts.items():
        if f.suffix != ".cpp":
            continue
        rel = f.relative_to(ROOT)
        # какие пространства имён открыты через using в этом файле
        opened = {m.group(1) for m in re.finditer(r"using\s+namespace\s+([\w:]+)\s*;", clean)}
        # и внутри каких namespace-блоков объявлен сам файл
        own_ns = {m.group(1) for m in re.finditer(r"^namespace\s+([A-Za-z_]\w*)\s*\{", clean, re.M)}
        for m in re.finditer(r"(?<![:\w])([A-Za-z_]\w*)\s*::", clean):
            name = m.group(1)
            full = nested.get(name)
            if not full or full.endswith("::" + name) is False:
                continue
            outer = full.rsplit("::", 1)[0]
            if outer in opened or outer in own_ns or full in opened:
                continue
            # уже написано полным путём?
            start = max(0, m.start() - len(outer) - 2)
            if clean[start:m.start()].endswith(outer + "::"):
                continue
            line = clean[:m.start()].count("\n") + 1
            errors.append(f"[namespace] {rel}:{line} -> {name}:: объявлен как {full}, "
                          f"но {outer} здесь не открыт (C2653). Пиши {full}::")

    # --- 5f. кириллица в строковых литералах ---
    # ImGui 1.48 с дефолтным шрифтом рисует только ASCII: любая кириллица
    # в UI превращается в «???». Логи тоже держим на английском — в проекте
    # так было с самого начала, и это снимает вопрос кодировки при чтении
    # лога чужим редактором. Комментарии и документация остаются русскими.
    #
    # Исключение: DefaultEntitiesIni.h — это содержимое ini-файла, который
    # читает человек в текстовом редакторе, там кириллица уместна.
    CYRILLIC_OK = {"DefaultEntitiesIni.h"}
    cyr = re.compile(r"[\u0400-\u04FF]")
    for f in sorted(sources()):
        if f.suffix not in (".cpp", ".h") or f.name in CYRILLIC_OK:
            continue
        raw = f.read_text(encoding="utf-8", errors="replace")
        clean = strip_noise(raw)          # тут строки уже стёрты
        # ищем литералы: берём из сырого текста те позиции, где в clean пробелы
        for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', raw):
            if not cyr.search(m.group(1)):
                continue
            # комментарий? в clean на этом месте тоже пробелы, отличаем по строке
            line_start = raw.rfind("\n", 0, m.start()) + 1
            prefix = raw[line_start:m.start()]
            if "//" in prefix or "*" == prefix.strip()[:1]:
                continue
            line = raw[:m.start()].count("\n") + 1
            errors.append(f"[кириллица] {f.relative_to(ROOT)}:{line} -> строковый литерал "
                          f"с кириллицей: в ImGui станет «???». Вывод — только ASCII")

    # --- 6. файлы мимо .vcxproj ---
    listed = {m.group(1).replace("\\", "/") for m in re.finditer(r'Include="([^"]+)"', proj_text)}
    for f in sorted(sources()):
        rel = str(f.relative_to(ROOT))
        if rel not in listed:
            errors.append(f"[сборка] {rel} нет в ddda-ai-overhaul.vcxproj")

    print("=" * 68)
    if errors:
        print(f"ОШИБКИ ({len(errors)}):")
        for e in errors:
            print("  " + e)
    else:
        print("ОШИБОК НЕТ")
    if warnings:
        print(f"\nпредупреждения ({len(warnings)}):")
        for w in warnings[:40]:
            print("  " + w)
        if len(warnings) > 40:
            print(f"  ... ещё {len(warnings) - 40}")
    print("=" * 68)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

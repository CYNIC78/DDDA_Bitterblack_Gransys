#!/usr/bin/env python3
"""Готовит копию src/CombatIntel.cpp для разбора компилятором g++.

ЗАЧЕМ. В файле есть крошечные переходники на ассемблере MSVC
(`__declspec(naked)` + `__asm { pushad; call MarkPlayerAttack; popad; jmp oDmg1 }`).
Это синтаксис MSVC, g++ его не разберёт ни при каких ключах, а выкинуть из
проверки весь файл нельзя: в нём логика урона и проверка «свой или чужой».

Что делает: заменяет каждый такой переходник на пустую функцию с тем же именем
(имя нужно, потому что ниже на него ссылаются), остальное оставляет как есть.
Тела переходников — три строки ассемблера, проверять в них нечего.

Порождается ровно одна копия в /tmp; репозиторий не трогается.

Прецедент: tools/test_audio_redirect.sh делает то же самое через sed (SEH там).
"""
import re
import sys

SRC = "src/CombatIntel.cpp"
DST = sys.argv[1] if len(sys.argv) > 1 else "/tmp/synchk_combatintel.cpp"

# void __declspec(naked) NAME() { __asm { ... } }  ->  void NAME() {}
NAKED = re.compile(
    r"void\s+__declspec\(naked\)\s+(\w+)\(\)\s*\{\s*__asm\s*\{.*?\n\s*\}\s*\}",
    re.DOTALL)

with open(SRC, encoding="utf-8") as f:
    text = f.read()

stripped = []
def _sub(m):
    stripped.append(m.group(1))
    return "void %s() {}  /* проверка: ассемблер MSVC вырезан */" % m.group(1)

out = NAKED.sub(_sub, text)

# stdafx подключаем сами: копия лежит вне дерева исходников.
out = '#include "stdafx.h"\n' + out

with open(DST, "w", encoding="utf-8") as f:
    f.write(out)

print("prep_combatintel: переходников вырезано %d (%s) -> %s"
      % (len(stripped), ", ".join(stripped), DST))

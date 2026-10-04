#!/usr/bin/env bash
# Кто зовёт пакет сессии — тот подключает LogMemSession.h (85.68).
#
# ПОВОД. Сборка 85.67 упала в студии владельца: src/dinput8.cpp звал
# LogMem::SessionNote/SessionFlush, а объявления этих функций живут в
# src/runtime/LogMemSession.h, а НЕ в LogMem.h. Заголовок отдельный намеренно
# (см. его шапку), поэтому «виден через другой файл» для него не работает.
# Вызовы въехали в dinput8.cpp в 85.63 и ждали первой честной пересборки:
# шаг гейта 1r/10 копирует ВЫЗОВЫ в фикстуру, но не список включений файла —
# поэтому огрех не видели ни синтаксическая проверка, ни сетка.
#
# ЧТО БУДЕТ, ЕСЛИ УПАДЁТ: у того, кто соберёт DLL, MSVC скажет
#   error C2039: "SessionNote": не является членом "LogMem"
# Лечится одной строкой в названном файле: #include "runtime/LogMemSession.h".
#
# Вторая проверка сторожит саму причину отдельного заголовка: LogMemSession.h
# обязан оставаться «пустым» (никаких #include) — иначе его потянет фикстура и
# сборка тестов упадёт на расхождении типа logFile (std::ostream против ofstream).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

python3 - "$ROOT" <<'PY'
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
src = root / "src"

CONTRACT = "runtime/LogMemSession.h"
CALL = re.compile(r"\bLogMem::Session(?:Note|NoteRollback|Flush)\s*\(")

callers = []
missing = []
for cpp in sorted(src.rglob("*.cpp")):
    text = cpp.read_text(encoding="utf-8", errors="replace")
    if not CALL.search(text):
        continue
    rel = cpp.relative_to(root).as_posix()
    callers.append(rel)
    includes = re.findall(r'#include\s+"([^"]+)"', text)
    if not any(i.endswith("LogMemSession.h") for i in includes):
        missing.append(rel)

if not callers:
    print("session include: НЕ НАЙДЕН НИ ОДИН зовущий LogMem::Session* —")
    print("  проверку пора переписать (вызовы переименовали?), иначе она сторожит воздух.")
    sys.exit(1)

if missing:
    print("session include: НАРУШЕНИЙ %d" % len(missing))
    for rel in missing:
        print("  ПЛОХО: %s зовёт LogMem::Session*, но не включает %s" % (rel, CONTRACT))
    print('  в студии это: error C2039: "SessionNote": не является членом "LogMem"')
    print('  лечится строкой: #include "runtime/LogMemSession.h"')
    sys.exit(1)

# Вторая проверка: у отдельного заголовка не должно быть своих включений.
hdr = root / "src" / "runtime" / "LogMemSession.h"
hinc = re.findall(r"^\s*#include\s+\S+", hdr.read_text(encoding="utf-8"), re.M)
if hinc:
    print("session include: НАРУШЕНИЕ — LogMemSession.h начал подключать файлы:")
    for line in hinc:
        print("  " + line.strip())
    print("  причина отдельного заголовка — «две функции и ничего больше»; включение")
    print("  вернёт расхождение типа logFile в фикстурах гейта (см. шапку заголовка).")
    sys.exit(1)

print("  зовущих пакет: %d, все включают %s" % (len(callers), CONTRACT))
print("  LogMemSession.h по-прежнему без включений (2 функции и ничего больше)")
print("session include: PASS (%d зовущих, 0 без включения)" % len(callers))
PY

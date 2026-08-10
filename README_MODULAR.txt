=== DDDA Bitterblack Gransys — MODULAR REFACTOR 10.08.2026 ===

Привет! Это архив от твоего лид-програмиста (cpp).

Что внутри — пробили стену types.tsv и ввели шину:

[FRESH] types.tsv теперь работает на полную:
  - src/EnemyTypes.Generated.h — 110 типов (gid + vtableRVA), генерит python tools/generate_bestiary.py
  - src/Bestiary.Generated.h — 72 бестиария, 45/72 уже с gid из TSV
  - docs/TYPES_TSV_WALL_BREAKTHROUGH.md — вся магия как использовать адреса без ограничений

[FRESH] Модульность PawnAI (было 407 строк монолита → стало 117 строк оркестратор):
  - src/pawnai/PawnAI_Common.h — оффсеты и хелперы
  - src/pawnai/PresetManager.{h,cpp} — пресеты
  - src/pawnai/SanitaryCordon.{h,cpp} — санитарный кордон (слушает шину)
  - src/pawnai/SmartUtilitarian.{h,cpp} — ★ ПРИМЕР-ШАБЛОН для всех будущих модулей ★
  - src/pawnai/TacticalSwitch.{h,cpp} — авто-смена пресета
  - src/pawnai/PawnAI_BusOrchestrator.h — собирает модули в один Tick()
  - src/CombatBus.h — шина-тренер (мегафон), CombatIntel публикует, модули слушают
  - src/CombatIntel.cpp — теперь публикует CombatReport в шину
  - src/PawnAI.cpp — тонкий оркестратор (копируй SmartUtilitarian как шаблон для новых модулей)

Как добавить новый модуль (например SuperTactics):
  1. Скопируй src/pawnai/SmartUtilitarian.h/.cpp → MyModule.h/.cpp
  2. Добавь #include и поле в PawnAI_BusOrchestrator.h
  3. Всё! Он уже слушает CombatBus.

Сборка: открой ddda-ai-overhaul.vcxproj → Release Win32 → Ctrl+Shift+B → dinput8.dll в Release/

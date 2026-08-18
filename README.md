# DDDA AI Overhaul

Runtime AI platform for **Dragon's Dogma: Dark Arisen** (Steam/GOG, x86).

> Умеем изменить живую политику — делаем LIVE. Игровые архивы используются как каталог, а не как основной способ установки.

**Текущий milestone:** Build 69 — разделение слоёв: продуктовый рантайм
(`src/runtime/`) отделён от исследовательского DevTools и работает независимо
от него. Игровая вертикаль прежняя — Guardian Doctrine (ролевая матрица +
градиентная зона телохранителя).

## Что уже работает

### Main Pawn runtime AI

- dynamic `uPlayer/uCmc` discovery через DTI;
- current action/FSM observation;
- 83 pawn AI resources в offline catalog;
- 85 priority rows и 48 live buckets;
- personality/order modifiers;
- generalized persistent profiles из 0..48 exact rules;
- multi-rule transaction, readback, convergence и rollback;
- автоматическое повторное применение после загрузки мира;
- planner current code и indexed `PlanCtrl`;
- 352 weapon/action eligibility rows в generated CSV.

Priority profiles не запускают действие насильно. Они меняют штатный порядок намерений; GOAP, eligibility и FSM сохраняют право отказаться от физически невозможного действия.

### Existing modules

- Pawn inclination modules: presets, Acquisitor Manager, Smart Utilitarian, Tactical Switch, Guardian Doctrine;
- CombatIntel/CombatBus и bestiary mapping;
- Camera Plus: tactical free camera with optional player tracking, pause, party cam slider: Arisen <-> main pawn;
- WorldScan/PartyRecon/PriorityPlatform — продуктовый рантайм (`src/runtime/`);
- TypeAtlas/SCAN/DUMP/HUNT — исследовательская платформа (`src/devtools/`, отключаемая);
- experimental EnemyTuner.

## Важные ограничения

Build 63 — development milestone, не законченный пользовательский AI overhaul.

Что уже работает (Guardian Doctrine):
- трекинг игрока/пешки/врагов + диспозиция боя;
- ролевая матрица (вокация пешки × вокация игрока): Protector/Assault/Adaptive/…;
- градиентная зона телохранителя (preempt/melee) со снятием Guardian-штрафа
  на кинжалы (code 54) транзакционно;
- трёхсигнальный детектор боя (урон + боевые действия врагов + цель пешки).

Ещё не завершены:

- поводок (follow-дистанция) — роль-зависимый, НЕ найден (см. GUARDIAN_LEASH_MATRIX.md);
- semantic names всех priority codes (code 4/66 в техдолге);
- Файтер/Варриор (код меча/двуручника) — ждёт смены вокации;
- Nexus doctrine (anchor = выбранная пешка);
- monster priority/planner bridge.

## Сборка

1. Открыть `ddda-ai-overhaul.sln` или `.vcxproj` в Visual Studio.
2. Выбрать **Release | Win32**.
3. Build Solution.
4. Скопировать `dinput8.dll` и `ddda_ai_overhaul.ini` в папку с `DDDA.exe`.

Подробно (требования, установка в игру, разбор ошибок): [`docs/BUILD_INSTRUCTIONS_RU.md`](docs/BUILD_INSTRUCTIONS_RU.md).

Если уже используется другой `dinput8.dll`, его можно загрузить цепочкой через `loadLibrary` в ini.

## Основные файлы данных

```text
DDDA_AI_Overhaul/ddda_pawn_ai_profiles.ini
```

Priority sidecar schema v2 идентифицирует rule без transient address:

```text
sensor / code / category / objectId / extra / ruleIndex
```

Профиль проверяет все expected fields до записи и применяется транзакционно.

## Hotkeys

| Key | Action |
|---|---|
| F12 | открыть UI |
| F4 | Camera Plus |
| стрелки + PgUp/PgDn | free-fly camera |
| Num 0 | pause toggle |
| `-` | profile switch в исследовательских билдах |
| `=` / `+` | pawn AI snapshot в исследовательских билдах |

**F9 модом не используется** — у пользователя это сохранение игры.

## Слои

```text
src/runtime/    продукт: работает всегда, не зависит от DevTools
src/devtools/   исследование: выключается целиком через [devtools] enabled = off
```

Продукт не вызывает research напрямую — только через `Runtime::ResearchHooks`.
Инвариант проверяется `tools/analyze_devtools_layers.py` (код возврата 1 при нарушении).

## Архитектура AI

```text
Sensors / target selection
  → Priority policy
  → selected intent
  → GOAP planner
  → action eligibility
  → FSM/current Act
  → motion/collision/damage
```

Основной продуктовый слой — priority. GOAP рассматривается как место точечных статических исправлений, FSM — как хирургический уровень, а не способ прямого командования.

## Документация

| Документ | Назначение |
|---|---|
| [`docs/VISION.md`](docs/VISION.md) | замысел: три слоя и философия проекта |
| [`docs/README.md`](docs/README.md) | индекс документации |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | архитектура платформы |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | актуальный план |
| [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md) | подтверждённые контракты |
| [`docs/FIELD_MAP.md`](docs/FIELD_MAP.md) | offsets |
| [`docs/ASSET_FORMATS.md`](docs/ASSET_FORMATS.md) | игровые resource formats |
| [`docs/PLAYER_PAWN_WORK/`](docs/PLAYER_PAWN_WORK/) | Main Pawn vertical slice |
| [`CHANGELOG.md`](CHANGELOG.md) | мастер-индекс по дням (детали — `docs/changelog/`) |

Завершённые `TEST_*`, protocol и промежуточные result-документы удалены из текущего дерева; история остаётся в Git.

## Принципы безопасности

- не сохранять heap pointers;
- не менять `DDDA.sav`;
- не угадывать offsets;
- validate → write → readback → convergence → rollback;
- unknown version/object → vanilla fallback;
- `game_main.arc` repack — только резервный PACK-путь.

## Credits

- [Arena.ai](https://arena.ai) — ИИ-ассистент и лид-программист runtime-платформы;
- kubik-jaroslav — ddda-dinput8 architecture;
- Cielos — Cheat Engine research;
- Atvaark — DragonsDogma.Research/types;
- chrispurnell — pawn knowledge data;
- Lefein — World Difficulty and DDDA AI modding groundwork;
- FluffyQuack — ARCtool;
- TsudaKageyu — MinHook;
- ocornut — Dear ImGui.

MIT License.

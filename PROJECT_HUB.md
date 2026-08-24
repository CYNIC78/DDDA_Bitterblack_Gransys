# DDDA AI Overhaul — Project Hub

## Текущий milestone

**Build 84.18:** `card-recon` — универсальный card recon для любого вида (см. README).

**Предыдущий milestone:** Build 84.16 `dual-observe` — GOBCARD + PS,
read-only (лог 24: блок B goblin на волчьих оффсетах).

**Предыдущий milestone:** Build 84.15 `goblin-grab-hold` (`docs/GOBLIN_PACK_OBSERVE.md`):
grab держится через `cPlActHagaijime`, пустые карты `0/0/0/0` на
`+0x2FA0` будятся в `1/4 att=300 w=1.0`. Pin-only, Tempo owned=0.

**Предыдущий зафиксированный pawn milestone — Build 75.56:** `anodyne` — кастер пешки, AI range 15 м (`docs/WAND_RANGE.md`)

**Ветка разработки:** `work/player-main-pawn-recon`

Главный результат трека: рывок пешки приходит под кодом приоритета
1 (`Follow`), а у кодов рывка `84/85` нет ни одной строки приоритета.
Переключатель «бежать/рвануть» — внутри цели Follow, между моторными
командами `cCmcFollow` / `cCmcDashFollow`.
Подробно: `docs/PAWN_SPRINT_RECON.md` §26-27.

Предыдущий milestone (Build 63, `guardian-role-matrix`): Guardian Doctrine — ролевая матрица (вокация пешки × вокация
игрока), градиентная зона телохранителя со снятием Guardian-штрафа на кинжалы
(code 54) транзакционно, трёхсигнальный детектор боя. Поверх verified priority
platform (persistent generalized profiles, transaction, convergence, rollback).

## Структура проекта

```text
src/
  pawnai/                 продуктовые модули поведения пешки
  devtools/               DTI/WorldScan/research bridge
  CombatIntel.*           события боя и bestiary integration
  EnemyTuner.*            экспериментальные live enemy mutations
  CameraPlus.*            камера/пауза

tools/                    генераторы и offline parsers
resources/                extracted/reference game assets
docs/                     каноническая документация
  PLAYER_PAWN_WORK/       отдельный vertical slice Arisen/Main Pawn
builds/                   проверенные source packages
ddda_pawn_ai_profiles.ini generalized priority sidecar template
```

## Канонические документы

| Документ | Роль |
|---|---|
| `docs/ARCHITECTURE.md` | слои платформы и правила проектирования |
| `docs/ROADMAP.md` | актуальный порядок работы после Build 47 |
| `docs/SOURCE_OF_TRUTH.md` | подтверждённые runtime-контракты |
| `docs/FIELD_MAP.md` | компактная карта offsets |
| `docs/ASSET_FORMATS.md` | форматы XFS/AI resources |
| `docs/generated/TYPE_ATLAS.md` | generated 4405-type catalog |
| `docs/ARC_MAP.txt` | карта архивов |
| `docs/PLAYER_PAWN_WORK/` | подробный pawn AI vertical slice |
| `docs/WAND_RANGE.md` | эррата дальности посоха пешки (не игрок) |
| `docs/PAWN_SPRINT_RECON.md` | трек «спринт/уклонение пешек» + компенсация темпа |
| `docs/PAWN_IDLE_RECON.md` | разведка разнообразия простоя вне боя |
| `docs/HIRED_PAWNS_SCOPE.md` | наёмные пешки: граница вмешательства и замер общего ресурса |
| `docs/GUARDIAN_REPORTS_MAP.md` | жалобы игроков на Guardian → механика → замеры |

История промежуточных экспериментов находится в Git до Build 47 и не дублируется десятками документов.

## Рабочие команды

```bash
python3 tools/check_cpp_literals.py
python3 tools/generate_type_atlas.py
python3 tools/generate_act_map.py
python3 tools/analyze_pawn_ai_assets.py
python3 tools/analyze_pawn_ai_bridge.py <snapshots...>
python3 tools/xfs_tree_dump.py <resource>
```

Сборка DLL: Visual Studio → **Release | Win32** → Build Solution.

## Runtime hotkeys

| Key | Action |
|---|---|
| F12 | UI |
| F4 | Camera Plus toggle |
| `-` | dev profile switch (research builds) |
| `=` / `+` | pawn AI snapshot (research builds) |
| F9 | зарезервирован сохранением пользователя; мод не использует |

## Ближайшие задачи (сессия 20.08 → следующая)

1. **Пешки-проводники** — единственный воспроизводимый спринт пешки;
   снимается за один заход, когда сюжет дойдёт до квеста сопровождения
   (`PAWN_SPRINT_RECON` §31.1);
2. **Уклонение на кастере** — коды 32/28/73/58/2, у всех есть строки
   приоритета (`PAWN_SPRINT_RECON` §29.3);
3. **Разнообразие простоя вне боя** — `docs/PAWN_IDLE_RECON.md`:
   пул НПЦ как источник закрыт (разные семейства действий и разные
   `.lmt`), но веса целей покоя доступны проверенному рычагу.

## Старые задачи

1. semantic mapping priority code → intent/GOAP;
2. main-pawn-specific root association и current target;
3. action eligibility A/B на `AIPlActParam*`;
4. GOAP patch framework только для доказанных дыр;
5. перенос метода на monster decision layer.

Подробно: `docs/ROADMAP.md`.

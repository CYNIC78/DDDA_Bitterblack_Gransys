# DDDA AI Overhaul — Project Hub

## Текущий milestone

**Build 47:** `pawn-ai-platform-milestone`

**Ветка разработки:** `work/player-main-pawn-recon`

Главный результат: verified runtime priority platform главной пешки с persistent generalized profiles, automatic discovery, multi-rule transaction, convergence и rollback.

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

## Ближайшие задачи

1. semantic mapping priority code → intent/GOAP;
2. main-pawn-specific root association и current target;
3. action eligibility A/B на `AIPlActParam*`;
4. GOAP patch framework только для доказанных дыр;
5. перенос метода на monster decision layer.

Подробно: `docs/ROADMAP.md`.

# DDDA AI Overhaul — документация

Документация разделена по назначению. История экспериментов хранится в Git и не дублируется отдельными `TEST_*`/`RESULT_*` файлами в корне.

## Канонические документы

| Файл | Назначение |
|---|---|
| [`VISION.md`](VISION.md) | **замысел: три слоя, философия, куда идём.** Живой документ |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | устройство платформы, слои AI и правила проектирования |
| [`ROADMAP.md`](ROADMAP.md) | текущее состояние и порядок дальнейшей работы |
| [`NEXT_MILESTONE_OPTIONS.md`](NEXT_MILESTONE_OPTIONS.md) | **три трека после Build 73**: честный спринт пешек / память места / дышащий мир — цена, риск, первый шаг |
| [`SOURCE_OF_TRUTH.md`](SOURCE_OF_TRUTH.md) | только подтверждённые runtime-контракты и источники |
| [`FIELD_MAP.md`](FIELD_MAP.md) | компактные таблицы подтверждённых полей и оффсетов |
| [`ASSET_FORMATS.md`](ASSET_FORMATS.md) | глубокая спецификация XFS и игровых AI-ресурсов |
| [`generated/TYPE_ATLAS.md`](generated/TYPE_ATLAS.md) | сгенерированный справочник 4405 MT Framework типов |
| [`ARC_MAP.txt`](ARC_MAP.txt) | карта ресурсов `game_main.arc` и связанных архивов |
| [`BUILD_INSTRUCTIONS_RU.md`](BUILD_INSTRUCTIONS_RU.md) | сборка Release/Win32 в Visual Studio — единственный канонический build-документ |
| [`IMGUI_148_RULES.md`](IMGUI_148_RULES.md) | ограничения старой версии ImGui в проекте |
| [`FIX_RULES.md`](FIX_RULES.md) | правила внесения правок в живой runtime |
| [`LAYER_MODEL.md`](LAYER_MODEL.md) | модель слоёв AI |
| [`GUARDIAN_VOCATION_MATRIX.md`](GUARDIAN_VOCATION_MATRIX.md) | ролевая матрица Guardian (вокация пешки × вокация игрока) |
| [`GUARDIAN_LEASH_MATRIX.md`](GUARDIAN_LEASH_MATRIX.md) | состояние вопроса по поводку (follow-дистанции) |
| [`INCLINATION_DRIFT_INTEL.md`](INCLINATION_DRIFT_INTEL.md) | верифицированная модель дрифта инклинаций |
| [`ENCOUNTER_MEMORY_DESIGN.md`](ENCOUNTER_MEMORY_DESIGN.md) | телеметрия боёв, память мест и поправка мутаций — связка слоёв 2 и 3 |
| [`ANATOMY_EM0100.md`](ANATOMY_EM0100.md) | анатомия гоблина: поля тела, подобъекты, анимации, характеристики — справочник для модеров |
| [`TEMPO_SYSTEM.md`](TEMPO_SYSTEM.md) | **система темпа**: примитив, две ручки, связка, пресеты, замеренная цена |
| [`MONSTER_AI_ARCHITECTURE.md`](MONSTER_AI_ARCHITECTURE.md) | **две стороны и одна шина**: контракт режиссёра монстров, правила политик |
| [`SPECIES_ROLLOUT.md`](SPECIES_ROLLOUT.md) | перенос темпа на остальные виды: допуск вида, классификация атак по `ActMap` |
| [`GOBLIN_PACK_OBSERVE.md`](GOBLIN_PACK_OBSERVE.md) | ночной observe-прибор exact `uEm0100`: протокол берега, без записи |
| [`WAND_RANGE.md`](WAND_RANGE.md) | **эррата посоха пешки**: eligibility 15 м, не игрок, IceWalk короткий |
| [`PAWN_SPRINT_RECON.md`](PAWN_SPRINT_RECON.md) | почему пешки не спринтят в бою: разведка GOAP, приборы, план правки |
| [`generated/PAWN_GOAL_SET.md`](generated/PAWN_GOAL_SET.md) | 69 загруженных целей планировщика пешки со смещениями (живой дамп) |
| [`STATUS_EFFECTS_RECON.md`](STATUS_EFFECTS_RECON.md) | статусы и торпор (のろま), карта `cCharParamEnemy` из 72 полей |
| [`TEMPO_HUNT_PROTOCOL_RU.md`](TEMPO_HUNT_PROTOCOL_RU.md) | протокол исследования темпа: что вводить и когда жать |
| [`MONSTER_TEMPO_RECON.md`](MONSTER_TEMPO_RECON.md) | история охоты за темпом: что исключили, где ошиблись и как нашли |
| [`SPAWN_SYSTEM_RECON.md`](SPAWN_SYSTEM_RECON.md) | карта классов расстановки врагов — вход в слой среды |
| [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md) | готовность к публичному релизу: что закрыто, что требует решения |
| [`REFACTOR_TASK.md`](REFACTOR_TASK.md) | рефакторинг слоёв Build 69: что сделано и что проверить |
| [`DEVTOOLS_LAYER_MAP.md`](DEVTOOLS_LAYER_MAP.md) | generated: карта слоёв `DevTools.cpp` (`tools/analyze_devtools_layers.py`) |
| [`changelog/`](changelog/) | подробные дневники по дням; мастер-индекс — [`../CHANGELOG.md`](../CHANGELOG.md) |

## Рабочая область игрока и главной пешки

[`PLAYER_PAWN_WORK/`](PLAYER_PAWN_WORK/) содержит отдельный вертикальный срез `uPlayer/uCmc`:

- live body, action/FSM и character record;
- 83 разобранных pawn AI resources;
- priority → planner → eligibility → action pipeline;
- Builds 40–47: runtime buckets, `AddS32`, persistent profiles;
- канонический `generated/pawn_ai_catalog.json` и weapon action CSV.

Подробный индекс: [`PLAYER_PAWN_WORK/README.md`](PLAYER_PAWN_WORK/README.md).

## Где хранится история

Завершённые протоколы, дневники `TEST_*`, промежуточные гипотезы и тяжёлые snapshot reports удалены из текущего дерева. Они доступны через историю Git до milestone Build 47 (`54e0573`) и не являются источником истины.

## Правила документации

1. Подтверждённый runtime-факт сначала попадает в `SOURCE_OF_TRUTH.md` и/или `FIELD_MAP.md`.
2. Архитектурное решение меняет `ARCHITECTURE.md`.
3. Следующий практический шаг меняет `ROADMAP.md`.
4. Generated-справочники (`generated/`, `DEVTOOLS_LAYER_MAP.md`) не редактируются вручную — только через `tools/`.
5. Один эксперимент не создаёт новый вечный документ: после подтверждения результат консолидируется, сырой отчёт удаляется.
6. Абсолютные heap/vtable-адреса из одного запуска не становятся runtime-контрактом.

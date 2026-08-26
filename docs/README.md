# DDDA AI Overhaul — документация

## Живой канон

| Файл | Назначение |
|---|---|
| [`SOURCE_OF_TRUTH.md`](SOURCE_OF_TRUTH.md) | **единственный runtime-контракт** (84.25) |
| [`HUNT.md`](HUNT.md) | охота за адресом: снапшот, не новый прибор |
| [`FIELD_MAP.md`](FIELD_MAP.md) | компактные offsets |
| [`VISION.md`](VISION.md) | замысел трёх слоёв |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | слои платформы и правила |
| [`FIX_RULES.md`](FIX_RULES.md) | как вмешиваться в AI |
| [`ASSET_FORMATS.md`](ASSET_FORMATS.md) | XFS / AI resources |
| [`generated/STATUS_PARAM.md`](generated/STATUS_PARAM.md) | 40 слотов `*.statusparam` |
| [`generated/TYPE_ATLAS.md`](generated/TYPE_ATLAS.md) | 4405 типов |
| [`BUILD_INSTRUCTIONS_RU.md`](BUILD_INSTRUCTIONS_RU.md) | сборка Release/Win32 |
| [`archive/`](archive/) | замороженные дневники и старые контракты |

## Операционные документы (ещё в работе)

| Файл | Назначение |
|---|---|
| [`TEMPO_SYSTEM.md`](TEMPO_SYSTEM.md) | примитив темпа |
| [`SPECIES_ROLLOUT.md`](SPECIES_ROLLOUT.md) | метод допуска вида |
| [`POSSESSION_RECON.md`](POSSESSION_RECON.md) | охота Possession; канон слота — SoT §12 |
| [`PARTY_STATUS_OBSERVE.md`](PARTY_STATUS_OBSERVE.md) | прибор PS |
| [`GOBLIN_PACK_OBSERVE.md`](GOBLIN_PACK_OBSERVE.md) | observe exact `uEm0100` |
| [`GOBLIN_FAKEHIT.md`](GOBLIN_FAKEHIT.md) | goblin block B |
| [`WAND_RANGE.md`](WAND_RANGE.md) | эррата посоха 15 м |
| [`PAWN_SPRINT_RECON.md`](PAWN_SPRINT_RECON.md) | открытый трек спринта |
| [`PAWN_IDLE_RECON.md`](PAWN_IDLE_RECON.md) | простой вне боя |
| [`HIRED_PAWNS_SCOPE.md`](HIRED_PAWNS_SCOPE.md) | граница наёмных |
| [`ERRATA_ARCHITECTURE.md`](ERRATA_ARCHITECTURE.md) | слой B vs доктрина |
| [`ENCOUNTER_MEMORY_DESIGN.md`](ENCOUNTER_MEMORY_DESIGN.md) | память мест (дизайн) |
| [`INCLINATION_DRIFT_INTEL.md`](INCLINATION_DRIFT_INTEL.md) | дрифт склонностей |
| [`ANATOMY_EM0100.md`](ANATOMY_EM0100.md) | тело гоблина |
| [`LAYER_MODEL.md`](LAYER_MODEL.md) | модель слоёв AI |
| [`IMGUI_148_RULES.md`](IMGUI_148_RULES.md) | ImGui 1.48 |
| [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md) | публичный релиз |
| [`DEVTOOLS_LAYER_MAP.md`](DEVTOOLS_LAYER_MAP.md) | generated: слои DevTools |
| [`changelog/`](changelog/) | дневники; индекс — [`../CHANGELOG.md`](../CHANGELOG.md) |

Битые ссылки (`GUARDIAN_VOCATION_MATRIX.md`, `GUARDIAN_LEASH_MATRIX.md`,
`REFACTOR_TASK.md`) в дереве нет — не восстанавливать наугад.

## `PLAYER_PAWN_WORK/`

Вертикальный срез `uPlayer/uCmc`, каталог 83 ресурсов, Builds 40–47.
Индекс: [`PLAYER_PAWN_WORK/README.md`](PLAYER_PAWN_WORK/README.md).

## Правила

1. Runtime-факт → `SOURCE_OF_TRUTH.md` / `FIELD_MAP.md`.
2. Архитектура платформы → `ARCHITECTURE.md`.
3. Новый «план» не плодим: пробелы живут в SoT §13.
4. Generated (`generated/`, `DEVTOOLS_LAYER_MAP.md`) — только из `tools/`.
5. Законченный дневник охоты → `docs/archive/`, не удалять.
6. Heap/vtable VA одного запуска — не контракт.

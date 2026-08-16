# DDDA AI Overhaul — документация

Документация разделена по назначению. История экспериментов хранится в Git и не дублируется отдельными `TEST_*`/`RESULT_*` файлами в корне.

## Канонические документы

| Файл | Назначение |
|---|---|
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | устройство платформы, слои AI и правила проектирования |
| [`ROADMAP.md`](ROADMAP.md) | текущее состояние Build 47 и порядок дальнейшей работы |
| [`SOURCE_OF_TRUTH.md`](SOURCE_OF_TRUTH.md) | только подтверждённые runtime-контракты и источники |
| [`FIELD_MAP.md`](FIELD_MAP.md) | компактные таблицы подтверждённых полей и оффсетов |
| [`ASSET_FORMATS.md`](ASSET_FORMATS.md) | глубокая спецификация XFS и игровых AI-ресурсов |
| [`generated/TYPE_ATLAS.md`](generated/TYPE_ATLAS.md) | сгенерированный справочник 4405 MT Framework типов |
| [`ARC_MAP.txt`](ARC_MAP.txt) | карта ресурсов `game_main.arc` и связанных архивов |
| [`BUILD_INSTRUCTIONS_RU.md`](BUILD_INSTRUCTIONS_RU.md) | сборка Release/Win32 в Visual Studio |
| [`IMGUI_148_RULES.md`](IMGUI_148_RULES.md) | ограничения старой версии ImGui в проекте |

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
4. Generated-справочники не редактируются вручную.
5. Один эксперимент не создаёт новый вечный документ: после подтверждения результат консолидируется, сырой отчёт удаляется.
6. Абсолютные heap/vtable-адреса из одного запуска не становятся runtime-контрактом.

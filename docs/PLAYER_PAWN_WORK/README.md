# Рабочие документы: игрок и главная пешка

Эта папка содержит документы, созданные в ходе отдельного исследования `uPlayer` / `uCmc`. Общие документы проекта, которые только дополнялись (`FIELD_MAP.md`, `SOURCE_OF_TRUTH.md`, `ASSET_FORMATS.md` и другие), остаются на своих прежних местах.

## Состав

- `PLAYER_PAWN_RECON.md` — протокол и история первого поиска live-тел.
- `PLAYER_PAWN_IN_MEMORY.md` — подтверждённая карта live body, action/FSM и character record.
- `PARTY_LIVE_TRACE_RESULT_39.md` — полный результат непрерывного теста Build 39.
- `PAWN_RESOURCE_EXTRACTION.md` — мост от файлов `game_main.arc` к живой AI-системе пешки и список того, что нужно распаковать.
- `PAWN_AI_ASSET_RESULT.md` — результат разбора 83 файлов: priority table, GOAP и weapon action parameters.
- `PAWN_AI_LIVE_BRIDGE_RESULT_40.md` — найденные live priority code, plan slots, personality table и `cCmc*` action parameters.
- `PAWN_AI_SCORE_BRIDGE_RESULT_41.md` — root `cAIPriorityThink -> rAIPriorityThink` и исправленная исходная гипотеза 0x90 targets.
- `PAWN_AI_PRIORITY_BUCKET_RESULT_42.md` — точный layout 48 live buckets, пять вычисленных переносов строк и sentinel «no priority».
- `PAWN_AI_PRIORITY_RULE_RESULT_43.md` — точные runtime layouts `cCodeParam`/`cOrderValue` и контракт первого rollback-safe A/B.
- `PAWN_AI_PRIORITY_AB_RESULT_44.md` — успешный AddS32 A/B, точное bucket-смещение и доказательство отдельной rebuild-фазы.
- `PAWN_AI_PROFILE_SIDECAR_RESULT_45.md` — persistent профиль, auto-discovery/apply после перезапуска и clean vanilla rollback.
- `FUTURE_PAWN_SIGNAL_BUS.md` — отложенный мост боевых реплик пешек в CombatBus.
- `generated/` — канонический resource-каталог, weapon CSV, full 91-slot priority semantics, planner map, runtime GOAP manifest и compact Build 51/53 evidence; semantic generator также обновляет runtime include `src/devtools/generated/PawnPrioritySemantics.inl`; тяжёлые промежуточные reports удалены.

Завершённые test protocol-файлы удалены после подтверждения результатов. Главные факты пока также продублированы в центральных `docs/FIELD_MAP.md` и `docs/SOURCE_OF_TRUTH.md`; их общая чистка проводится отдельно.

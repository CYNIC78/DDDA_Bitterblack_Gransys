# Build 45 — persistent priority sidecar подтверждён

**Batches:** first session `001f/002f`, restarted session `001g/002g`  
**Build tag:** `45-pawn-ai-profile-sidecar`

## 1. Полный persistence cycle успешен

| Snapshot | Session | Sidecar profile | AddS32 | Code 45 slot | Converged |
|---|---|---|---:|---:|---|
| `001f` | first | `vanilla` | -1 | 35 | yes |
| `002f` | first | `research_code45` | -2 | 34 | yes |
| `001g` | after process restart | `research_code45` | -2 | 34 | yes |
| `002g` | restarted | `vanilla` | -1 | 35 | yes |

Все четыре snapshots дали:

```text
48/48 coherent descriptors
85/85 known cPrioParam pointers
0 unknown pointers
0 rule validation errors
profile fileOk = true
profile converged = true
```

Таким образом подтверждён полный runtime pipeline:

```text
DDDA_AI_Overhaul/ddda_pawn_ai_profiles.ini
  -> persisted active profile
  -> automatic live-object discovery after loading
  -> validated cPrioParam/cCodeParam resolution
  -> AddS32 write
  -> natural cAIPriorityThink rebuild
  -> expected live bucket
```

## 2. Автоматическое применение действительно произошло после перезапуска

Между `002f` и `001g` изменились абсолютные адреса:

```text
first session:
  cPrioParam 0x10B802A0
  cCodeParam 0x10B80330

restarted session:
  cPrioParam 0x10BA34F0
  cCodeParam 0x10BA3580
```

Несмотря на это, `001g` без ручного повторного выбора получил:

```text
active          research_code45
desiredAddS32   -2
currentAddS32   -2
liveSlot        34
converged       true
```

Следовательно, sidecar не сохраняет transient addresses. Каждая загрузка заново
разрешает exact priority tuple и rule index.

Оранжевый UI status корректно означает активный non-vanilla profile, а не ошибку.
При DLL/world unload runtime field откатывается, но sidecar остаётся активным и
применяется к новому объекту после следующей загрузки.

## 3. Hot switch обратно в vanilla также подтверждён

`002g` после переключения:

```text
active          vanilla
desiredAddS32   -1
currentAddS32   -1
liveSlot        35
writes/restores 2/2
converged       true
```

То есть sidecar — не односторонний patch. Он поддерживает runtime apply и чистый
rollback без изменения save или `game_main.arc`.

## 4. Дополнительная находка между загрузками

Сами inclination states и placement других priorities изменились после restart:

```text
first session:
  personality 1 state 2
  personality 5 state 1
  codes 54/55 in Etc-04 (delta 0)

restarted session:
  personality 1 state 1
  personality 5 state 2
  codes 54/55 in Etc-06 (delta +2)
```

Это независимое подтверждение, что `cAIPriorityThink` не копирует `cmc.prt`
один раз навсегда, а пересобирает buckets из runtime personality/context state.
Codes 54/55 двигаются совместно, поэтому их итоговый delta пока нельзя объяснять
простым суммированием только видимых rule rows.

Code 45 сохранил personality-0/state-0 условие в обеих сессиях и потому остаётся
чистым контрольным target sidecar engine.

## 5. Архитектурный итог

Static ARC repack для priority tuning больше не нужен как основной путь.
Подтверждён рабочий memory-only слой с persistent пользовательским выбором:

- sidecar хранится отдельно от save и игровых архивов;
- абсолютные адреса не сохраняются;
- правила разрешаются заново после загрузки;
- запись разрешена только после exact validation;
- UI показывает pending/applied через live bucket convergence;
- vanilla profile выполняет rollback.

Следующий шаг — заменить один hardcoded proof-rule на общий список rule entries в
sidecar (`sensor/code/category/object/extra/ruleIndex/AddS32`) и затем дать этим
entries понятные поведенческие имена после mapping planner/GOAP codes.

Тяжёлый промежуточный persistence report удалён после фиксации результата; generalized engine подтверждён компактным Build 46 evidence.

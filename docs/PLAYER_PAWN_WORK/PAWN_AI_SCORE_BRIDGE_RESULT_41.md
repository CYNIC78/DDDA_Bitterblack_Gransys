# Build 41 — root priority bridge подтверждён

**Снимки:** Wait, Wait, Follow/Walk, Combat/Hyakuretsu

## 1. Root-пара найдена

В одном запуске стабильно присутствовали:

```text
cAIPriorityThink  @ 0x10F59640   size 1020
rAIPriorityThink  @ 0x506480B0   size 1064
```

Абсолютные адреса временные.

Подтверждена прямая ссылка:

```text
cAIPriorityThink + 0x08 -> rAIPriorityThink
```

Хэш `rAIPriorityThink` был неизменен во всех четырёх снимках: это статический resource из `cmc.prt`.

Хэш `cAIPriorityThink` менялся Wait → Follow → Combat: это runtime-объект вычисления/сортировки приоритетов.

## 2. Current priority chain повторена полностью

| Snapshot | Pawn action | `planner +0x17C` | `cPrioParam` | `PlanCtrl` |
|---:|---|---:|---|---|
| 001 | Wait | 0 | runtime code 0 | slot 0 |
| 002 | Wait | 0 | runtime code 0 | slot 0 |
| 003 | Walk/Follow | 1 | runtime code 1 | slot 1 |
| 004 | Dagger Hyakuretsu | 54 | runtime code 54 | slot 54 |

Для code 1/54 точные вычисления PlanCtrl по формуле `+0x190 + code*0x110` снова дали реально существующие DTI-объекты.

Code 0 неоднозначен в каталоге `cmc.prt`: 16 quest rows используют code 0 с разными extra masks. Поэтому для Wait нельзя выбирать `cPrioParam` только по code; требуются sensor/category/object/extra или текущий pointer из score node.

## 3. Что меняется в `cAIPriorityThink`

В root нет очевидного массива float scores. Меняются:

- небольшие integer counts/states в header (`+0x0C..+0x30`);
- десятки heap pointers по всему объекту;
- несколько count/rank значений возле конца (`+0x3AC`, `+0x3D4`).

Адреса pointer targets часто образуют серии с шагом `0x90`. В Build 41 это было принято за отдельные transient score/ranking nodes.

**Поправка Build 42:** это allocator-шаг буферов `mpArray` у 48 `cArray` внутри root, а не размер score object. Действительны только первые `count` указателей на `cPrioParam`; trailing bytes являются свободным/stale storage. Вычисленный integer priority проявляется как перенос строки между live buckets.

## 4. Player action parameter roots

Найдены точные файловые наборы:

```text
9 x rAIPlayerActionParameter (static resources)
```

Также найдены `cAIPlayerActionParameter` runtime objects. Часть из них временная и исчезает после первого снимка; постоянными остались семь объектов. Статические resources не менялись.

Это подтверждает двухступенчатую схему:

```text
rAIPlayerActionParameter -> runtime cAIPlayerActionParameter -> cCmc* +0x258
```

Прямая pointer-связь первого уровня не найдена; compiled значения в `cCmc* +0x258` остаются подтверждённым live write point.

## 5. Situation layer

Найдены 12 объектов `cAICheckSituationCmc` size 1424. Одиннадцать изменились при Follow/Combat, один оставался статичным. Это отдельные оценщики ситуации, а не один current-state object.

Они будут подключаться после score nodes, через обнаруженные ссылки, а не глобальным перебором.

## 6. Почему cap всё ещё достигнут

Root-объекты теперь попали в результат, поэтому cap больше не блокирует задачу. Но `cAIGoalPlanning::*` дал дополнительные сотни inline subobjects (`GoalPlanningNode`, `GoalInfo`, `GoalPlanningResult`).

Build 42 оставляет только `PlanCtrl/PlanResult/GoalInfoParam` и исключает остальные уже понятные типы.

## 7. Продолжение

Build 42 проверил 0x90-byte targets и исправил модель: это `cPrioParam**` payloads 48 runtime buckets. См. `PAWN_AI_PRIORITY_BUCKET_RESULT_42.md`.

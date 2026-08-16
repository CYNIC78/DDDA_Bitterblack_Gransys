# Build 42 — runtime priority buckets

**Статус:** подтверждено тремя снимками главной пешки: Wait, Follow и Combat.

## 1. Главная поправка к гипотезе Build 41

Указатели с шагом allocator-а `0x90` — не отдельные объекты с materialized float score.
Это буферы `mpArray` у 48 массивов внутри `cAIPriorityThink`.

Точный layout root:

```text
cAIPriorityThink +0x08 -> rAIPriorityThink (cmc.prt)

cAIPriorityThink +0x38 + slot*0x14:
  +0x00  cArray vtable
  +0x04  count
  +0x08  capacity
  +0x0C  flags / auto-delete state
  +0x10  cPrioParam** mpArray
```

Всего `6 групп * 8 slots = 48` массивов:

```text
QUEST, PL_Party, Situ_Personal, Enemy, Wait_Follow, Etc
```

В тестовом состоянии непустыми были 33 массива. В их действующих диапазонах
`[0, count)` лежали ровно 85 уникальных указателей на все 85
`rAIPriorityThink::cPrioParam` из `cmc.prt`.

Почему прежний разбор видел «node»: каждый `mpArray` имел capacity 16, а allocator
размещал буферы с характерным шагом `0x90`. После первых `count` указателей дамп
захватывал свободную или старую память allocator-а. Например, в буфере `Etc - 06`
действовали первые пять pointers `[54,55,67,72,73]`, а ссылки на `47/46/45` по
`+0x50` были чужим stale payload. Их нельзя считать частью этого bucket.

## 2. Что является вычисленным результатом priority policy

`cAIPriorityThink` материализует не scalar score для каждой строки, а **раскладку
`cPrioParam*` по integer buckets и порядок внутри bucket**.

Сравнение source slot из `cmc.prt` с live slot дало:

| Codes | Source slot | Live slot | Delta |
|---|---|---|---:|
| 45, 46, 47 | `Wait_Follow - 04` | `Wait_Follow - 03` | -1 |
| 54, 55 | `Etc - 04` | `Etc - 06` | +2 |

Остальные 80 строк остались в исходных slots.

Для codes 45/46/47 смещение `-1` совпадает с их активным personality `AddS32=-1`.
Для 54/55 live delta `+2` подтверждён, но пока нельзя приписывать его одной
конкретной personality/order проверке: следующий снимок должен раскрыть runtime
`cCodeParam` и `cOrderValue` целиком.

Значит практическая точка управления найдена:

```text
cmc.prt base slot
  + personality/order AddS32 и runtime conditions
  -> live cAIPriorityThink bucket + ordered cPrioParam pointers
  -> sensor/action eligibility
  -> selected planner code
```

## 3. Bucket order и current selection — разные вещи

В трёх снимках состав и порядок всех 33 buckets были одинаковыми. Менялись heap
addresses буферов, но не их логическое содержимое.

Примеры:

```text
root pointer +0x0FC, PL_Party - 01 -> [64, 70, 1, 56]
root pointer +0x3E0, Etc - 06      -> [54, 55, 67, 72, 73]
```

Follow выбрал code 1, хотя он стоит на index 2 внутри bucket. Combat выбрал code 54
на index 0. Следовательно, array order задаёт priority order, но выбор также
фильтруется sensors, situation checks, order и action eligibility. Это не простое
«всегда взять index 0».

Восемь quest buckets `+0x48..+0xD4` содержали пары code 0 с разными sensor/Extra:

```text
Extra masks: 0x202, 0x204, 0x208, 0x210, 0x220, 0x240, 0x280, 0x300
```

Поэтому code 0 нельзя идентифицировать без полного tuple
`Sensor/Code/Category/OBJECT_ID/Extra`.

## 4. Wait может не иметь current priority

Три состояния Build 42:

| Snapshot | Pawn action | `cAIGoalPlanning+0x17C` | Результат |
|---:|---|---:|---|
| 001 | `cPlActWait` | `0xFFFFFFFF` | current priority отсутствует |
| 002 | `cPlActRun` | `1` | Follow priority |
| 003 | `cPlActWpnDaggerAtckLandL` | `54` | combat priority |

Build 41 действительно видел code 0 в другом idle состоянии. Оба результата
совместимы: Wait может быть либо планом code 0, либо fallback при отсутствии
выбранного priority.

Формула `PlanCtrl = planner + 0x190 + code*0x110` применяется только если code не
равен `0xFFFFFFFF`.

Анализатор исправлен: он выводит raw field отдельно, а `selectedPriorityCode=null`
и `hasSelectedPriority=false` для sentinel `0xFFFFFFFF`.

## 5. Дополнительные результаты census

Build 42 записал `1007` candidates и `1006` валидных объектов — cap 1024 больше не
достигнут. Среди них:

```text
85  rAIPriorityThink::cPrioParam
41  rAIPriorityThink::cOrderValue
353 cAIPlayerActionParameter
91  cAIGoalPlanning::cPlanCtrl
91  cAIGoalPlanning::cPlanResult
91  cAIGoalPlanning::cGoalInfoParam
```

353 runtime action-parameter objects почти точно покрывают 352 строки девяти
`AIPlActParam*` resources плюс один default/sentinel object; точная индексация ещё
не утверждается.

Прямые DTI pointers внутри `cCmcInfo`:

```text
+0x0288 -> rHumanEdit
+0x028C -> rBodyEdit
+0x0290 -> rFaceEdit
+0x07EC -> cPlLoadManager
+0x1658 -> cPwnMsgLoadManager
```

`cPwnMsgLoadManager` — важный будущий мост к pawn messages/callouts, но не
priority root.

## 6. Итоговый подтверждённый pipeline

```text
rAIPriorityThink / cmc.prt
  -> 85 cPrioParam rules
  -> nested personality/order rules (AddS32)
  -> 48 live cAIPriorityThink buckets
  -> sensor + cAICheckSituationCmc + action eligibility
  -> cAIGoalPlanning+0x17C: code или 0xFFFFFFFF
  -> cPlanCtrl[code]
  -> cCmc* action interface
  -> body+0x2DC8 execution slot / body+0x2DD4 packed code
  -> cPlAct* actual action
```

Тяжёлый промежуточный machine-readable snapshot report удалён после фиксации подтверждённых результатов в этом документе и каноническом каталоге.

## 7. Следующий безопасный шаг

Build 43 остаётся read-only и должен атомарно сохранить:

1. descriptor/count/mpArray каждого из 48 buckets;
2. pointer arrays `cPrioParam+0x28` (personality rules) и `+0x3C` (orders);
3. каждый pointed `cCodeParam` и `cOrderValue` целиком;
4. точное соответствие runtime bytes полям `AddS32`, `AddF32`, checks и order type/value.

Только после этого допустим Build 44: временно изменить один `AddS32`, проверить
перемещение одной строки между buckets и гарантированно восстановить исходные
байты при повторном хоткее, unload или потере объекта.

# Build 43 — nested priority rules подтверждены

**Snapshot:** `ddda_pawn_ai_bridge_001c.json`  
**Build tag:** `43-pawn-ai-priority-rules`

## 1. Снимок полный и согласованный

```text
1007 census candidates
1007 written objects
48/48 coherent cAIPriorityThink descriptors
33 non-empty runtime buckets
85/85 known cPrioParam pointers
0 unknown pointers
85 priorityRules records
```

Все descriptor/payload пары прошли повторное чтение без изменения. Caveat Build 42
с двумя разнесёнными проходами устранён.

Live bucket placement полностью повторило Build 42:

| Codes | Source | Runtime | Delta |
|---|---|---|---:|
| 45, 46, 47 | `Wait_Follow - 04` | `Wait_Follow - 03` | -1 |
| 54, 55 | `Etc - 04` | `Etc - 06` | +2 |

Остальные 80 строк не перемещены.

В этом idle snapshot planner выбрал code 0. Это дополняет предыдущий idle snapshot с
`0xFFFFFFFF`: Wait действительно может работать и через code 0, и как fallback
без current priority.

## 2. Layout `cPrioParam` nested arrays

Подтверждённый `cPrioParam`:

```text
+0x18  personality cArray vtable
+0x1C  personality count
+0x20  personality capacity
+0x24  personality flags
+0x28  cCodeParam**

+0x2C  order cArray vtable
+0x30  order count
+0x34  order capacity
+0x38  order flags
+0x3C  cOrderValue**
```

В 85 rows найдено:

```text
21 rows with personality rules
48 cCodeParam rules total
48 personality checks total
41 rows with order rule
41 cOrderValue rules total
```

Все pointer arrays и все дочерние objects прочитаны успешно.

## 3. Точная write-точка personality modifier

Первые поля каждого 104-byte `rAIPriorityThink::cCodeParam`:

```text
+0x00  vtable
+0x04  int32 AddS32
+0x08  float AddF32
+0x0C  uint32 BreakAfterApply
+0x10  check cArray vtable
+0x14  check count
+0x18  check capacity
+0x1C  check flags
+0x20  check pointer array
```

Проверка всех 48 rules против `cmc.prt`:

```text
AddS32:          48/48 exact
AddF32:          48/48 exact
BreakAfterApply: 48/48 exact
check count:     48/48 exact
validation errors: 0
```

У всех 48 runtime rules один vtable `0x01558A04` в этом запуске. Абсолютный адрес
не должен использоваться как сигнатура между версиями/запусками.

Personality/state каждой проверки уже однозначно задаются source row и индексом
rule из `cmc.prt`. Для первого A/B изменения достаточно проверенного `AddS32` по
`cCodeParam+0x04`; layout самих check leaf objects для этой записи не нужен.

## 4. Layout order modifier

Каждый 12-byte `rAIPriorityThink::cOrderValue`:

```text
+0x00  vtable
+0x04  uint32 Value
+0x08  uint32 Type
```

Все 41 runtime pair `Value/Type` точно совпали с каталогом; validation errors 0.

## 5. Выбранная первая мутация

Для первого rollback-safe A/B выбран ровно один modifier:

```text
source row:       Wait_Follow - 04, index 7
priority code:    45
sensor/code/...:  0 / 45 / 0 / 0 / 1
personality rule: index 0
check:            personality 0, state 0
original AddS32:  -1
test AddS32:      -2
write address:    resolved cCodeParam + 0x04
```

Почему code 45:

- его текущий `-1` уже визуализирован переносом slot 36 -> 35;
- codes 45/46/47 сейчас находятся вместе;
- изменение только code 45 на `-2` должно отделить его в пустой slot 34;
- codes 46/47 служат встроенной контрольной группой и должны остаться в slot 35.

Никакие pointers, counts, planner code или action execution slots не изменяются.

## 6. Rollback contract Build 44

Перед записью Build 44 обязан повторно проверить:

1. полный tuple выбранного `cPrioParam`;
2. personality count/capacity и первый child pointer;
3. child vtable;
4. исходные `AddS32=-1`, `AddF32=0`, `BreakAfterApply=1` и check count 1;
5. после записи прочитать `AddS32` обратно.

Rollback выполняется:

- повторным нажатием диагностической клавиши `-`;
- при выходе из world;
- в DLL shutdown до отключения hooks;
- при неожиданном overwrite/потере target.

Если current value не равен ни сохранённому original, ни нашему test value, запись
не выполняется, чтобы не затереть чужое изменение.

Тяжёлый промежуточный snapshot report удалён после проверки всех 48/41 rules без ошибок; итоговый layout сохранён выше.

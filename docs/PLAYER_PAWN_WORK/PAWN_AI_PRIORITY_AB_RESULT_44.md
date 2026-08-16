# Build 44 — первый runtime priority A/B успешен

**Snapshots:** `001d`, `002d`, `003d`  
**Build tag:** `44-pawn-ai-priority-ab`

## 1. Главный результат

Впервые подтверждено управляемое runtime-изменение priority policy главной пешки:

```text
code 45 / personality rule 0
cCodeParam+0x04 AddS32: -1 -> -2
```

В snapshot `001d` запись была прочитана обратно как `-2`, после чего только code
45 переместился ещё на один live bucket:

```text
source slot 36  Wait_Follow - 04
vanilla -1  -> slot 35  Wait_Follow - 03
test    -2  -> slot 34  Wait_Follow - 02
```

Точная контрольная раскладка:

```text
Test ON:
  slot 34: [45]
  slot 35: [47, 46]

Rollback:
  slot 34: []
  slot 35: [47, 46, 45]
```

Codes 46/47 и остальные 82 priority rows не изменились. Значит `AddS32` имеет
прямую integer-семантику смещения bucket и не является косвенным коэффициентом.

## 2. Rollback подтверждён

Snapshot `002d`:

```text
mutation enabled: false
target valid:     true
current AddS32:   -1
writes/restores:  1 / 1
code 45 slot:     35
vanilla diffs:    0
validation errors: 0
```

Это полный A/B цикл с восстановлением исходного resource field и исходной live
bucket placement. Сообщённый UI status `Priority A/B: OFF, restored AddS32=-1`
дополнительно подтверждает финальный ручной rollback после последнего снимка.

## 3. Важное уточнение: resource write и bucket rebuild разнесены

Snapshot `003d` содержит второй test write:

```text
mutation enabled: true
current AddS32:   -2
writes/restores:  2 / 1
```

Но в момент снимка code 45 ещё оставался в slot 35. То есть запись `cCodeParam`
успешна, однако она сама по себе не гарантирует немедленный rebuild
`cAIPriorityThink`.

Сопоставление двух test-on снимков:

| Snapshot | Runtime AddS32 | Code 45 slot | Вывод |
|---|---:|---:|---|
| `001d` | -2 | 34 | rebuild уже произошёл |
| `003d` | -2 | 35 | resource изменён, bucket ещё старый |

Следовательно, pipeline имеет две отдельные стадии:

```text
write rAIPriorityThink::cCodeParam
  -> ожидание/инвалидация policy
  -> rebuild 48 cAIPriorityThink buckets
```

Для пользовательских профилей нельзя считать запись AddS32 мгновенным live
применением. Профиль нужно либо применять до создания runtime buckets, либо
найти безопасный штатный invalidation/rebuild trigger.

## 4. Snapshot integrity

Во всех трёх snapshots:

```text
48/48 coherent descriptors
85/85 known cPrioParam pointers
0 unknown pointers
all nested rule reads valid
validation errors: 0
selected planner code: 0
```

`001d` дал 34 non-empty buckets из-за отделения code 45 в ранее пустой slot 34.
После rollback снова было 33.

## 5. Rebuild trigger уточнён повторным тестом

Дополнительные snapshots `001e/002e/003e` разделили ожидание и смену контекста.

| Snapshot | Контекст | AddS32 | Code 45 slot | Planner code |
|---|---|---:|---:|---:|
| `001e` | Wait после 15 секунд | -2 | 34 | 0 |
| `002e` | Follow | -2 | 34 | 1 |
| `003e` | Wait после rollback | -1 | 35 | 0 |

Следовательно:

- переход Wait -> Follow **не требуется** для rebuild;
- в спокойном idle движок сам перечитывает resource rule и перестраивает buckets;
- задержка не детерминирована на масштабе трёх секунд, но в тесте гарантированно
  уложилась в 15 секунд;
- Follow не сбрасывает применённый profile: code 45 сохранился в slot 34;
- rollback также конвергирует обратно без ручного invalidation.

Практический production contract:

```text
write validated cCodeParam AddS32
  -> status PENDING
  -> monitor expected live bucket
  -> APPLIED после естественного think rebuild
  -> timeout/error, если convergence не произошла за 20 секунд
```

Это достаточно для первого persistent sidecar profile: неизвестный virtual
rebuild method вызывать не нужно. Profile engine должен применять resource
modifier при загрузке и отслеживать convergence, не обещая мгновенный результат.

Тяжёлые промежуточные A/B snapshot reports удалены после фиксации подтверждённых результатов и компактного Build 46 evidence.

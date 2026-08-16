# Build 40 — живые приоритеты пешки найдены

**Снимки:** Follow/Walk, Follow/Run, Combat/Dagger

**Build:** `40-pawn-ai-live-bridge`

**Статус:** основной CATALOG → LIVE мост подтверждён

## 1. Главный результат

Найден один живой объект:

```text
cAIGoalPlanning @ 0x10F48500   (адрес только этого запуска)
```

В нём:

```text
cAIGoalPlanning + 0x17C = код текущего выбранного приоритета
```

Контроль:

| Snapshot | Нижнее действие пешки | `cAIGoalPlanning +0x17C` |
|---:|---|---:|
| 001 | `cPlActWalk` | `1` |
| 002 | `cPlActRun` | `1` |
| 003 | `cPlActWpnDaggerAtckLandL` | `54` |

Код `1` стабильно пережил Walk → Run, потому что верхнее решение оставалось «следовать за Воскресшим». При переходе в бой верхний приоритет сменился на `54`.

## 2. Код напрямую индексирует планировщик

Внутри `cAIGoalPlanning` расположен массив plan controllers:

```text
PlanCtrl(code) = planner + 0x190 + code * 0x110
```

Это подтверждено DTI-объектами:

```text
code 1  -> planner + 0x2A0  -> cAIGoalPlanning::cPlanCtrl
code 54 -> planner + 0x3AF0 -> cAIGoalPlanning::cPlanCtrl
```

Оба вычисленных объекта присутствуют в census и меняют содержимое в соответствующих состояниях.

## 3. Мост к `cmc.prt`

В памяти найдено ровно **85** объектов:

```text
rAIPriorityThink::cPrioParam
```

Это ровно число priority entries, извлечённых из `cmc.prt`.

Подтверждённый layout одного объекта:

| Runtime offset | Поле из `cmc.prt` |
|---:|---|
| `+0x00` | vtable |
| `+0x04` | Sensor |
| `+0x08` | Code |
| `+0x0C` | Category |
| `+0x10` | OBJECT_ID |
| `+0x14` | Extra/info |
| `+0x18...` | embedded personality modifier array |
| `+0x2C...` | embedded order array |

Текущие статические записи этого запуска:

```text
code 1  -> cPrioParam @ 0x10BA0920
code 54 -> cPrioParam @ 0x10BA4CC0
```

Абсолютные адреса временные; стабильны DTI-класс и оффсеты.

### Priority code 1 — следование

Из каталога `cmc.prt`:

```text
slot: PL_Party - 01
sensor: 0
code: 1
order: type 11, value 0
personality modifiers: none
```

### Priority code 54 — агрессивная боевая ветка

Из каталога `cmc.prt`:

```text
slot: Etc - 04
sensor: 1
code: 54
order: type 9, value 7
```

Personality modifiers:

| Personality ID | State | Delta |
|---:|---:|---:|
| 5 (Guardian) | 2 | `-3` |
| 5 (Guardian) | 1 | `-2` |
| 0 (Scather) | 1 | `+2` |
| 0 (Scather) | 2 | `+5` |
| 1 (Medicant) | 0 | `-1` |

Это первый прямой доказанный пример: в бою планировщик реально выбрал код из `cmc.prt`, а этот код имеет конкретные поправки от инклинаций.

## 4. Живая personality-таблица внутри `cCmcInfo`

В `cCmcInfo` найден массив девяти инклинаций:

```text
entry(id) = cCmcInfo + 0x14B8 + id * 0x0C
```

Layout entry:

| +off | Поле |
|---:|---|
| `+0x00` | context/state (`0/1/2`) |
| `+0x04` | personality ID (`0..8`) |
| `+0x08` | текущий float value |

ID совпадают с уже используемым порядком:

```text
0 Scather, 1 Medicant, 2 Mitigator, 3 Challenger, 4 Utilitarian,
5 Guardian, 6 Nexus, 7 Pioneer, 8 Acquisitor
```

### До боя → в бою

| ID | Inclination | Follow | Combat |
|---:|---|---:|---:|
| 0 | Scather | 760.00 | 787.11 |
| 1 | Medicant | 500.00 | 500.00 |
| 2 | Mitigator | 650.00 | 605.81 |
| 3 | Challenger | 600.00 | 649.18 |
| 4 | Utilitarian | 800.00 | 742.17 |
| 5 | Guardian | 450.00 | 450.00 |
| 6 | Nexus | 450.00 | 450.00 |
| 7 | Pioneer | 400.00 | 400.00 |
| 8 | Acquisitor | 400.00 | 400.00 |

Это live-зеркало политики пешки; изменения совпадают с работой нашего TacticalSwitch. Поле context/state точно используется условиями `cmc.prt`, но семантические названия состояний `0/1/2` требуют отдельного A/B перед записью человекочитаемых имён.

## 5. `AIPlActParam` скопирован в живые `cCmc*`

При входе в бой движок заполнил хвосты action-interface объектов значениями из weapon tables.

Подтверждённый compiled parameter block:

```text
cCmc action interface + 0x258
```

Девять полей:

```text
RangeMinXZ, RangeMaxXZ, RangeMinY, RangeMaxY,
EnableMinXZ, EnableMaxXZ,
ElementAttr, AtkAttr, UseAttr
```

Точные совпадения:

| Live object | Live tuple | Файловая строка |
|---|---|---|
| `cCmcDaggerComboL` | `0,150,-100,250,0,0,0,2,0x80000` | Dagger row 3 Lv1/Lv2 |
| `cCmcDaggerAirL` | `50,150,-100,200,0,0,0,2,0x80000` | Dagger row 4 Lv1/Lv2 |
| `cCmcBowAttack` | `500,1500,-500,1000,0,2000,0,0,0x80004` | Bow row 0 Lv1/Lv2 |
| `cCmcBowRensa` | `500,1500,-500,1000,0,2000,0,0,0x80002` | Bow row 20 Lv1/Lv2 |

Следствие: дистанции и атрибуты конкретных действий можно будет менять непосредственно в `cCmc*` runtime-объектах, не перепаковывая `game_main.arc`.

## 6. Дополнительные поля `cCmcInfo`

Подтверждено зеркало текущих HP:

```text
cCmcInfo + 0x29C = current HP
cCmcInfo + 0x2A4 = второй/recoverable HP parameter
```

В Combat snapshot оба изменились синхронно с уроном пешки.

## 7. Что означал cap=1024

Cap был достигнут из-за полного успешного census, а не из-за мусора:

- 85 `cPrioParam` — точное число из `cmc.prt`;
- 41 `cOrderValue` — точное число order-bearing priority rows;
- сотни GOAP resource subobjects;
- 202 `cCmc*` action-interface objects;
- один живой `cAIGoalPlanning` с 60 DTI-visible plan slots.

Для следующей сборки общий GOAP resource census будет отфильтрован: статический каталог уже сохранён, теперь нужны только planner roots, current plan и pawn-owned graph.

## 8. Следующий шаг

1. Сделать компактный монитор `current priority code` вместо 1024 объектов.
2. Резолвить выбранный `cPrioParam` по `+0x08 == code`.
3. Показывать применимые personality deltas из нашего Generated-каталога.
4. Найти parent/root `cAIPriorityThink` и точку вычисленного итогового score.
5. Только после read-only подтверждения выполнить безопасный A/B одного delta/range с автоматическим откатом.

Машинная выжимка: `generated/pawn_ai_bridge_40_report.json`.

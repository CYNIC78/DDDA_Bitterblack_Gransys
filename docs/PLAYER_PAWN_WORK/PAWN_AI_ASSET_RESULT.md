# Pawn AI assets — первый полный разбор

**Источник:** `resources/extracted_assets/pawnAI/` из `game_main.arc`

**Дата:** 2026-08-15

**Получено:** 83 файла, 418 КБ

## 1. Состав

| Набор | Файлов | Смысл |
|---|---:|---|
| `AI/AIPlayerActionParameter/` | 9 | дистанции и атрибуты действий по оружию |
| `AI/Character/Pawn/` | 1 | маленький `pawn_act_param.eap` |
| `AI/Goap/Cmc/` | 68 | верхнеуровневые планы/цели/интерфейсы пешки |
| `AI/PrioThink/` | 1 | `cmc.prt`, таблица приоритетного мышления |
| `AI/Sensor/Cmc/` | 1 | девять сенсорных профилей |
| `AI/SensorTarget/` | 3 | параметры целей CMC/default/player |

## 2. Главный мост `cmc.prt` → runtime

`cmc.prt` — XFS с шестью классами и 489 сериализованными объектами.

Корневой класс имеет `sizeof=1064`, что **точно совпадает** с TypeAtlas:

```text
file:    cmc.prt class[0] sizeof 1064
runtime: rAIPriorityThink sizeof 1064
```

Его runtime-пара уже известна:

```text
rAIPriorityThink (resource) -> cAIPriorityThink (live instance, sizeof 1020)
```

Корень содержит 48 именованных слотов, по восемь в шести группах:

- `QUEST`;
- `PL_Party`;
- `Situ_Personal`;
- `Enemy`;
- `Wait_Follow`;
- `Etc`.

В них реально заполнено **85 priority entries**:

| Группа | Записей |
|---|---:|
| QUEST | 16 |
| PL_Party | 11 |
| Situ_Personal | 15 |
| Enemy | 5 |
| Wait_Follow | 25 |
| Etc | 13 |

### Структура одной priority entry

Файл сам дал имена полей:

- `Сенсор`;
- `Код`;
- `Категория`;
- `OBJECT_ID`;
- `Дополнительная информация`;
- список модификаторов личности;
- список команд.

Из 85 записей:

- **21** изменяются в зависимости от personality/inclination;
- **41** реагируют на команды;
- personality-модификаторы реально добавляют/вычитают целый приоритет: наблюдаются значения от `-4` до `+5`;
- используются personality ID `0..6` и `8`;
- используются состояния personality `0`, `1`, `2`;
- команды представлены типами `9`, `10`, `11` и значениями `0..7`.

Это прямое файловое доказательство, что инклинации и приказы не являются отдельным «режимом»: они прибавляют или вычитают очки у конкретных вариантов поведения.

Полная таблица: `generated/cmc_priority_rows.csv`.

## 3. GOAP — верхний каталог поведения пешки

Все **68 `.gop` файлов** разобраны без ошибок. Получено:

- 68 целей (Goal);
- 167 action interfaces;
- явные `InterfaceID`;
- premise/effect;
- element/attack/use attributes;
- class hash и runtime size каждого интерфейса.

Примеры:

| GOAP | Goal | InterfaceID |
|---|---|---:|
| `Wait.gop` | Wait | 0 |
| `Follow.gop` | Follow | 1 |
| `Jump.gop` | Jump | 2 |
| `Climb.gop` | Climb | 3 |
| `Provoke.gop` | ProvokeAction | 8 |
| `ItemFind.gop` / `ItemGet.gop` | Wait | 147 |
| `OpenDoor.gop` | OpenDoor | 159 |
| `TreasureBox.gop` | TreasureBox | 165 |
| `DashFollow.gop` | Follow | 173 |

Есть составные файлы: например, `WpnDaggerAtk.gop` содержит десять интерфейсов, а `WpnWandAtk.gop` — шестнадцать.

Полная таблица: `generated/cmc_goap_interfaces.csv`.

## 4. `AIPlActParam*` — параметры низкоуровневых действий

Файлы XFS имеют корневой `sizeof=120`, что точно совпадает с `rAIPlayerActionParameter` из TypeAtlas.

| Таблица | Строк |
|---|---:|
| Bow | 38 |
| BowL | 40 |
| Dagger | 50 |
| GSword | 40 |
| Shield | 29 |
| ShieldMG | 35 |
| Sword | 50 |
| Unarm | 10 |
| Wand | 60 |
| **Всего** | **352** |

Каждая строка содержит 27 полей:

- минимальные/максимальные XZ/Y дистанции для Lv1, Lv2, EX;
- enable-range для трёх уровней;
- element attributes;
- attack attributes;
- use attributes.

### Мост к Build 39

Нижняя часть packed action-code является индексом строки weapon table:

```text
0x01050003 -> Dagger row 3  -> cPlActWpnDaggerAtckLandL
0x0105002A -> Dagger row 42 -> cPlActWpnDaggerCstmHyakuretsu
0x02060000 -> Bow row 0     -> cPlActWpnBow
```

Первые два соответствия подтверждены живым trace Build 39 и диапазоном таблицы Dagger. Bow row 0 согласуется с кодом, но требует отдельного повторного runtime-моста перед записью в SOURCE_OF_TRUTH.

CSV каждой таблицы лежит в `generated/AIPlActParam*.csv`.

## 5. Сенсоры

`cmc_normal.sn2` содержит девять записей. У всех тип `15`, второй радиус `1000`, угол `360°`. Первые радиусы:

```text
3000, 4000, 3000, 1000, 1500, 500, 2000, 1500, 500
```

Это не один «радиус зрения», а набор сенсорных зон для разных решений CMC. Семантика индексов будет сопоставляться с полем `Сенсор` из `cmc.prt` и runtime-проверкой.

## 6. `pawn_act_param.eap`

Файл содержит 14 коротких записей по 24 байта, большинство полей нулевые. Он не является основным каталогом поведения. Основная полезная информация находится в `cmc.prt`, `Goap/Cmc/*.gop` и `AIPlActParam*`.

## 7. Что теперь известно об архитектуре

```text
cmc.prt
  -> rAIPriorityThink (ванильная политика)
  -> cAIPriorityThink (живой расчёт приоритетов)
  -> GOAP goal / InterfaceID
  -> cCmc* (верхнее решение пешки)
  -> AIPlActParam[weapon][action index]
  -> cPlAct* на uCmc + 0x2DC8
  -> packed action-code на uCmc + 0x2DD4
```

Файлы дали каталог и дефолты. Build 39 дал нижний live-выход. Следующая runtime-задача — найти `cAIPriorityThink` и текущий `cCmc*` через pawn-only `cCmcInfo`, затем сопоставить их с уже распарсенными code/InterfaceID.

## 8. Инструменты и generated-результаты

- `tools/xfs_tree_dump.py` — рекурсивно декодирует вложенные XFS class/classref.
- `tools/analyze_pawn_ai_assets.py` — строит каталог из всех pawn AI файлов.
- `generated/pawn_ai_catalog.json` — полный машинный каталог.
- `generated/cmc_priority_rows.csv` — 85 priority entries.
- `generated/cmc_goap_interfaces.csv` — 167 GOAP interfaces.

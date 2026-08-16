# Asset Formats

Канонический справочник статических ресурсов DDDA, используемых как CATALOG для runtime-мода. История гипотез удалена; неподтверждённые runtime-выводы не следуют из одного имени TypeAtlas.

## 1. XFS container

MT Framework XFS самоописывающийся: файл хранит таблицу классов, имена properties, типы, размеры и плотный поток значений.

```text
0x00  char[4]  "XFS\0"
0x04  u16      version (DDDA обычно 0x0109)
0x06  u8,u8    minor/flags
0x08  u32      instanceCount
0x0C  u32      classCount
0x10  u32      dataOffset (relative to 0x14)
0x14  u32[]    classOffset[] (relative to 0x14)
```

Class record:

```text
u32 hash
u32 runtime sizeof
u32 propertyCount
propertyCount × 24 B:
  u32 nameOffset (relative to 0x14)
  u32 typeword    (type/attr/fieldBytes)
  u8[16] reserved
```

Value stream:

```text
u32 instanceCount
u32 classHash
for property in schema order:
  u32 elementCount
  elementCount × fieldBytes payload
```

Поток плотный, без runtime padding. Поэтому file `packedOffset` нельзя автоматически считать offset живой C++ структуры. Runtime bridge подтверждается отдельно по size/hash/signature/DTI и A/B.

Основные type IDs:

| ID | Type | Bytes |
|---:|---|---:|
| 1 | class | 4 |
| 2 | classref | 4 |
| 3 | bool | 1 |
| 4 | u8 | 1 |
| 6 | u32 | 4 |
| 10 | s32 | 4 |
| 12 | f32 | 4 |
| 13 | f64 | 8 |
| 14 | string | 36 |
| 20 | vector3 | 16 |

Parsers:

```bash
python3 tools/xfs_dump.py --values <file>
python3 tools/xfs_tree_dump.py <nested-xfs-resource>
```

`xfs_tree_dump.py` обязателен для nested arrays/references (`cmc.prt`, GOAP, `AIPlActParam`). `.eap/.sn2/.stg` не являются обычным XFS и разбираются отдельными tools.

## 2. Enemy charparam `.prp`

`charparam/em/em0100_cmn.prp`:

```text
class sizeof = 320 (0x140)
property hash = 0x7E509FE9
72 named fields
runtime pair = cCharParamEnemy
```

Ключевые runtime offsets внутри charparam block:

| Offset | Field |
|---:|---|
| `+0x000` | XP reward |
| `+0x004/+0x008` | physical ATK/DEF |
| `+0x00C/+0x010` | magic ATK/DEF |
| `+0x014` | weight |
| `+0x030..+0x048` | elemental/physical damage multipliers |
| `+0x04C..+0x098` | status accumulation thresholds |
| `+0x09C/+0x0A0` | flinch/knockdown guard |
| `+0x100/+0x104` | return-territory activation/duration |
| `+0x12C` | model scale |

Полную property table всегда можно воспроизвести parser-ом из extracted resource; вручную продублированная таблица в документации не является источником истины.

Пример базовых значений:

| Species | ATK | DEF | MATK | MDEF | Weight |
|---|---:|---:|---:|---:|---:|
| Goblin `em0100` | 250 | 75 | 80 | 75 | 40 |
| Hobgoblin `em0101` | 410 | 140 | 100 | 120 | 80 |
| Grimgoblin `em0102` | 800 | 240 | 400 | 230 | 40 |

`人間敵 HP` внутри этого resource относится к human-enemy branch и не является универсальным current/max HP гоблина.

## 3. Resist/status `.rst`

`.rst` хранит max HP и flinch/knockdown pool parameters. Подтверждённые max HP:

| Species | maxHP |
|---|---:|
| Goblin | 1000 |
| Hobgoblin | 2000 |
| Grimgoblin | 6000 |

Числа вроде `61/80/15`, наблюдавшиеся в старых runtime probes, не являются HP; это значения pools/состояний. Current HP generic enemy body остаётся отдельной live задачей.

Parser:

```bash
python3 tools/rst_dump.py <file.rst>
```

## 4. Sensors `.sn2`

`AI/Sensor/Enemy/em0100A.sn2` имеет magic `SNR2`, version 18, пять 80-byte records после 16-byte header.

Наблюдаемая запись содержит sensor type, masks, owner group, distance/height, cone angle, joint и local offset. Для Goblin один профиль читается как узкое зрение (`distance 1500`, cone `60°`, head joint), остальные как 360° zones.

Это CATALOG interpretation. Конкретное поле live sensor и момент aggro должны подтверждаться runtime bridge; `.sn2` сам по себе не доказывает current target.

Parser:

```bash
python3 tools/sn2_dump.py <file.sn2>
```

## 5. Sensor target `.stg`

`.stg` использует отдельный `STGU` container. Он описывает targetability/target zones, но полная семантика полей ещё не закреплена. Не подавать `.stg` в XFS parser и не объявлять совпадающие числа runtime offsets без A/B.

## 6. Enemy action table `.eap`

`em0100_enemy_act_param.eap`:

```text
magic EAP_
92 records × 232 B (+ tail)
```

Устойчивые наблюдаемые поля записи:

| Offset | Observation |
|---:|---|
| `+0x08C` | action parameter/flags |
| `+0x090` | group/index-like value |
| `+0x094` | enabled-like flag |
| `+0x0B8/+0x0BC` | `-1` sentinels |
| `+0x0C0` | motion ID or `-1` |

Связь конкретной row с live decision должна подтверждаться отдельно. Свободные/нулевые slots не означают, что туда безопасно дописывать новый action.

## 7. Shell/effect `.shl`

XFS shell resources содержат projectile/effect parameters: speed, acceleration, gravity, lifetime, durability, hit limits, sensor-target range, lock-on distance/angle и resource paths.

Типичные properties:

```text
spd / spdMax / spdAccel
gravity / gravityMax / gravityAccel
dieFrame / dieDist
maxHitCount / durable
isSensorTarget / sensorTargetRange
isAutoLockon / lockonDist / lockonDeg
colTimer_0/1/2
```

Motion speed changes могут изменить collision timing. Любая runtime mutation требует проверки фактического урона/коллизии.

## 8. Lock-on `.ltg`

XFS lock-on point records содержат:

```text
mJoint
mOffset
mRadius
mAttr
mAtemiOffset
mAtemiRot
```

Это target points, а не current target selector.

Подтверждено на `param\lockon\m000cmc.ltg` (Build 59.x, через xfs_dump):
- XFS ver `0x0109.2.1`, 2 класса, 2 инстанса;
- класс `rLockOnTarget` (120 B): `mQuality` u32, `mArray` classref;
- класс записи (128 B, packed 80): `mJoint` s32, `mOffset` vector3,
  `mRadius` f32, `mAttr` u32, `mAtemiOffset` vector3, `mAtemiRot` vector3;
- **`m000cmc.ltg` — конфиг локона ГЛАВНОЙ ПЕШКИ: `mQuality=2` (две точки
  локона), `mRadius=10.0`** (мировые единицы ~см → 10 м).

`mRadius=10.0` совпадает с live `sLockOnManager::cLockOnTarget +0x38`
(Build 59.2) и с нашим Guardian preempt-радиусом 10 м — то есть 10 м это
штатная дальность локона/engagement пешки, а не произвольная наша константа.

## 9. Motion `.lmt`

```text
magic "LMT\0"
u16 version (DDDA observed 66)
u16 count
u32[count] entry offsets (0 = empty)
```

Parser:

```bash
python3 tools/lmt_dump.py <file.lmt>
```

`.lmt` задаёт motion data/duration, но не является полным AI action catalog. `cCmc*`, GOAP и `cPlAct*` — разные уровни.

## 10. Pawn AI resource set

В `resources/extracted_assets/pawnAI/AI/` импортированы 83 файла:

| Layer | Resource | Parsed result |
|---|---|---|
| priority policy | `PrioThink/cmc.prt` | 85 rows, 48 personality rules, 41 order rules |
| GOAP | `Goap/Cmc/*.gop` | 68 goals, 167 interfaces |
| action eligibility | `AIPlayerActionParameter/*` | 9 tables, 352 rows |
| sensors | `Sensor/Cmc/cmc_normal.sn2` | 9 profiles |
| target definitions | `SensorTarget/*.stg` | raw/partial schema |

Canonical machine-readable outputs:

```text
docs/PLAYER_PAWN_WORK/generated/pawn_ai_catalog.json
docs/PLAYER_PAWN_WORK/generated/cmc_priority_rows.csv
docs/PLAYER_PAWN_WORK/generated/cmc_goap_interfaces.csv
docs/PLAYER_PAWN_WORK/generated/AIPlActParam*.csv
```

Runtime class matches:

```text
cmc.prt root sizeof 1064 == rAIPriorityThink sizeof 1064
AIPlActParam root sizeof 120 == rAIPlayerActionParameter sizeof 120
```

Actual live layouts and write contracts are in `SOURCE_OF_TRUTH.md` / `FIELD_MAP.md`.

## 11. AI action parameter CSV meaning

For each weapon family and `Lv1/Lv2/LvEX`:

```text
RangeMinXZ / RangeMaxXZ
RangeMinY / RangeMaxY
EnableMinXZ / EnableMaxXZ
ElementAttr / AtkAttr / UseAtr
```

These are AI target/use range and eligibility parameters. They are not proven physical damage radius/hitbox. The future workflow is resource row → live object → rollback-safe A/B → downstream action observation.

## 12. ARC/PACK policy

ARCtool and repack remain research/fallback tools. Do not overwrite `game_main.arc` as the primary profile mechanism. PACK is justified only when a required resource/LOT/GPL/state graph is absent and the live engine cannot instantiate it.

Paths and archive ownership: `ARC_MAP.txt`.
